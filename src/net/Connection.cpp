#include "net/Connection.hpp"

namespace uaro::net {

void Connection::connect(const std::string& host, u16 port) {
    tx_.clear();
    rx_.clear();
    sock_.connect(host, port);
}

void Connection::disconnect() {
    sock_.close();
    tx_.clear();
    rx_.clear();
}

void Connection::update() {
    Socket::State st = sock_.poll();
    if (st != Socket::State::Connected) return;

    // Flush as much of the transmit queue as the socket accepts.
    while (!tx_.empty()) {
        int n = sock_.send(tx_.data(), tx_.size());
        if (n <= 0) break;  // would-block (0) or error (-1, state updated)
        tx_.erase(tx_.begin(), tx_.begin() + n);
    }

    // Drain incoming bytes into rx_.
    u8 buf[4096];
    for (;;) {
        int n = sock_.recv(buf, sizeof(buf));
        if (n <= 0) break;  // no more data, would-block, or closed
        rx_.insert(rx_.end(), buf, buf + n);
    }
}

void Connection::consume(usize n) {
    if (n >= rx_.size())
        rx_.clear();
    else
        rx_.erase(rx_.begin(), rx_.begin() + n);
}

} // namespace uaro::net
