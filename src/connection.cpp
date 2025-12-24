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
        write_queue_.push_back(line + "\n");

        if (!writing_) {
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
                do_read();
            } else {
                if (ec == boost::asio::error::eof) {
                    std::cerr << "Read error: End of file\n";
                } else if (ec != boost::asio::error::operation_aborted) {
                    std::cerr << "Read error: " << ec.message() << "\n";
                }
                // ВАЖНО: закрыть сокет
                socket_.close();
                // И НИЧЕГО БОЛЬШЕ НЕ ДЕЛАТЬ (не вызывать do_read)
            }
        }
    );
}


void Connection::do_write() {
    if (write_queue_.empty()) {
        writing_ = false;
        return;
    }

    auto self = shared_from_this();
    const std::string& front = write_queue_.front();

    boost::asio::async_write(
        socket_, boost::asio::buffer(front),
        [this, self](boost::system::error_code ec, std::size_t /*bytes*/) {
            if (ec) {
                if (ec != boost::asio::error::operation_aborted) {
                    std::cerr << "Write error: " << ec.message() << "\n";
                }
                writing_ = false;
                return;
            }

            write_queue_.pop_front();

            if (!write_queue_.empty()) {
                do_write();
            } else {
                writing_ = false;
            }
        }
    );
}
