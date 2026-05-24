// stun_client.hpp
#pragma once

#include <boost/asio.hpp>
#include <array>
#include <string>
#include <memory>

class StunClient {
public:
    using tcp = boost::asio::ip::tcp;
    using udp = boost::asio::ip::udp;

    struct Result {
        std::string public_ip;
        uint16_t public_port;
        bool success = false;
        std::string error_message;
    };

    StunClient(boost::asio::io_context& ctx);

    void get_public_address(std::function<void(Result)> callback);

private:
    void send_stun_request();
    void handle_response(const boost::system::error_code& ec, std::size_t bytes_transferred);
    void finish(Result result);

    boost::asio::io_context& ctx_;
    udp::socket socket_;
    udp::endpoint server_endpoint_;
    boost::asio::steady_timer timeout_timer_;
    std::array<unsigned char, 20> request_;
    std::array<char, 1500> buffer_;
    std::function<void(Result)> callback_;
    bool completed_ = false;
};
