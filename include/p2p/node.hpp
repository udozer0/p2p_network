#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace p2p {

struct NodeConfig {
    uint16_t listen_port = 0;
    std::string id;
};

class P2PNode {
public:
    explicit P2PNode(NodeConfig config);
    ~P2PNode();

    P2PNode(const P2PNode&) = delete;
    P2PNode& operator=(const P2PNode&) = delete;

    void start();
    void stop();
    void connect_to(const std::string& host, uint16_t port);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace p2p
