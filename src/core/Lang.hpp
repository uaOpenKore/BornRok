#pragma once
#include <string>
#include <unordered_map>

#include "core/Types.hpp"

namespace uaro {

// Minimal i18n string table (#104 groundwork). Texts live in texts/<Language>.cfg as `key=value`
// lines (// comments and blank lines skipped; the value runs to end-of-line and may contain spaces).
// tr(key) returns the localized text, or the key itself when missing so untranslated UI still reads.
// Only English ships today; the table makes adding a language a data drop, not a code change.
class Lang {
public:
    static Lang& instance() {
        static Lang g;
        return g;
    }

    // Replace the table from a whole-file string.
    void load(const std::string& text) {
        map_.clear();
        usize i = 0;
        const usize n = text.size();
        while (i < n) {
            usize eol = text.find('\n', i);
            if (eol == std::string::npos) eol = n;
            std::string line = text.substr(i, eol - i);
            i = eol + 1;
            // trim trailing CR / whitespace
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            usize s = 0;
            while (s < line.size() && (line[s] == ' ' || line[s] == '\t')) ++s;
            if (s >= line.size() || line[s] == '#' || (line[s] == '/' && s + 1 < line.size() && line[s + 1] == '/'))
                continue;  // blank / comment
            const usize eq = line.find('=', s);
            if (eq == std::string::npos) continue;
            std::string key = line.substr(s, eq - s);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            usize vs = eq + 1;
            while (vs < line.size() && (line[vs] == ' ' || line[vs] == '\t')) ++vs;
            if (key.empty()) continue;
            // Unescape "\n" (backslash + n) into real newlines so a value can hold a multi-line block.
            std::string val;
            const std::string raw = line.substr(vs);
            for (usize k = 0; k < raw.size(); ++k) {
                if (raw[k] == '\\' && k + 1 < raw.size() && raw[k + 1] == 'n') { val.push_back('\n'); ++k; }
                else val.push_back(raw[k]);
            }
            map_[key] = std::move(val);
        }
    }

    // Localized text for `key`, or `key` itself if there is no entry.
    std::string tr(const std::string& key) const {
        const auto it = map_.find(key);
        return it != map_.end() ? it->second : key;
    }

    usize count() const { return map_.size(); }

private:
    std::unordered_map<std::string, std::string> map_;
};

// Shorthand used at call sites.
inline std::string tr(const std::string& key) { return Lang::instance().tr(key); }

}  // namespace uaro
