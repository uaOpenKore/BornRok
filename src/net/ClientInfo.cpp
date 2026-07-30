#include "net/ClientInfo.hpp"

#include <cctype>
#include <cstdlib>

namespace uaro::net {

namespace {

std::string trim(const std::string& s) {
    usize a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Text between the first <tag>...</tag> inside [block], or "" if absent.
std::string tagText(const std::string& block, const std::string& tag) {
    const std::string open = "<" + tag + ">";
    const std::string close = "</" + tag + ">";
    usize a = block.find(open);
    if (a == std::string::npos) return {};
    a += open.size();
    usize b = block.find(close, a);
    if (b == std::string::npos) return {};
    return trim(block.substr(a, b - a));
}

} // namespace

ClientInfo parseClientInfo(const std::string& xml) {
    ClientInfo info;
    info.desc = tagText(xml, "desc");

    usize pos = 0;
    for (;;) {
        usize open = xml.find("<connection", pos);
        if (open == std::string::npos) break;
        usize gt = xml.find('>', open);
        if (gt == std::string::npos) break;
        usize close = xml.find("</connection>", gt);
        if (close == std::string::npos) break;

        const std::string block = xml.substr(gt + 1, close - gt - 1);
        ServerConnection c;
        c.display = tagText(block, "display");
        c.address = tagText(block, "address");
        c.port = static_cast<u16>(std::atoi(tagText(block, "port").c_str()));
        c.version = static_cast<u32>(std::atol(tagText(block, "version").c_str()));
        c.langtype = static_cast<u32>(std::atol(tagText(block, "langtype").c_str()));
        if (!c.address.empty() && c.port != 0) info.connections.push_back(std::move(c));

        pos = close + 1;
    }
    return info;
}

} // namespace uaro::net
