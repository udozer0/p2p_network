#pragma once
#include <utility>
#include <set>
#include <string>
#include <vector>
#include <memory>
#include <boost/asio.hpp>

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

    bool add_known_peer(const tcp::endpoint& ep);
    bool add_known_peer(const std::string& host, uint16_t port);

    std::string make_peers_message() const;
    void handle_peers_message(const std::string& msg);

    void maybe_connect_to_peer(const std::string& host, uint16_t port);
    
    boost::asio::io_context& ctx_;
    tcp::acceptor acceptor_;
    std::string id_;
    uint16_t listen_port_;


    std::vector<std::shared_ptr<Connection>> connections_;
    std::set<std::string> known_peers_;     // "ip:port"
    std::set<std::string> outbound_peers_;  // к кому уже инициировали connect

};
