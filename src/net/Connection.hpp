#pragma once
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "net/Socket.hpp"

namespace uaro::net {

// Buffered TCP connection: owns a non-blocking Socket plus transmit/receive byte
// queues. Call update() once per frame to advance the connect, flush queued sends
// and drain incoming bytes into rx(). Packet framing is the caller's job — it
// inspects rx() and calls consume() once a whole packet has been decoded.
class Connection {
public:
    void connect(const std::string& host, u16 port);
    void disconnect();

    // Advance the socket: poll connect state, flush tx, read into rx.
    void update();

    Socket::State state() const { return sock_.state(); }
    int sockError() const { return sock_.lastError(); }
    bool connecting() const { return sock_.state() == Socket::State::Connecting; }
    bool connected() const { return sock_.state() == Socket::State::Connected; }
    bool closed() const {
        return sock_.state() == Socket::State::Closed || sock_.state() == Socket::State::Failed;
    }

    // Queue bytes for sending (sent during update() once connected).
    void send(const u8* p, usize n) { tx_.insert(tx_.end(), p, p + n); }
    void send(const std::vector<u8>& v) { send(v.data(), v.size()); }

    std::vector<u8>& rx() { return rx_; }
    const std::vector<u8>& rx() const { return rx_; }
    // Drop `n` already-consumed bytes from the front of the receive buffer.
    void consume(usize n);

private:
    Socket sock_;
    std::vector<u8> tx_;
    std::vector<u8> rx_;
};

} // namespace uaro::net
