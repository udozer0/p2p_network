#pragma once
#include <utility>
#include <boost/asio.hpp>
#include <memory>
#include <functional>
#include <string>
#include <deque>

class Connection : public std::enable_shared_from_this<Connection> {
public:
    using tcp = boost::asio::ip::tcp;
    using MessageHandler = std::function<void(const std::string&, std::shared_ptr<Connection>)>;

    Connection(tcp::socket socket, MessageHandler handler);

    void start();                      // запустить асинхронное чтение
    void send_line(const std::string& line); // асинхронно отправить строку с '\n'

    tcp::socket& socket() { return socket_; }

private:
    void do_read();
    void do_write();

    tcp::socket socket_;
    boost::asio::streambuf read_buf_;

    std::deque<std::string> write_queue_; // очередь строк на отправку
    bool writing_ = false;

    MessageHandler handler_;
};
