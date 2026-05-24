// stun_client.cpp
#define BOOST_ASIO_DISABLEAwaitable
#include <chrono>
#include <utility>
#include <iostream>  // для логов

#include "stun_client.hpp"
#include <cstring>

StunClient::StunClient(boost::asio::io_context& ctx)
    : ctx_(ctx),
      socket_(ctx, udp::v4()),
      server_endpoint_(udp::resolver(ctx)
                           .resolve("77.110.104.122", "3478")
                           .begin()->endpoint()),
      timeout_timer_(ctx)
{
    std::cout << "[STUN] Using server: " << server_endpoint_ << "\n";
}

void StunClient::get_public_address(std::function<void(Result)> callback) {
    using namespace std::chrono_literals;

    callback_ = std::move(callback);
    completed_ = false;

    timeout_timer_.expires_after(5s);
    timeout_timer_.async_wait([this](boost::system::error_code ec) {
        if (ec || completed_) {
            return;
        }

        Result res;
        res.success = false;
        res.error_message = "STUN timeout";
        std::cerr << "[STUN] Timeout waiting for response\n";
        socket_.cancel();
        finish(res);
    });

    // Логируем начало запроса
    std::cout << "[STUN] Sending request to " << server_endpoint_ << "\n";

    request_ = {
        0x00, 0x01, 0x00, 0x00,  // Type: Binding Request, Length: 0
        0x21, 0x12, 0xA4, 0x42,  // Magic Cookie
        0x00, 0x00, 0x00, 0x00,  // Transaction ID (random)
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };

    socket_.async_send_to(
        boost::asio::buffer(request_),
        server_endpoint_,
        [this](const boost::system::error_code& ec, std::size_t /*bytes*/) {
            if (ec) {
                std::cerr << "[STUN] Send error: " << ec.message() << "\n";
                Result res;
                res.success = false;
                res.error_message = "Send error: " + ec.message();
                finish(res);
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
    if (completed_) {
        return;
    }

    Result res;
    res.success = false;

    if (ec) {
        if (ec == boost::asio::error::operation_aborted && completed_) {
            return;
        }
        std::cerr << "[STUN] Receive error: " << ec.message() << "\n";
        res.error_message = "Receive error: " + ec.message();
        finish(res);
        return;
    }

    if (bytes < 20) {
        std::cerr << "[STUN] Response too short: " << bytes << " bytes\n";
        res.error_message = "Response too short: " + std::to_string(bytes) + " bytes";
        finish(res);
        return;
    }
    std::cout << "[STUN] Raw response (" << bytes << " bytes):\n";
    for (size_t i = 0; i < bytes; ++i) {
        printf("%02X ", static_cast<unsigned char>(buffer_[i]));
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    // Проверяем заголовок
    if (buffer_[0] != 0x01 || buffer_[1] != 0x01) { // Binding Success Response
        std::cerr << "[STUN] Not a success response: " << std::hex << (int)buffer_[0] << " " << (int)buffer_[1] << "\n";
        res.error_message = "Not a success response";
        finish(res);
        return;
    }

    std::cout << "[STUN] Got valid response, parsing...\n";

    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(buffer_.data()) + 20; // после заголовка
    const uint8_t* end = ptr + bytes - 20;
    bool parsed_mapped_address = false;

    while (ptr < end && !parsed_mapped_address) {
        if (end - ptr < 4) break;

        uint16_t attr_type = (static_cast<uint16_t>(ptr[0]) << 8) | ptr[1];
        uint16_t attr_len  = (static_cast<uint16_t>(ptr[2]) << 8) | ptr[3];
        if (end - ptr < 4 + attr_len) {
            std::cerr << "[STUN] Attribute length exceeds response size\n";
            break;
        }

        if ((attr_type == 0x0020 || attr_type == 0x8020) && attr_len >= 8) {
            std::cout << "[STUN] Found XOR-MAPPED-ADDRESS attribute\n";

            const uint8_t* attr = ptr;

            // 0:type_hi,1:type_lo,2:len_hi,3:len_lo,4:0,5:family
            uint8_t family = attr[5];
            if (family != 0x01) {
                std::cerr << "[STUN] Not IPv4 family: " << (int)family << "\n";
            } else {
                uint16_t xport = (static_cast<uint16_t>(attr[6]) << 8) | attr[7];
                uint32_t xip   = (static_cast<uint32_t>(attr[8])  << 24) |
                                (static_cast<uint32_t>(attr[9])  << 16) |
                                (static_cast<uint32_t>(attr[10]) << 8)  |
                                (static_cast<uint32_t>(attr[11]));

                uint32_t magic_cookie = 0x2112A442;
                uint16_t port = xport ^ static_cast<uint16_t>((magic_cookie >> 16) & 0xFFFF);
                uint32_t ip   = xip   ^ magic_cookie;

                res.public_ip   = boost::asio::ip::address_v4(ip).to_string();
                res.public_port = port;
                res.success     = true;

                std::cout << "[STUN] Parsed public: " << res.public_ip
                        << ":" << res.public_port << "\n";
            }

            parsed_mapped_address = true;
        }

        // выравнивание длины до 4 байт
        std::size_t padded = (attr_len + 3) & ~3u;
        ptr += 4 + padded;
    }

    if (!res.success) {
        std::cerr << "[STUN] No Xor-Mapped-Address found in response\n";
        res.error_message = "No Xor-Mapped-Address found";
    }

    finish(res);
}

void StunClient::finish(Result result) {
    if (completed_) {
        return;
    }

    completed_ = true;
    timeout_timer_.cancel();
    callback_(std::move(result));
}
