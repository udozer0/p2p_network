#include "peer.hpp"
#include <iostream>

Peer::Peer(boost::asio::io_context& ctx, uint16_t listen_port, std::string id)
    : ctx_(ctx),
      acceptor_(ctx, tcp::endpoint(tcp::v4(), listen_port)),
      id_(std::move(id))
{
}

void Peer::start() {
    do_accept();
    std::cout << "Peer " << id_ << " listening on port "
              << acceptor_.local_endpoint().port() << "\n";
}

void Peer::do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::cout << "Incoming connection from "
                          << socket.remote_endpoint() << "\n";

                auto conn = std::make_shared<Connection>(
                    std::move(socket),
                    [this](const std::string& msg,
                           std::shared_ptr<Connection> c) {
                        on_message(msg, std::move(c));
                    }
                );
                connections_.push_back(conn);
                conn->start();
            } else {
                std::cerr << "Accept error: " << ec.message() << "\n";
            }
            do_accept();
        }
    );
}

void Peer::connect_to(const std::string& host, uint16_t port) {
    auto& ctx = ctx_;
    auto resolver = std::make_shared<tcp::resolver>(ctx);
    auto sock = std::make_shared<tcp::socket>(ctx);

    resolver->async_resolve(
        host, std::to_string(port),
        [this, resolver, sock](boost::system::error_code ec,
                               tcp::resolver::results_type results) {
            if (ec) {
                std::cerr << "Resolve error: " << ec.message() << "\n";
                return;
            }

            boost::asio::async_connect(
                *sock, results,
                [this, sock](boost::system::error_code ec2,
                             const tcp::endpoint& ep) {
                    if (ec2) {
                        std::cerr << "Connect error: "
                                  << ec2.message() << "\n";
                        return;
                    }
                    std::cout << "Connected to " << ep << "\n";

                    auto conn = std::make_shared<Connection>(
                        std::move(*sock),
                        [this](const std::string& msg,
                               std::shared_ptr<Connection> c) {
                            on_message(msg, std::move(c));
                        }
                    );
                    connections_.push_back(conn);
                    conn->start();

                    // отправим HELLO при подключении
                    conn->send_line("HELLO " + id_);
                }
            );
        }
    );
}

void Peer::on_message(const std::string& msg, std::shared_ptr<Connection> conn) {
    std::cout << "[" << id_ << "] got: " << msg << "\n";

    if (msg.rfind("HELLO", 0) == 0) {
        conn->send_line("PONG from " + id_);
    } else if (msg.rfind("PING", 0) == 0) {
        conn->send_line("PONG");
    }
    // здесь потом добавишь PEERS, DATA и т.д.
}
