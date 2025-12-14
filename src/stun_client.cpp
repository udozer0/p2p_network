// stun_client.cpp
#define BOOST_ASIO_DISABLEAwaitable
#include <utility>
#include <iostream>  // для логов

#include "stun_client.hpp"
#include <cstring>

StunClient::StunClient(boost::asio::io_context& ctx)
    : ctx_(ctx),
      socket_(ctx, udp::v4()),
      server_endpoint_(udp::resolver(ctx).resolve("stunserver.org", "3478").begin()->endpoint())
{
    std::cout << "[STUN] Using server: stunserver.org:3478\n";
}

void StunClient::get_public_address(std::function<void(Result)> callback) {
    callback_ = std::move(callback);

    // Логируем начало запроса
    std::cout << "[STUN] Sending request to " << server_endpoint_ << "\n";

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
                std::cerr << "[STUN] Send error: " << ec.message() << "\n";
                Result res;
                res.success = false;
                res.error_message = "Send error: " + ec.message();
                callback_(res);
                return;
            }

            std::cout << "[STUN] Request sent, waiting for response...\n";

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
        std::cerr << "[STUN] Receive error: " << ec.message() << "\n";
        res.error_message = "Receive error: " + ec.message();
        callback_(res);
        return;
    }

    if (bytes < 20) {
        std::cerr << "[STUN] Response too short: " << bytes << " bytes\n";
        res.error_message = "Response too short: " + std::to_string(bytes) + " bytes";
        callback_(res);
        return;
    }

    // Проверяем заголовок
    if (buffer_[0] != 0x01 || buffer_[1] != 0x01) { // Binding Success Response
        std::cerr << "[STUN] Not a success response: " << std::hex << (int)buffer_[0] << " " << (int)buffer_[1] << "\n";
        res.error_message = "Not a success response";
        callback_(res);
        return;
    }

    std::cout << "[STUN] Got valid response, parsing...\n";

    const char* ptr = buffer_.data() + 20; // после заголовка
    const char* end = ptr + bytes - 20;

    while (ptr < end) {
        if (end - ptr < 4) break;

        uint16_t attr_type = (static_cast<uint16_t>(ptr[0]) << 8) | ptr[1];
        uint16_t attr_len = (static_cast<uint16_t>(ptr[2]) << 8) | ptr[3];

        if (attr_type == 0x0020 && attr_len >= 8) { // Xor-Mapped-Address
            std::cout << "[STUN] Found Xor-Mapped-Address attribute\n";

            uint8_t family = ptr[4];
            if (family != 0x01) {
                std::cerr << "[STUN] Not IPv4 family: " << (int)family << "\n";
                continue;
            }

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

            std::cout << "[STUN] Public address: " << res.public_ip << ":" << res.public_port << "\n";
            break;
        }

        ptr += 4 + attr_len; // следующий атрибут
    }

    if (!res.success) {
        std::cerr << "[STUN] No Xor-Mapped-Address found in response\n";
        res.error_message = "No Xor-Mapped-Address found";
    }

    callback_(res);
}