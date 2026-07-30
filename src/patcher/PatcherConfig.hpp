#pragma once
// patcher/patcher.cfg: the list of mirror URLs the patcher fetches downloads.list
// from, tried in order (first success wins). One URL per line; blank lines and
// lines starting with '#' are ignored. If the file is missing the patcher writes
// it with the built-in defaults. Parsing is pure -> unit-tested offline.
#include <string>
#include <vector>

namespace uaro {

struct PatcherConfig {
    std::vector<std::string> manifestUrls;

    // Built-in defaults (used when patcher.cfg is absent).
    static PatcherConfig defaults();
    // Parse the cfg text (one URL per line; '#' comments; whitespace trimmed).
    static PatcherConfig parse(const std::string& text);
    // Serialise back to cfg text (for writing the default file).
    std::string serialize() const;
};

}  // namespace uaro
