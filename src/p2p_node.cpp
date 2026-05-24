#include "p2p/node.hpp"

#include "peer.hpp"

#include <boost/asio.hpp>

#include <stdexcept>
#include <thread>
#include <utility>

namespace p2p {

struct P2PNode::Impl {
    explicit Impl(NodeConfig node_config)
        : config(std::move(node_config))
    {
        if (config.listen_port == 0) {
            throw std::invalid_argument("listen_port must be non-zero");
        }

        if (config.id.empty()) {
            config.id = "peer_" + std::to_string(config.listen_port);
        }
    }

    NodeConfig config;
    boost::asio::io_context ctx;
    std::unique_ptr<Peer> peer;
    std::thread io_thread;
    bool started = false;
};

P2PNode::P2PNode(NodeConfig config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{
}

P2PNode::~P2PNode() {
    stop();
}

void P2PNode::start() {
    if (impl_->started) {
        return;
    }

    impl_->ctx.restart();
    impl_->peer = std::make_unique<Peer>(impl_->ctx, impl_->config.listen_port, impl_->config.id);
    impl_->peer->start();
    impl_->started = true;
    impl_->io_thread = std::thread([this]() {
        impl_->ctx.run();
    });
}

void P2PNode::stop() {
    if (!impl_->started) {
        return;
    }

    impl_->ctx.stop();
    if (impl_->io_thread.joinable()) {
        impl_->io_thread.join();
    }
    impl_->peer.reset();
    impl_->started = false;
}

void P2PNode::connect_to(const std::string& host, uint16_t port) {
    if (!impl_->started) {
        throw std::logic_error("P2PNode must be started before connect_to()");
    }

    boost::asio::post(impl_->ctx, [this, host, port]() {
        if (impl_->peer) {
            impl_->peer->connect_to(host, port);
        }
    });
}

} // namespace p2p
