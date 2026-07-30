#include "net/ClientInfo.hpp"

#include <string>

#include "microtest.hpp"

using namespace uaro;

// The real uaRO sclientinfo.xml shape: multiple <connection> blocks, EUC-KR
// header, whitespace-indented tags.
TEST_CASE(clientinfo_parses_connections) {
    const std::string xml =
        "<?xml version=\"1.0\" encoding=\"euc-kr\" ?>\n"
        "<clientinfo>\n"
        "  <desc>uaRO</desc>\n"
        "  <connection>\n"
        "    <display>uaRO</display>\n"
        "    <address>play.bornrok.com</address>\n"
        "    <port>16900</port>\n"
        "    <version>20</version>\n"
        "    <langtype>14</langtype>\n"
        "  </connection>\n"
        "  <connection>\n"
        "    <display>uaRO 11.3 - uAthena - x64</display>\n"
        "    <address>play.bornrok.com</address>\n"
        "    <port>6900</port>\n"
        "    <version>20</version>\n"
        "    <langtype>14</langtype>\n"
        "  </connection>\n"
        "</clientinfo>\n";

    const net::ClientInfo info = net::parseClientInfo(xml);
    CHECK(info.desc == "uaRO");
    CHECK_EQ(info.connections.size(), 2u);

    CHECK(info.connections[0].display == "uaRO");
    CHECK(info.connections[0].address == "play.bornrok.com");
    CHECK_EQ(info.connections[0].port, 16900);
    CHECK_EQ(info.connections[0].version, 20u);
    CHECK_EQ(info.connections[0].langtype, 14u);

    CHECK(info.connections[1].display == "uaRO 11.3 - uAthena - x64");
    CHECK_EQ(info.connections[1].port, 6900);
}

TEST_CASE(clientinfo_skips_incomplete) {
    const std::string xml =
        "<clientinfo>"
        "<connection><display>broken</display></connection>"  // no address/port -> dropped
        "<connection><address>1.2.3.4</address><port>5000</port></connection>"
        "</clientinfo>";
    const net::ClientInfo info = net::parseClientInfo(xml);
    CHECK_EQ(info.connections.size(), 1u);
    CHECK(info.connections[0].address == "1.2.3.4");
    CHECK_EQ(info.connections[0].port, 5000);
}

TEST_CASE(clientinfo_empty) {
    const net::ClientInfo info = net::parseClientInfo("<clientinfo></clientinfo>");
    CHECK(info.empty());
}
