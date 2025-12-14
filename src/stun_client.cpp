// stun_client.cpp
#define BOOST_ASIO_DISABLEAwaitable
#include <utility>  // для std::exchange (на случай, если он понадобится)

#include "stun_client.hpp"
#include <iostream>
#include <cstring>

StunClient::StunClient(boost::asio::io_context& ctx)
    : ctx_(ctx),
      socket_(ctx, udp::v4()),
      server_endpoint_(udp::resolver(ctx).resolve("stunserver.org", "3478").begin()->endpoint())
{
}

void StunClient::get_public_address(std::function<void(Result)> callback) {
    callback_ = std::move(callback);

    unsigned char request[] = {
        0x00, 0x01, 0x00, 0x00,  // Type: Binding Request, Length: 0
        0x21, 0x12, 0xA4, 0x42,  // Magic Cookie
        0x00, 0x00, 0x00, 0x00,  // Transaction ID (random)
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    socket_.async_send_to(
        boost::asio::buffer(request, sizeof(request)),
        server_endpoint_,
        [this](const boost::system::error_code& ec, std::size_t /*bytes*/) {
            if (ec) {
                Result res;
                res.success = false;
                callback_(res);
                return;
            }

            socket_.async_receive_from(
                boost::asio::buffer(buffer_),
                server_endpoint_,
                [this](const boost::system::error_code& ec, std::size_t bytes) {
                    handle_response(ec, bytes);
                }
            );
        }
    );
}

void StunClient::handle_response(const boost::system::error_code& ec, std::size_t bytes) {
    Result res;
    res.success = false;

    if (ec) {
        std::cerr << "[STUN] Error: " << ec.message() << "\n";
        callback_(res);
        return;
    }

    if (bytes < 20) {
        std::cerr << "[STUN] Response too short: " << bytes << " bytes\n";
        callback_(res);
        return;
    }

    const char* ptr = buffer_.data() + 20;
    const char* end = ptr + bytes - 20;

    while (ptr < end) {
        if (end - ptr < 4) break;

        uint16_t attr_type = (static_cast<uint16_t>(ptr[0]) << 8) | ptr[1];
        uint16_t attr_len = (static_cast<uint16_t>(ptr[2]) << 8) | ptr[3];

        if (attr_type == 0x0020 && attr_len >= 8) {
            uint8_t family = ptr[4];
            if (family != 0x01) break;

            uint16_t port_xor = (static_cast<uint16_t>(ptr[5]) << 8) | ptr[6];
            uint32_t ip_xor = (static_cast<uint32_t>(ptr[7]) << 24) |
                              (static_cast<uint32_t>(ptr[8]) << 16) |
                              (static_cast<uint32_t>(ptr[9]) << 8) |
                              ptr[10];

            uint32_t magic_cookie = 0x2112A442;
            uint16_t port = port_xor ^ ((magic_cookie >> 16) & 0xFFFF);
            uint32_t ip = ip_xor ^ magic_cookie;

            res.public_ip = boost::asio::ip::address_v4(ip).to_string();
            res.public_port = port;
            res.success = true;
            break;
        }

        ptr += 4 + attr_len;
    }

    callback_(res);
}