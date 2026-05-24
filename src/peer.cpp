#include "peer.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

Peer::Peer(boost::asio::io_context& ctx, uint16_t listen_port, std::string id)
    : ctx_(ctx),
      acceptor_(ctx, tcp::endpoint(tcp::v4(), listen_port)),
      id_(std::move(id)),
      listen_port_(listen_port),
      ping_timer_(ctx_)
{
    std::cout << "[" << id_ << "] ctor: starting, listen_port=" << listen_port_ << "\n";

    stun_client_ = std::make_unique<StunClient>(ctx_);
    if (const char* public_ip = std::getenv("P2P_PUBLIC_IP"); public_ip && *public_ip) {
        public_ip_ = public_ip;
        public_port_ = listen_port_;
        std::cout << "[" << id_ << "] using P2P_PUBLIC_IP: " << public_ip_ << ":" << public_port_ << "\n";
    } else {
        discover_public_address();
    }

    std::cout << "[" << id_ << "] ctor: connecting to signaling...\n";
    connect_to_signaling("77.110.104.122", 9000);
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
        public_port_ = listen_port_;
        std::cout << "[" << id_ << "] ✅ STUN SUCCESS: UDP mapped address: " << result.public_ip << ":"
                  << result.public_port << "\n";
        std::cout << "[" << id_ << "] advertising TCP address: " << public_ip_ << ":" << public_port_ << "\n";
        // отправляем свой публичный адрес на сигналинг
        signal_send_line("PUBLIC " + public_ip_ + ":" +
                         std::to_string(public_port_));
        // Рассылаем свой публичный адрес другим пирам
        for (auto& conn : connections_) {
            if (conn) {
                conn->send_line("PUBLIC " + public_ip_ + ":" + std::to_string(public_port_));
            }
        }
    } else {
        std::cerr << "[" << id_ << "] ❌ STUN FAILED: " << result.error_message << "\n";
        std::cerr << "[" << id_ << "] Public address is unknown; not advertising 0.0.0.0\n";
        public_ip_.clear();
        public_port_ = 0;
    }
}

bool Peer::add_known_peer(const tcp::endpoint& ep) {
    auto host = ep.address().to_string();
    auto port = static_cast<uint16_t>(ep.port());
    return add_known_peer(host, port);
}

bool Peer::add_known_peer(const std::string& host, uint16_t port) {
    if (is_self_peer(host, port)) {
        std::cout << "[" << id_ << "] ignoring self peer: " << host << ":" << port << "\n";
        return false;
    }

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
    if (is_self_peer(host, port)) {
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

bool Peer::is_self_peer(const std::string& host, uint16_t port) const {
    if (port != listen_port_) {
        return false;
    }

    if (!public_ip_.empty() && host == public_ip_) {
        return true;
    }

    boost::system::error_code ec;
    auto address = boost::asio::ip::make_address(host, ec);
    if (ec) {
        return host == "localhost";
    }

    return address.is_loopback() || address.is_unspecified();
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
void Peer::connect_to_signaling(const std::string& host, uint16_t port) {
    std::cout << "[" << id_ << "] connect_to_signaling(" << host << ":" << port << ")\n";
    signal_sock_ = std::make_shared<tcp::socket>(ctx_);
    auto resolver = std::make_shared<tcp::resolver>(ctx_);

    resolver->async_resolve(
        host, std::to_string(port),
        [this, resolver](boost::system::error_code ec, tcp::resolver::results_type res) {
            if (ec) {
                std::cerr << "[" << id_ << "] signaling resolve error: "
                        << ec.message() << "\n";
                return;
            }

            boost::asio::async_connect(
                *signal_sock_, res,
                [this](boost::system::error_code ec2, const tcp::endpoint& ep) {
                    if (ec2) {
                        std::cerr << "[" << id_ << "] signaling connect error: "
                                << ec2.message() << "\n";
                        return;
                    }

                    std::cout << "[" << id_ << "] connected to signaling "
                            << ep << "\n";

                    signal_send_line("ID " + id_);

                    if (!public_ip_.empty()) {
                        signal_send_line("PUBLIC " + public_ip_ + ":" +
                                        std::to_string(public_port_));
                    }

                    signal_do_read();
                });
        });

}

void Peer::signal_send_line(const std::string& line) {
    if (!signal_sock_ || !signal_sock_->is_open()) return;
    std::cout << "[" << id_ << "] signaling SEND: " << line << "\n";

    boost::asio::post(signal_sock_->get_executor(), [this, line]() {
        if (!signal_sock_ || !signal_sock_->is_open()) {
            return;
        }

        signal_write_queue_.push_back(line + "\n");
        if (!signal_writing_) {
            signal_writing_ = true;
            signal_do_write();
        }
    });
}

void Peer::signal_do_write() {
    if (!signal_sock_ || !signal_sock_->is_open()) {
        signal_writing_ = false;
        signal_write_queue_.clear();
        return;
    }

    if (signal_write_queue_.empty()) {
        signal_writing_ = false;
        return;
    }

    boost::asio::async_write(
        *signal_sock_, boost::asio::buffer(signal_write_queue_.front()),
        [this](boost::system::error_code ec, std::size_t) {
            if (ec) {
                std::cerr << "[" << id_ << "] signaling write error: "
                          << ec.message() << "\n";
                signal_writing_ = false;
                signal_write_queue_.clear();
                if (signal_sock_) {
                    signal_sock_->close();
                }
                return;
            }

            signal_write_queue_.pop_front();
            signal_do_write();
        });
}

void Peer::signal_do_read() {
    if (!signal_sock_ || !signal_sock_->is_open()) return;

    boost::asio::async_read_until(
        *signal_sock_, signal_buf_, '\n',
        [this](boost::system::error_code ec, std::size_t) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    std::cerr << "[" << id_ << "] signaling read error: "
                              << ec.message() << "\n";
                }
                return;
            }

            std::istream is(&signal_buf_);
            std::string line;
            std::getline(is, line);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) {
                handle_signal_line(line);
            }

            signal_do_read();
        });
}

void Peer::handle_signal_line(const std::string& line) {
    std::cout << "[" << id_ << "] signaling got: " << line << "\n";

    if (line.rfind("PEER_PUBLIC", 0) == 0) {
        std::istringstream iss(line);
        std::string cmd, pid, addr;
        iss >> cmd >> pid >> addr;

        auto pos = addr.find(':');
        if (pos == std::string::npos) return;

        std::string host = addr.substr(0, pos);
        std::string port_str = addr.substr(pos + 1);

        try {
            uint16_t port = static_cast<uint16_t>(std::stoi(port_str));

            // Не подключаемся к себе
            if (pid != id_) {
                if (add_known_peer(host, port)) {
                    maybe_connect_to_peer(host, port);
                }
            }
        } catch (...) {
            std::cerr << "[" << id_ << "] signaling parse error: "
                      << addr << "\n";
        }
    }
}
