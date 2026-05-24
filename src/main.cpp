#include "p2p/node.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {

std::atomic_bool stop_requested = false;

void handle_signal(int) {
    stop_requested = true;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc != 2 && argc != 4) {
            std::cerr << "Usage:\n"
                      << "  " << argv[0] << " <listen_port>\n"
                      << "  " << argv[0] << " <listen_port> <bootstrap_host> <bootstrap_port>\n";
            return 1;
        }

        uint16_t listen_port = static_cast<uint16_t>(std::stoi(argv[1]));

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        p2p::P2PNode node({listen_port});
        node.start();

        if (argc == 4) {
            std::string host = argv[2];
            uint16_t bport = static_cast<uint16_t>(std::stoi(argv[3]));

            node.connect_to(host, bport);
        }

        while (!stop_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        node.stop();
    }
    catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
