#ifndef HTTP_SESSION_H
#define HTTP_SESSION_H

#include "Cache.h"



namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(tcp::socket socket, std::uint64_t session_id);
    void start();
private:
    std::uint64_t session_id_;
    tcp::socket client_socket_;
    tcp::socket remote_socket_;
    tcp::resolver resolver_;

    // Buffer setup
    beast::flat_buffer client_buffer_, remote_buffer_;
    http::request<http::dynamic_body> request_;
    http::response<http::dynamic_body> response_;

    std::vector<char> connect_client_buffer_;
    std::vector<char> connect_remote_buffer_;


    void read_http_request();
    void read_http_response();
    void write_http_request();
    void write_http_response(bool fromCache);

    void handle_get();
    void handle_post(bool is_get);
    void handle_connect();
    void remote_connect(tcp::resolver::results_type::iterator endpoint_iterator);
    void send_connect_response();
    void start_connect_relay();
    void connect_read_from_client();
    void connect_read_from_remote();
    void connect_write_to_client(size_t length);
    void connect_write_to_remote(size_t length);
    std::string get_content_type(const std::string& uri);
    std::tuple<std::string, std::string> get_host_and_port(bool is_connect);
    void handle_revalidation(CachedResponse* cachedResponse);

    void log_request();

    std::string get_client_ip_address();
};


#endif //HTTP_SESSION_H
