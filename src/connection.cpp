#include "connection.hpp"
#include <iostream>

Connection::Connection(tcp::socket socket, MessageHandler handler)
    : socket_(std::move(socket)),
      handler_(std::move(handler))
{
}

void Connection::start() {
    do_read();
}

void Connection::send_line(const std::string& line) {
    auto self = shared_from_this();
    boost::asio::post(socket_.get_executor(), [this, self, line]() {
        bool write_in_progress = writing_;
        // одна строка за раз, для простоты
        write_queue_ = line + "\n";
        if (!write_in_progress) {
            writing_ = true;
            do_write();
        }
    });
}

void Connection::do_read() {
    auto self = shared_from_this();
    boost::asio::async_read_until(
        socket_, read_buf_, '\n',
        [this, self](boost::system::error_code ec, std::size_t /*bytes*/) {
            if (!ec) {
                std::istream is(&read_buf_);
                std::string line;
                std::getline(is, line);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    handler_(line, self);
                }
                // читаем дальше
                do_read();
            } else {
                std::cerr << "Read error: " << ec.message() << "\n";
            }
        }
    );
}

void Connection::do_write() {
    auto self = shared_from_this();
    boost::asio::async_write(
        socket_, boost::asio::buffer(write_queue_),
        [this, self](boost::system::error_code ec, std::size_t /*bytes*/) {
            if (ec) {
                std::cerr << "Write error: " << ec.message() << "\n";
            }
            writing_ = false;
        }
    );
}
