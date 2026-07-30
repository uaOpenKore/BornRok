#pragma once
// STUB — implemented in v4 (networking).
//
// Async TCP transport plus a versioned packet codec. Packet tables are derived
// from the OpenKore reference for this client (uaOpenKore/uOK*, e.g.
// tables/recvpackets-uaro.txt). Goal 3.3: an extensible packet format on top of
// the legacy layout. Built on core::ByteReader/ByteWriter.
#include "core/io/ByteBuffer.hpp"

namespace uaro::net {

// Planned interface:
//   class Connection { bool connect(host, port); void send(const Packet&); ... };
//   struct PacketHeader { u16 id; ... };
//   class PacketTable { usize sizeOf(u16 id) const; ... };

} // namespace uaro::net
