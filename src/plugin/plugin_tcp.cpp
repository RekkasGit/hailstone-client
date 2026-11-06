#include "plugin/plugin_tcp.h"

#include <mutex>
#include <utility>

#include "platform/udp_socket.h"

namespace nmsc::plugin {

// Filled on resolve_async's thread, which owns it alone once the connection is gone.
struct PluginTcp::Lookup {
    std::mutex mutex;
    bool done = false;
    std::vector<platform::Endpoint> endpoints;
    std::string error;
};

std::unique_ptr<PluginTcp> PluginTcp::open(const std::string& address, std::uint16_t port, std::string& error) {
    std::unique_ptr<PluginTcp> t(new PluginTcp());
    t->address_ = address;
    if (auto ep = platform::numeric_endpoint(address, port)) {
        if (!t->socket_.connect(*ep, error)) return nullptr;
        return t;
    }
    // A name: getaddrinfo can wait out a slow name server for seconds, and the plugin calls this on the frame.
    auto lookup = std::make_shared<Lookup>();
    t->lookup_ = lookup;
    platform::resolve_async(address, port, [lookup](std::vector<platform::Endpoint> endpoints, std::string err) {
        std::lock_guard l(lookup->mutex);
        lookup->endpoints = std::move(endpoints);
        lookup->error = std::move(err);
        lookup->done = true;
    });
    return t;
}

PluginTcp::~PluginTcp() = default;

void PluginTcp::progress() {
    if (lookup_) {
        std::vector<platform::Endpoint> endpoints;
        {
            std::lock_guard l(lookup_->mutex);
            if (!lookup_->done) return;
            endpoints = std::move(lookup_->endpoints);
            error_ = std::move(lookup_->error);
        }
        lookup_.reset();
        bool started = false;
        for (const auto& ep : endpoints)
            if ((started = socket_.connect(ep, error_))) break;
        if (started) {
            error_.clear();
        } else {
            if (error_.empty()) error_ = "cannot resolve " + address_;
            pending_.clear();
            sent_ = 0;
            return;
        }
    }
    flush();
}

void PluginTcp::flush() {
    while (sent_ < pending_.size()) {
        const auto n = socket_.send(std::span<const std::byte>(pending_).subspan(sent_));
        if (!n) {
            pending_.clear();
            sent_ = 0;
            return;
        }
        if (*n == 0) break;
        sent_ += *n;
    }
    if (sent_ == pending_.size()) {
        pending_.clear();
        sent_ = 0;
    } else if (sent_ >= pending_.size() / 2) {
        // A link that is always a little behind never empties the queue: what it took goes, or the queue would keep
        // every byte ever sent. Half or more taken, so each byte is moved at most once on average.
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(sent_));
        sent_ = 0;
    }
}

int PluginTcp::state() {
    progress();
    if (lookup_) return 0;
    switch (socket_.state()) {
    case platform::TcpSocket::State::Connected: return 1;
    case platform::TcpSocket::State::Connecting: return 0;
    default: return -1;
    }
}

std::int64_t PluginTcp::send(std::span<const std::byte> data) {
    progress();
    if (!lookup_ && socket_.state() == platform::TcpSocket::State::Closed) return -1;
    if (pending_.size() - sent_ + data.size() > kPendingMax) {
        error_ = "the peer stopped reading";
        lookup_.reset();
        socket_.close();
        pending_.clear();
        sent_ = 0;
        return -1;
    }
    pending_.insert(pending_.end(), data.begin(), data.end());
    if (!lookup_) flush();
    return static_cast<std::int64_t>(data.size());
}

std::int64_t PluginTcp::recv(std::span<std::byte> out) {
    progress();
    if (lookup_) return 0;
    const auto n = socket_.recv(out);
    return n ? static_cast<std::int64_t>(*n) : -1;
}

std::string PluginTcp::error() const { return error_.empty() ? socket_.last_error() : error_; }

} // namespace nmsc::plugin
