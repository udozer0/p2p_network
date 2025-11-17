#include "peer.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        if (argc != 2 && argc != 4) {
            std::cerr << "Usage:\n"
                      << "  " << argv[0] << " <listen_port>\n"
                      << "  " << argv[0] << " <listen_port> <bootstrap_host> <bootstrap_port>\n";
            return 1;
        }

        uint16_t listen_port = static_cast<uint16_t>(std::stoi(argv[1]));
        std::string id = "peer_" + std::to_string(listen_port);

        boost::asio::io_context ctx;
        Peer peer(ctx, listen_port, id);
        peer.start();

        if (argc == 4) {
            std::string host = argv[2];
            uint16_t bport = static_cast<uint16_t>(std::stoi(argv[3]));

            // чуть позже, чтобы accept успел стартануть
            boost::asio::post(ctx, [&peer, host, bport]() {
                peer.connect_to(host, bport);
            });
        }

        ctx.run();
    }
    catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
