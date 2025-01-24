//
// Created by ritik and yanbo on 2/24/24.
//

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "Session.h"

class Server : public std::enable_shared_from_this<Server> {
public:
    Server(asio::io_context& io_context, tcp::endpoint endpoint);
    void run();
private:
    asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    static std::atomic<std::uint64_t> session_id_counter;


    void do_accept();
    void on_accept(beast::error_code ec, tcp::socket socket);

};

#endif //HTTP_SERVER_H
