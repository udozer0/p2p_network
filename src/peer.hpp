#pragma once
#include <utility>

#include <boost/asio.hpp>
#include <memory>
#include <vector>
#include <string>
#include "connection.hpp"

class Peer {
public:
    using tcp = boost::asio::ip::tcp;

    Peer(boost::asio::io_context& ctx, uint16_t listen_port, std::string id);

    void start();
    void connect_to(const std::string& host, uint16_t port);

private:
    void do_accept();
    void on_message(const std::string& msg, std::shared_ptr<Connection> conn);

    boost::asio::io_context& ctx_;
    tcp::acceptor acceptor_;
    std::string id_;

    std::vector<std::shared_ptr<Connection>> connections_;
};
