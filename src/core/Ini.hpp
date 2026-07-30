#pragma once
#include <map>
#include <sstream>
#include <string>
#include <string_view>

// Minimal INI reader. The original client is configured through INI files
// (e.g. data.ini lists the GRF load order), so a small, robust parser here is
// directly useful for the resource layer (v1).
namespace uaro {

class Ini {
public:
    static Ini parse(std::string_view text) {
        Ini ini;
        std::string section;
        std::istringstream stream{std::string(text)};
        std::string line;
        while (std::getline(stream, line)) {
            std::string_view sv = trim(line);
            if (sv.empty() || sv.front() == ';' || sv.front() == '#') continue;
            if (sv.front() == '[' && sv.back() == ']') {
                section = std::string(trim(sv.substr(1, sv.size() - 2)));
                continue;
            }
            auto eq = sv.find('=');
            if (eq == std::string_view::npos) continue;
            std::string key(trim(sv.substr(0, eq)));
            std::string value(trim(sv.substr(eq + 1)));
            ini.data_[section][key] = value;
        }
        return ini;
    }

    bool has(const std::string& section, const std::string& key) const {
        auto s = data_.find(section);
        return s != data_.end() && s->second.find(key) != s->second.end();
    }

    std::string get(const std::string& section, const std::string& key,
                    const std::string& def = "") const {
        auto s = data_.find(section);
        if (s == data_.end()) return def;
        auto k = s->second.find(key);
        return k == s->second.end() ? def : k->second;
    }

    const std::map<std::string, std::map<std::string, std::string>>& sections() const {
        return data_;
    }

private:
    static std::string_view trim(std::string_view s) {
        const char* ws = " \t\r\n";
        auto b = s.find_first_not_of(ws);
        if (b == std::string_view::npos) return {};
        auto e = s.find_last_not_of(ws);
        return s.substr(b, e - b + 1);
    }

    std::map<std::string, std::map<std::string, std::string>> data_;
};

} // namespace uaro
