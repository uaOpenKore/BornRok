#include "patcher/GDriveUrl.hpp"

#include "core/Types.hpp"

namespace uaro {
namespace {

// Read an id token (letters/digits/-/_) starting at `pos`.
std::string readId(const std::string& s, usize pos) {
    usize e = pos;
    while (e < s.size()) {
        const char c = s[e];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) break;
        ++e;
    }
    return s.substr(pos, e - pos);
}

}  // namespace

std::string gdriveFileId(const std::string& url) {
    // 1) id= query parameter (?id= or &id=).
    for (const char* key : {"?id=", "&id=", "open?id="}) {
        const usize p = url.find(key);
        if (p != std::string::npos) {
            const std::string id = readId(url, p + std::string(key).size());
            if (!id.empty()) return id;
        }
    }
    // 2) /d/<id>/ or /file/d/<id>/ path form.
    const usize d = url.find("/d/");
    if (d != std::string::npos) {
        const std::string id = readId(url, d + 3);
        if (!id.empty()) return id;
    }
    return "";
}

std::string gdriveDirectUrl(const std::string& fileId) {
    if (fileId.empty()) return "";
    return "https://drive.usercontent.google.com/download?id=" + fileId +
           "&export=download&confirm=t";
}

}  // namespace uaro
