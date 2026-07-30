// Real loopback test of the non-blocking Socket/Connection layer. POSIX-only
// (the sandbox/CI host); the Windows Winsock path is exercised on the desktop
// build. Single-threaded: a non-blocking listener is polled alongside the client
// in one bounded loop, so the test can never hang.
#include "microtest.hpp"

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "net/Connection.hpp"

using namespace uaro;

TEST_CASE(socket_loopback_roundtrip) {
    // Listener on 127.0.0.1:<ephemeral>, non-blocking.
    int ls = ::socket(AF_INET, SOCK_STREAM, 0);
    CHECK(ls >= 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    CHECK(::bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    CHECK(::listen(ls, 1) == 0);
    socklen_t alen = sizeof(addr);
    getsockname(ls, reinterpret_cast<sockaddr*>(&addr), &alen);
    const u16 port = ntohs(addr.sin_port);
    fcntl(ls, F_SETFL, fcntl(ls, F_GETFL, 0) | O_NONBLOCK);

    net::Socket::globalInit();
    net::Connection conn;
    conn.connect("127.0.0.1", port);

    int srv = -1;
    bool sentPing = false, gotPong = false, serverEcho = false;

    for (int i = 0; i < 2000 && !gotPong; ++i) {  // ~2s budget at 1ms/iter
        if (srv < 0) {
            srv = ::accept(ls, nullptr, nullptr);
            if (srv >= 0) fcntl(srv, F_SETFL, fcntl(srv, F_GETFL, 0) | O_NONBLOCK);
        }
        conn.update();
        if (conn.connected() && !sentPing) {
            const u8 ping[4] = {'p', 'i', 'n', 'g'};
            conn.send(ping, 4);
            sentPing = true;
        }
        if (srv >= 0 && !serverEcho) {
            char buf[16];
            ssize_t n = ::recv(srv, buf, sizeof(buf), 0);
            if (n == 4 && std::memcmp(buf, "ping", 4) == 0) {
                ::send(srv, "pong", 4, 0);
                serverEcho = true;
            }
        }
        if (serverEcho && conn.rx().size() >= 4) {
            CHECK(std::memcmp(conn.rx().data(), "pong", 4) == 0);
            gotPong = true;
        }
        usleep(1000);
    }

    CHECK(conn.connected());
    CHECK(sentPing);
    CHECK(serverEcho);
    CHECK(gotPong);

    conn.disconnect();
    if (srv >= 0) ::close(srv);
    ::close(ls);
    net::Socket::globalShutdown();
}
#endif  // !_WIN32
