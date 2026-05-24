#include "p2p/node.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic_bool stop_requested = false;

void handle_signal(int) {
    stop_requested = true;
}

struct Options {
    uint16_t listen_port = 0;
    std::string id;
    std::string connect_host;
    uint16_t connect_port = 0;
    int run_for_seconds = 0;
};

void print_usage(const char* argv0) {
    std::cerr << "Usage:\n"
              << "  " << argv0 << " <listen_port>\n"
              << "  " << argv0 << " <listen_port> <bootstrap_host> <bootstrap_port>\n"
              << "  " << argv0 << " --listen <port> [--id <id>] [--connect <host> <port>] [--run-for <seconds>]\n";
}

uint16_t parse_port(const std::string& value) {
    int parsed = std::stoi(value);
    if (parsed <= 0 || parsed > 65535) {
        throw std::out_of_range("port must be in range 1..65535");
    }
    return static_cast<uint16_t>(parsed);
}

Options parse_args(int argc, char* argv[]) {
    Options options;

    if (argc == 2 || argc == 4) {
        options.listen_port = parse_port(argv[1]);
        if (argc == 4) {
            options.connect_host = argv[2];
            options.connect_port = parse_port(argv[3]);
        }
        return options;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--listen" && i + 1 < argc) {
            options.listen_port = parse_port(argv[++i]);
        } else if (arg == "--id" && i + 1 < argc) {
            options.id = argv[++i];
        } else if (arg == "--connect" && i + 2 < argc) {
            options.connect_host = argv[++i];
            options.connect_port = parse_port(argv[++i]);
        } else if (arg == "--run-for" && i + 1 < argc) {
            options.run_for_seconds = std::stoi(argv[++i]);
        } else {
            throw std::invalid_argument("unknown or incomplete argument: " + arg);
        }
    }

    if (options.listen_port == 0) {
        throw std::invalid_argument("--listen is required");
    }

    return options;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        std::cout.setf(std::ios::unitbuf);
        std::cerr.setf(std::ios::unitbuf);

        Options options = parse_args(argc, argv);

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        p2p::P2PNode node({options.listen_port, options.id});
        node.start();

        if (!options.connect_host.empty()) {
            node.connect_to(options.connect_host, options.connect_port);
        }

        auto started_at = std::chrono::steady_clock::now();
        while (!stop_requested) {
            if (options.run_for_seconds > 0
                && std::chrono::steady_clock::now() - started_at >= std::chrono::seconds(options.run_for_seconds)) {
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        node.stop();
    }
    catch (const std::exception& ex) {
        print_usage(argv[0]);
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
