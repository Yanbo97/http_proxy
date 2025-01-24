#include "Session.h"

Session::Session(tcp::socket socket, std::uint64_t session_id) :
        client_socket_(std::move(socket)),
        remote_socket_(client_socket_.get_executor()),
        resolver_(client_socket_.get_executor()),
        connect_client_buffer_(1024),
        connect_remote_buffer_(1024),
        session_id_(session_id) {}

void Session::start() {
    read_http_request();
}

void Session::read_http_request() {
    auto self(shared_from_this());
    http::async_read(client_socket_, client_buffer_, request_,
                     [self, this](boost::system::error_code ec, size_t length) {
                         if (!ec) {
                             log_request();
                             switch (request_.method()) {
                                 case http::verb::get:
                                     handle_get();
                                     break;
                                 case http::verb::post:
                                     handle_post(false);
                                     break;
                                 case http::verb::connect:
                                     handle_connect();
                                     break;
                                 default:
                                     BOOST_LOG_TRIVIAL(error) << session_id_ << ": Bad Request";
                                     break;
                             }
                         } else {
                             BOOST_LOG_TRIVIAL(error) << session_id_ << ": Error receiving request. " << ec.message();
                         }
                     });
}
void Session::handle_get() {
    auto self = shared_from_this();
    std::string uri(request_.target().data(), request_.target().size());

    CachedResponse* cachedResponse = Cache::get_request(uri, session_id_);
    if (cachedResponse != nullptr) {
        if (Cache::check_revalidate(*cachedResponse, session_id_)) {
            BOOST_LOG_TRIVIAL(info) << session_id_ << ": in cache, requires validation";
            handle_revalidation(cachedResponse);
        } else {
            if (Cache::check_expiration(*cachedResponse, session_id_)){
                std::string expirationStr = cachedResponse->getExpirationTime();
                BOOST_LOG_TRIVIAL(info) << session_id_ << ": in cache, but expired at " << expirationStr;
                handle_revalidation(cachedResponse);
            }
            else {
                BOOST_LOG_TRIVIAL(info) << session_id_ << ": in cache, valid";

                response_ = cachedResponse->response;
                response_.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                response_.set("X-Cache", "HIT");
                write_http_response(true);
            }
        }
    } else {
        BOOST_LOG_TRIVIAL(info) << session_id_ << ": not in cache";
        handle_post(true);
    }
}


void Session::handle_revalidation(CachedResponse* cachedResponse) {
    // Prepare a revalidation request based on ETag or Last-Modified
    if (!cachedResponse->ETag.empty()) {
        request_.set(boost::beast::http::field::if_none_match, boost::beast::string_view(cachedResponse->ETag));
        BOOST_LOG_TRIVIAL(info) << session_id_ << ": NOTE handle revalidation with ETag";
    } else if (!cachedResponse->Last_Modified.empty()) {
        request_.set(boost::beast::http::field::if_modified_since, boost::beast::string_view(cachedResponse->Last_Modified));
        BOOST_LOG_TRIVIAL(info) << session_id_ << ": NOTE handle revalidation with Last Modified";
    }
    handle_post(true);
}

//void Session::handle_get() {
//    auto self = shared_from_this();
//    std::string uri(request_.target().data(), request_.target().size());
//
//    CachedResponse *cachedResponse = Cache::get_request(uri, session_id_);
//    if (cachedResponse != nullptr) {
//        response_ = {};
//        response_.result(http::status::ok);
//        response_.version(request_.version());
//        response_.set(http::field::server, BOOST_BEAST_VERSION_STRING);
//        response_.set(http::field::content_type, get_content_type(uri));
//        response_.set("X-Cache", "HIT"); // Custom header indicating cache hit
//        response_.content_length(cachedResponse->body.size());
//
//        boost::beast::ostream(response_.body()) << cachedResponse->body;
//
//        write_http_response(true); // Indicate that the response is from cache
//    } else {
//        BOOST_LOG_TRIVIAL(info) << session_id_ << ": not in cache";
//        handle_post(true);
//    }
//}


void Session::handle_post(bool is_get) {
    auto self(shared_from_this());
    auto [host, port] = get_host_and_port(false);
    BOOST_LOG_TRIVIAL(info) << session_id_ << ": Requesting \"" << "HTTP Request: "
                            << request_.method_string() << " "
                            << std::string(request_.target()) << " "
                            << "HTTP/" << request_.version() / 10 << "." << request_.version() % 10 << "\" from "
                            << host;
    resolver_.async_resolve(host, port,
                            [this, self, is_get](boost::system::error_code ec, tcp::resolver::results_type results) {
                                if (!ec) {
                                    remote_connect(std::move(results));
                                }
                            });
}


void Session::remote_connect(tcp::resolver::results_type::iterator endpoint_iterator) {
    auto self(shared_from_this());
    asio::async_connect(remote_socket_, std::move(endpoint_iterator),
                        [self, this](boost::system::error_code ec, const tcp::resolver::iterator &) {
                            if (!ec) {
                                write_http_request();
                            }
                        });
}

void Session::write_http_request() {
    auto self(shared_from_this());
    http::async_write(remote_socket_, request_,
                      [self, this](boost::system::error_code ec, size_t length) {
                          if (!ec) {
                              read_http_response();
                          }
                      });
}
//
//void Session::read_http_response() {
//    auto self(shared_from_this());
//    http::async_read(remote_socket_, remote_buffer_, response_,
//                     [self, this](boost::system::error_code ec, size_t length) {
//                         if (!ec) {
//                             write_http_response(false);
//                             remote_socket_.shutdown(tcp::socket::shutdown_send, ec);
//                             remote_socket_.close();
//                         }
//                     });
//}

void Session::read_http_response() {
    auto self(shared_from_this());
    http::async_read(remote_socket_, remote_buffer_, response_,
                     [self, this](boost::system::error_code ec, size_t length) {
                         if (!ec) {
                             std::string uri = std::string(request_.target().data(), request_.target().length());

                             if (response_.result() == http::status::not_modified) {
                                 // case for 304
                                 auto cachedResponse = Cache::get_request(uri, session_id_);
                                 if (cachedResponse) {
                                     Cache::update_expiration(uri, response_["Cache-Control"].to_string(), response_["Expires"].to_string(), session_id_);
                                     response_ = cachedResponse->response;
                                     response_.set(http::field::server, "HTTP Proxy");
                                     response_.set("X-Cache", "HIT from proxy");
                                     write_http_response(true);
                                 }
                             } else {
                                 // case for 200
                                 Cache::store_request(uri, response_, session_id_);
                                 write_http_response(false);
                             }

                             remote_socket_.shutdown(tcp::socket::shutdown_send, ec);
                             remote_socket_.close();
                         }
                     });
}

void Session::write_http_response(bool fromCache) {
    auto self = shared_from_this();
    auto [host, port] = get_host_and_port(false);
    BOOST_LOG_TRIVIAL(info) << session_id_ << ": Received \""
                            << "HTTP/" << response_.version() / 10 << "." << response_.version() % 10 << " "
                            << response_.result_int() << " " // Status code
                            << response_.reason()
                            << "\" from " << host;

    http::async_write(client_socket_, response_, [self, this, fromCache](boost::system::error_code ec, size_t length) {
        if (!ec) {

            BOOST_LOG_TRIVIAL(info) << session_id_ << ": Response served from " << (fromCache ? "cache" : "origin server");

            if (request_.method() == http::verb::get && response_.result() == http::status::ok && !fromCache) {

                std::string uri(request_.target().data(), request_.target().length());


                Cache::store_request(uri, response_, session_id_);
            }


            client_socket_.shutdown(tcp::socket::shutdown_send, ec);
            client_socket_.close();
            boost::log::core::get()->flush();
        }
    });
}
//void Session::write_http_response(bool fromCache) {
//    auto self = shared_from_this();
//    auto [host, port] = get_host_and_port(false);
//    BOOST_LOG_TRIVIAL(info) << session_id_ << ": Received \""
//                            << "HTTP/" << response_.version() / 10 << "." << response_.version() % 10 << " "
//                            << response_.result_int() << " " // Status code
//                            << response_.reason()
//                            << "\" from " << host;
//
//    http::async_write(client_socket_, response_,
//                      [self, this, fromCache](boost::system::error_code ec, size_t length) { // Capture fromCache
//                          if (!ec) {
//                              if (fromCache) {
//                                  BOOST_LOG_TRIVIAL(info) << session_id_ << ": Response served from cache" << std::endl;
//                              } else {
//                                  BOOST_LOG_TRIVIAL(info) << session_id_ << ": Response served from origin server"
//                                                          << std::endl;
//                              }
//
//                              if (request_.method() == http::verb::get && response_.result() == http::status::ok &&
//                                  !fromCache) {
//                                  // Only store in cache if the response is not already from cache
//                                  std::stringstream responseStr;
//                                  for (const auto &field: response_) {
//                                      responseStr << field.name_string() << ": " << field.value() << "\r\n";
//                                  }
//                                  std::string body = boost::beast::buffers_to_string(response_.body().data());
//
//                                  std::string uri(request_.target().data(), request_.target().length());
//                                  int ttl = 60; // Set expiration time here
//                                  Cache::store_request(uri, body, responseStr.str(), session_id_);
//                              }
//                              client_socket_.shutdown(tcp::socket::shutdown_send, ec);
//                              client_socket_.close();
//                              boost::log::core::get()->flush();
//                          }
//                      });
//}


//void Session::read_http_response() {
//    auto self(shared_from_this());
//    http::async_read(remote_socket_, remote_buffer_, response_,
//                     [self, this](boost::system::error_code ec, size_t length) {
//                         if (!ec) {
//                             write_http_response();
//                             remote_socket_.shutdown(tcp::socket::shutdown_send, ec);
//                         }
//                     });
//}

//void Session::write_http_response() {
//    auto self(shared_from_this());
//    http::async_write(client_socket_, response_,
//                      [self, this](boost::system::error_code ec, size_t length) {
//                          if (!ec) {
//                              read_http_response();
//                              client_socket_.shutdown(tcp::socket::shutdown_send, ec);
//                          }
//                      });
//}

std::tuple<std::string, std::string> Session::get_host_and_port(bool is_connect) {
    std::string host;
    if (is_connect) {
        host = std::string(request_.target());
    } else {
        host = std::string(request_[http::field::host]);
    }
    std::string hostname, port;

    // Find the last colon (':')
    std::size_t colon_pos = host.find_last_of(':');

    if (colon_pos != std::string::npos && colon_pos != host.length() - 1) { // Colon found and not at the end
        hostname = host.substr(0, colon_pos);
        port = host.substr(colon_pos + 1);
    } else {
        hostname = host; // No colon found, entire string is the hostname
        port = "80"; // Default port can depend on the protocol (e.g., 80 for HTTP, 443 for HTTPS)
    }

    return std::make_tuple(hostname, port);
}


void Session::handle_connect() {
    auto self(shared_from_this());
    auto [host, port] = get_host_and_port(true);
    resolver_.async_resolve(host, port,
                            [this, self](boost::system::error_code ec, tcp::resolver::results_type results) {
                                if (!ec) {
                                    tcp::resolver::results_type::iterator endpoint_iterator = std::move(results);
                                    asio::async_connect(remote_socket_, std::move(endpoint_iterator),
                                                        [self, this](boost::system::error_code ec,
                                                                     const tcp::resolver::iterator &) {
                                                            if (!ec) {
                                                                send_connect_response();
                                                            }
                                                        });
                                }
                            });
}

void Session::send_connect_response() {
    std::string response =
            "HTTP/1.1 200 Connection established\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n"; // Note: Adjust headers as needed

    // Convert the response string into a buffer
    auto response_buffer = asio::buffer(response);

    auto self(shared_from_this());
    client_socket_.async_write_some(response_buffer,
                                    [self, this](boost::system::error_code ec, size_t length) {
                                        if (!ec) {
                                            BOOST_LOG_TRIVIAL(info) << session_id_ << ": Tunnel Established"
                                                                    << std::endl;
                                            start_connect_relay();
                                        } else {
                                            BOOST_LOG_TRIVIAL(error) << session_id_ << ": Error occurred: " << ec
                                                                     << " - " << ec.message() << std::endl;
                                        }
                                    });

}

void Session::start_connect_relay() {
    BOOST_LOG_TRIVIAL(info) << session_id_ << ": starting Relay" << std::endl;
    connect_read_from_client();
    connect_read_from_remote();
}

void Session::connect_read_from_client() {
    auto self(shared_from_this());
    client_socket_.async_read_some(asio::buffer(connect_client_buffer_.data(), 1024),
                                   [self, this](boost::system::error_code ec, std::size_t bytes_transferred) {
                                       if (!ec && bytes_transferred > 0) {
                                           connect_write_to_remote(bytes_transferred);
                                       } else {
                                           BOOST_LOG_TRIVIAL(error) << session_id_ << ": Error occurred: " << ec
                                                                    << " - " << ec.message() << std::endl;
                                           client_socket_.close();
                                           remote_socket_.close();
                                       }
                                   });
}

void Session::connect_write_to_remote(size_t length) {
    auto self(shared_from_this());
    remote_socket_.async_write_some(asio::buffer(connect_client_buffer_.data(), length),
                                    [self, this](boost::system::error_code ec, size_t length) {
                                        if (!ec && length > 0) {
                                            connect_read_from_client();
                                        } else {
                                            client_socket_.close();
                                            remote_socket_.close();
                                        }
                                    });
}

void Session::connect_read_from_remote() {
    auto self(shared_from_this());
    remote_socket_.async_read_some(asio::buffer(connect_remote_buffer_),
                                   [self, this](boost::system::error_code ec, size_t length) {
                                       if (!ec && length > 0) {
                                           connect_write_to_client(length);
                                       } else {
                                           client_socket_.close();
                                           remote_socket_.close();
                                       }
                                   });
}

void Session::connect_write_to_client(size_t length) {
    auto self(shared_from_this());
    client_socket_.async_write_some(asio::buffer(connect_remote_buffer_.data(), length),
                                    [self, this](boost::system::error_code ec, size_t length) {
                                        if (!ec && length > 0) {
                                            connect_read_from_remote();
                                        } else {
                                            BOOST_LOG_TRIVIAL(info) << session_id_ << ": Tunnel Closed" << std::endl;
                                            boost::log::core::get()->flush();
                                            client_socket_.close();
                                            remote_socket_.close();
                                        }
                                    });
}

std::string Session::get_content_type(const std::string &uri) {
    const std::map<std::string, std::string> types = {
            {".html", "text/html"},
            {".htm",  "text/html"},
            {".js",   "application/javascript"},
            {".css",  "text/css"},
            {".json", "application/json"},
            {".png",  "image/png"},
            {".jpg",  "image/jpeg"},
            {".jpeg", "image/jpeg"},
            {".gif",  "image/gif"},
            {".svg",  "image/svg+xml"},
            {".txt",  "text/plain"},
            {".xml",  "application/xml"},
    };

    std::size_t dot_pos = uri.rfind('.');
    if (dot_pos != std::string::npos) {
        std::string ext = uri.substr(dot_pos);
        auto it = types.find(ext);
        if (it != types.end()) {
            return it->second;
        }
    }
    return "text/html"; // Default type
}

void Session::log_request() {
    BOOST_LOG_TRIVIAL(info) << session_id_ << ": "
                            << "HTTP Request: "
                            << request_.method_string() << " "
                            << std::string(request_.target()) << " "
                            << "HTTP/" << request_.version() / 10 << "." << request_.version() % 10
                            << "from " << get_client_ip_address() << "@";
}

std::string Session::get_client_ip_address() {
    auto endpoint = client_socket_.remote_endpoint();
    auto endpoint_address = endpoint.address();
    return endpoint_address.to_string();
}










