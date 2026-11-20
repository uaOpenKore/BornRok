#include "patcher/LocalReuse.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace uaro {

bool localFileStat(const std::string& path, u64& size, i64& mtime) {
    std::error_code ec;
    const auto sz = fs::file_size(path, ec);
    if (ec) return false;
    const auto wt = fs::last_write_time(path, ec);
    if (ec) return false;
    size = static_cast<u64>(sz);
    mtime = static_cast<i64>(wt.time_since_epoch().count());
    return true;
}

std::string findLocalCopy(const std::string& dir, const std::string& destName, const std::string& sha,
                          const LocalFileMap& records,
                          const std::function<std::optional<std::string>(const std::string&)>& hashFile) {
    std::vector<std::string> stale;
    for (const auto& [name, rec] : records) {
        if (name == destName) continue;
        const std::string p = (fs::path(dir) / name).string();
        u64 sz = 0;
        i64 mt = 0;
        if (!localFileStat(p, sz, mt)) continue;
        if (rec.size == sz && rec.mtime == mt) {
            if (rec.sha == sha) return p;  // trusted record
        } else {
            stale.push_back(p);  // changed since its hash was confirmed: only a fresh hash can tell
        }
    }
    for (const std::string& p : stale)
        if (auto h = hashFile(p); h && *h == sha) return p;
    return {};
}

bool adoptLocalCopy(const std::string& srcPath, const std::string& destPath) {
    std::error_code ec;
    const std::string part = destPath + ".part";
    fs::remove(part, ec);
    ec.clear();
    fs::create_hard_link(srcPath, part, ec);
    if (ec) {
        ec.clear();
        if (!fs::copy_file(srcPath, part, fs::copy_options::overwrite_existing, ec) || ec) {
            fs::remove(part, ec);
            return false;
        }
    }
    fs::rename(part, destPath, ec);
    if (ec) {  // Windows: rename onto an existing file fails -> remove it first
        ec.clear();
        fs::remove(destPath, ec);
        ec.clear();
        fs::rename(part, destPath, ec);
    }
    if (ec) {
        fs::remove(part, ec);
        return false;
    }
    return true;
}

}  // namespace uaro
