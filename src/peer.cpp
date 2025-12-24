#include "peer.hpp"

#include <iostream>
#include <sstream>

Peer::Peer(boost::asio::io_context& ctx, uint16_t listen_port, std::string id)
    : ctx_(ctx),
      acceptor_(ctx, tcp::endpoint(tcp::v4(), listen_port)),
      id_(std::move(id)),
      listen_port_(listen_port),
      ping_timer_(ctx_)
{
    // Запускаем STUN-обнаружение
    stun_client_ = std::make_unique<StunClient>(ctx_);
    discover_public_address();
}


void Peer::start() {
    do_accept();
    std::cout << "Peer " << id_ << " listening on port "
              << acceptor_.local_endpoint().port() << "\n";

    schedule_ping(); // <–– запускаем heartbeat
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
    auto resolver = std::make_shared<tcp::resolver>(ctx_);
    auto sock = std::make_shared<tcp::socket>(ctx_);

    resolver->async_resolve(
        host, std::to_string(port),
        [this, resolver, sock, host, port](boost::system::error_code ec,
                                           tcp::resolver::results_type results) {
            if (ec) {
                std::cerr << "Resolve error: " << ec.message() << "\n";
                return;
            }

            boost::asio::async_connect(
                *sock, results,
                [this, sock, host, port](boost::system::error_code ec2,
                                         const tcp::endpoint& ep) {
                    if (ec2) {
                        std::cerr << "Connect error: "
                                  << ec2.message() << "\n";
                        return;
                    }
                    std::cout << "Connected to " << ep << "\n";

                    // тут добавляем именно слушающий host:port
                    add_known_peer(host, port);

                    auto conn = std::make_shared<Connection>(
                        std::move(*sock),
                        [this](const std::string& msg,
                               std::shared_ptr<Connection> c) {
                            on_message(msg, std::move(c));
                        }
                    );
                    connections_.push_back(conn);
                    conn->start();

                    conn->send_line("HELLO " + id_);

                    auto peers_msg = make_peers_message();
                    if (!peers_msg.empty()) {
                        std::cout << "[" << id_ << "] sending: " << peers_msg << "\n";
                        conn->send_line(peers_msg);
                    }
                }
            );
        }
    );
}


void Peer::discover_public_address() {
    stun_client_->get_public_address([this](StunClient::Result result) {
        on_stun_result(result); // <-- вызываем метод
    });
}

void Peer::on_message(const std::string& msg, std::shared_ptr<Connection> conn) {
    std::cout << "[" << id_ << "] got: " << msg << "\n";

    if (msg.rfind("HELLO", 0) == 0) {
        conn->send_line("PONG from " + id_);

        auto peers_msg = make_peers_message();
        if (!peers_msg.empty()) {
            std::cout << "[" << id_ << "] sending: " << peers_msg << "\n";
            conn->send_line(peers_msg);
        }

        // Отправляем свой публичный адрес
        if (!public_ip_.empty()) {
            conn->send_line("PUBLIC " + public_ip_ + ":" + std::to_string(public_port_));
        }
    }
    else if (msg.rfind("PEERS", 0) == 0) {
        handle_peers_message(msg);
    }
    else if (msg.rfind("PING", 0) == 0) {
        conn->send_line("PONG");
    }
    else if (msg.rfind("PUBLIC", 0) == 0) {
        // Обрабатываем PUBLIC сообщение
        std::istringstream iss(msg);
        std::string cmd, addr;
        iss >> cmd >> addr;

        auto pos = addr.find(':');
        if (pos != std::string::npos) {
            std::string host = addr.substr(0, pos);
            std::string port_str = addr.substr(pos + 1);

            try {
                uint16_t port = static_cast<uint16_t>(std::stoi(port_str));
                add_known_peer(host, port);
                maybe_connect_to_peer(host, port);
            } catch (...) {
                std::cerr << "[" << id_ << "] failed to parse PUBLIC: " << addr << "\n";
            }
        }
    }
}

void Peer::on_stun_result(StunClient::Result result) {
    
    std::cout << "[" << id_ << "] ✅ on_stun_result " "\n";
    if (result.success) {
        public_ip_ = result.public_ip;
        public_port_ = result.public_port;
        std::cout << "[" << id_ << "] ✅ STUN SUCCESS: Public address: " << public_ip_ << ":" << public_port_ << "\n";

        // Рассылаем свой публичный адрес другим пирам
        for (auto& conn : connections_) {
            if (conn) {
                conn->send_line("PUBLIC " + public_ip_ + ":" + std::to_string(public_port_));
            }
        }
    } else {
        std::cerr << "[" << id_ << "] ❌ STUN FAILED: " << result.error_message << "\n";
        std::cerr << "[" << id_ << "] Using local IP as fallback\n";
        public_ip_ = acceptor_.local_endpoint().address().to_string();
        public_port_ = listen_port_;
    }
}

bool Peer::add_known_peer(const tcp::endpoint& ep) {
    auto host = ep.address().to_string();
    auto port = static_cast<uint16_t>(ep.port());
    return add_known_peer(host, port);
}

bool Peer::add_known_peer(const std::string& host, uint16_t port) {
    std::string key = host + ":" + std::to_string(port);
    auto [it, inserted] = known_peers_.insert(key);
    if (inserted) {
        std::cout << "[" << id_ << "] added peer: " << key << "\n";
    }
    return inserted;
}

std::string Peer::make_peers_message() const {
    if (known_peers_.empty()) {
        return {};
    }

    std::string msg = "PEERS";
    for (const auto& p : known_peers_) {
        msg += " " + p;
    }
    return msg;
}

void Peer::handle_peers_message(const std::string& msg) {
    std::cout << "[" << id_ << "] handling peers: " << msg << "\n";

    std::istringstream iss(msg);
    std::string cmd;
    iss >> cmd; // "PEERS"

    std::string token;
    while (iss >> token) {
        auto pos = token.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        std::string host = token.substr(0, pos);
        std::string port_str = token.substr(pos + 1);

        try {
            uint16_t port = static_cast<uint16_t>(std::stoi(port_str));

            // если узел новый – добавляем и пробуем к нему подключиться
            if (add_known_peer(host, port)) {
                maybe_connect_to_peer(host, port);
            }
        }
        catch (...) {
            std::cerr << "[" << id_ << "] failed to parse peer: " << token << "\n";
        }
    }
}


void Peer::maybe_connect_to_peer(const std::string& host, uint16_t port) {
    // не коннектимся к себе по своему порту
    if (port == listen_port_) {
        return;
    }

    std::string key = host + ":" + std::to_string(port);

    // если уже инициировали исходящее соединение – больше не трогаем
    if (outbound_peers_.count(key) > 0) {
        return;
    }

    std::cout << "[" << id_ << "] auto-connect to " << key << "\n";
    outbound_peers_.insert(key);

    // используем уже существующий connect_to
    connect_to(host, port);
}

void Peer::schedule_ping() {
    using namespace std::chrono_literals;

    ping_timer_.expires_after(15s);
    ping_timer_.async_wait([this](boost::system::error_code ec) {
        if (ec == boost::asio::error::operation_aborted) {
            return;
        }

        // Чистим все соединения, у которых сокет закрыт
        connections_.erase(
            std::remove_if(
                connections_.begin(),
                connections_.end(),
                [](const std::shared_ptr<Connection>& c) {
                    return !c || !c->socket().is_open();
                }
            ),
            connections_.end()
        );

        if (!ec) {
            std::cout << "[" << id_ << "] sending heartbeat PING to "
                      << connections_.size() << " connections\n";

            for (auto& conn : connections_) {
                if (conn) {
                    conn->send_line("PING");
                }
            }
        }

        schedule_ping();
    });
}
