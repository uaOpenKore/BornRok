#pragma once
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro::net {

// One <connection> entry from (s)clientinfo.xml: a selectable server.
struct ServerConnection {
    std::string display;       // <display> shown in the server list
    std::string address;       // <address> host name or IP
    u16 port = 0;              // <port>
    u32 version = 0;           // <version> sent in the login packet
    u32 langtype = 0;          // <langtype>
};

// Parsed (s)clientinfo.xml. The original client reads this from the data folder
// to learn which login server(s) to offer. Encoding is typically EUC-KR, but the
// fields we need (address/port/version) are ASCII.
struct ClientInfo {
    std::string desc;
    std::vector<ServerConnection> connections;
    bool empty() const { return connections.empty(); }
};

// Scrape the <connection> blocks out of a clientinfo XML document. Tolerant of
// whitespace and the EUC-KR header; no full XML parser required.
ClientInfo parseClientInfo(const std::string& xml);

} // namespace uaro::net
