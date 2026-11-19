#pragma once
#include "core/Types.hpp"
#include "net/Connection.hpp"

namespace uaro::net {

// Read cursor over a Connection's receive buffer for a packet pump: data()/size()/[] start at the
// current packet, consume(n) only moves the cursor, and the parsed bytes are dropped from the buffer
// once - on flush() or when the cursor goes out of scope. Erasing the front of the vector per packet
// (Connection::consume) made a big burst, like the map-entry area scan, quadratic in its size.
// flush() before anything that may replace the buffer (a reconnect), so the destructor can't eat the
// new connection's bytes.
class RxCursor {
public:
    explicit RxCursor(Connection& conn) : conn_(conn) {}
    ~RxCursor() { flush(); }
    RxCursor(const RxCursor&) = delete;
    RxCursor& operator=(const RxCursor&) = delete;

    const u8* data() const { return conn_.rx().data() + off_; }
    usize size() const { return conn_.rx().size() - off_; }
    u8 operator[](usize i) const { return conn_.rx()[off_ + i]; }

    void consume(usize n) { off_ += n; }
    void flush() {
        if (off_ > 0) conn_.consume(off_);
        off_ = 0;
    }

private:
    Connection& conn_;
    usize off_ = 0;
};

}  // namespace uaro::net
