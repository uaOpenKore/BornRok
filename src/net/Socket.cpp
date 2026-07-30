#include "net/Socket.hpp"

#include <cstring>
#include <string>

#include "core/Log.hpp"

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <cerrno>
#endif

namespace uaro::net {

namespace {
#if defined(_WIN32)
int g_wsa_refs = 0;
int os_err() { return WSAGetLastError(); }
bool would_block() {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
// A non-blocking connect() that has not completed yet.
bool connect_in_progress() {
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY;
}
void set_nonblocking(SOCKET fd) {
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
}
void close_fd(SOCKET fd) { closesocket(fd); }
#else
int os_err() { return errno; }
bool would_block() { return errno == EWOULDBLOCK || errno == EAGAIN; }
// A non-blocking connect() reports EINPROGRESS (not EWOULDBLOCK) while pending.
bool connect_in_progress() { return errno == EINPROGRESS || errno == EALREADY; }
void set_nonblocking(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}
void close_fd(int fd) { ::close(fd); }
#endif
} // namespace

void Socket::globalInit() {
#if defined(_WIN32)
    if (g_wsa_refs++ == 0) {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
#endif
}

void Socket::globalShutdown() {
#if defined(_WIN32)
    if (g_wsa_refs > 0 && --g_wsa_refs == 0) WSACleanup();
#endif
}

Socket::~Socket() { close(); }

Socket::Socket(Socket&& o) noexcept : fd_(o.fd_), state_(o.state_) {
    o.fd_ = kInvalid;
    o.state_ = State::Idle;
}

Socket& Socket::operator=(Socket&& o) noexcept {
    if (this != &o) {
        close();
        fd_ = o.fd_;
        state_ = o.state_;
        o.fd_ = kInvalid;
        o.state_ = State::Idle;
    }
    return *this;
}

bool Socket::connect(const std::string& host, u16 port) {
    close();
    state_ = State::Idle;

    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // IPv4 (RO servers are v4)
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    const std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        log::error("net: cannot resolve host '{}'", host);
        state_ = State::Failed;
        return false;
    }

    fd_t fd = static_cast<fd_t>(socket(res->ai_family, res->ai_socktype, res->ai_protocol));
    if (fd == kInvalid) {
        freeaddrinfo(res);
        state_ = State::Failed;
        return false;
    }
    set_nonblocking(fd);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));

    int rc = ::connect(fd, res->ai_addr, static_cast<int>(res->ai_addrlen));
    freeaddrinfo(res);

    if (rc == 0) {
        fd_ = fd;
        state_ = State::Connected;  // immediate (common for loopback)
        return true;
    }
    if (connect_in_progress()) {
        fd_ = fd;
        state_ = State::Connecting;  // completion observed via poll()
        return true;
    }
    close_fd(fd);
    state_ = State::Failed;
    return false;
}

Socket::State Socket::poll() {
    if (state_ != State::Connecting) return state_;

    fd_set wset, eset;
    FD_ZERO(&wset);
    FD_ZERO(&eset);
    FD_SET(static_cast<int>(fd_), &wset);
    FD_SET(static_cast<int>(fd_), &eset);
    timeval tv{0, 0};  // non-blocking probe
    int n = select(static_cast<int>(fd_) + 1, nullptr, &wset, &eset, &tv);
    if (n <= 0) return state_;  // still pending (or transient error)

    if (FD_ISSET(static_cast<int>(fd_), &eset)) {
        state_ = State::Failed;
        return state_;
    }
    if (FD_ISSET(static_cast<int>(fd_), &wset)) {
        int err = 0;
        socklen_t len = sizeof(err);
        getsockopt(static_cast<int>(fd_), SOL_SOCKET, SO_ERROR,
                   reinterpret_cast<char*>(&err), &len);
        state_ = (err == 0) ? State::Connected : State::Failed;
    }
    return state_;
}

int Socket::send(const u8* data, usize len) {
    if (fd_ == kInvalid || state_ != State::Connected) return -1;
#if defined(_WIN32)
    int n = ::send(fd_, reinterpret_cast<const char*>(data), static_cast<int>(len), 0);
#else
    ssize_t n = ::send(fd_, data, len, MSG_NOSIGNAL);
#endif
    if (n > 0) return static_cast<int>(n);
    if (n == 0) return 0;
    if (would_block()) return 0;
    lastErr_ = os_err();
    state_ = State::Failed;
    return -1;
}

int Socket::recv(u8* dst, usize cap) {
    if (fd_ == kInvalid || state_ != State::Connected) return -1;
#if defined(_WIN32)
    int n = ::recv(fd_, reinterpret_cast<char*>(dst), static_cast<int>(cap), 0);
#else
    ssize_t n = ::recv(fd_, dst, cap, 0);
#endif
    if (n > 0) return static_cast<int>(n);
    if (n == 0) {  // orderly shutdown by peer (server sent FIN)
        lastErr_ = 0;
        state_ = State::Closed;
        return -1;
    }
    if (would_block()) return 0;
    lastErr_ = os_err();
    state_ = State::Failed;
    return -1;
}

void Socket::close() {
    if (fd_ != kInvalid) {
        close_fd(fd_);
        fd_ = kInvalid;
    }
}

} // namespace uaro::net
