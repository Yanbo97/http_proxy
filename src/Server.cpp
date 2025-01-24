#include "Server.h"

std::atomic<std::uint64_t> Server::session_id_counter(0);
Server::Server(asio::io_context &io_context, tcp::endpoint endpoint) :
        io_context_(io_context),
        acceptor_(asio::make_strand(io_context)) {
    beast::error_code ec;
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "Error code when opening: " << ec.message();
        return;
    }
    acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "Error code when setting option: " << ec.message();
        return;
    }
    acceptor_.bind(endpoint, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "Error code when binding: " << ec.message();
        return;
    }

    // Start listening for connections
    acceptor_.listen(
            asio::socket_base::max_listen_connections, ec);
    if (ec) {
        BOOST_LOG_TRIVIAL(error) << "Error code when listening: " << ec.message();
        return;
    }


}

void Server::run() {
    asio::dispatch(
            acceptor_.get_executor(),
            beast::bind_front_handler(
                    &Server::do_accept,
                    shared_from_this()
            )
    );
}

void Server::do_accept() {
    acceptor_.async_accept(
            asio::make_strand(io_context_),
            beast::bind_front_handler(
                    &Server::on_accept,
                    shared_from_this()
            )
    );

}

void Server::on_accept(beast::error_code ec, tcp::socket socket) {
    if(ec)
    {
        BOOST_LOG_TRIVIAL(error) << "Error code when accepting: " << ec.message();
    }
    else
    {
        // Create the http session and run it
        auto session_id = session_id_counter.fetch_add(1, std::memory_order_relaxed);
        std::make_shared<Session>(
                std::move(socket), session_id)->start();
    }

    // Accept another connection
    do_accept();
}