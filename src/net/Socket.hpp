#pragma once
#include <string>

#include "core/Types.hpp"

namespace uaro::net {

// Cross-platform non-blocking TCP client socket (Winsock on Windows, BSD sockets
// elsewhere). Thin RAII wrapper used by Connection; the DNS resolve in connect()
// is synchronous (brief), the rest of the lifecycle is non-blocking and polled.
class Socket {
public:
    enum class State { Idle, Connecting, Connected, Closed, Failed };

    Socket() = default;
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& o) noexcept;
    Socket& operator=(Socket&& o) noexcept;

    // Resolve `host` and begin a non-blocking connect to `port`. Returns false on
    // an immediate failure (bad host / socket creation); otherwise progress is
    // observed via poll().
    bool connect(const std::string& host, u16 port);

    // Drive a pending connect to completion; returns the current state.
    State poll();
    State state() const { return state_; }
    // OS error code captured when the connection last went to Failed (errno / WSAGetLastError).
    // 0 means an orderly peer shutdown (recv==0 -> Closed) rather than a socket error.
    int lastError() const { return lastErr_; }

    // Non-blocking I/O. Return the byte count transferred, 0 on would-block, or -1
    // on a closed/failed connection (state then becomes Closed/Failed).
    int send(const u8* data, usize len);
    int recv(u8* dst, usize cap);

    void close();
    bool valid() const { return fd_ != kInvalid; }

    // Process-wide socket subsystem init/teardown (WSAStartup on Windows, a no-op
    // elsewhere). Reference-counted; safe to call in pairs repeatedly.
    static void globalInit();
    static void globalShutdown();

private:
#if defined(_WIN32)
    using fd_t = unsigned long long;  // SOCKET (UINT_PTR)
    static constexpr fd_t kInvalid = ~0ull;
#else
    using fd_t = int;
    static constexpr fd_t kInvalid = static_cast<fd_t>(-1);
#endif
    fd_t fd_ = kInvalid;
    State state_ = State::Idle;
    int lastErr_ = 0;
};

} // namespace uaro::net
