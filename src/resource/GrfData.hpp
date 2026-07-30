#pragma once
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Central data-from-GRF layer: runs the RO client's .lub scripts (skill tree, sprite names, headgear
// offsets, status icons, effects) in an embedded Lua VM and exposes typed lookups, so the client
// drives everything off the GRF like the original instead of hardcoded C++ tables. Load once at
// startup; lookups are cached. `Reader` returns a .lub's raw bytes for a GRF vpath (Vfs::read in the
// app; a plain file read in tests) -- returns nullopt if absent.
class GrfData {
public:
    using Reader = std::function<std::optional<std::vector<u8>>(const std::string& vpath)>;

    // Load the tables. Missing lubs are skipped (lookups then fall back to hardcoded paths in the
    // caller). Returns true if at least the core job tables loaded.
    bool load(const Reader& read);
    bool loaded() const { return loaded_; }

    // Player body sprite folder name for a job class id (cp949 Korean, e.g. novice), or "" if unknown
    // -> caller keeps its existing hardcoded fallback for now.
    const std::string& jobSpriteName(u16 classId) const;

    // NPC / monster / pet sprite folder name by class id (from JobNameTable), or "".
    const std::string& npcSpriteName(u32 classId) const;

    // Status-effect (EFST) tooltip text from stateiconinfo.lub: title line + body block, exactly the
    // strings the original client shows. nullptr if this EFST id has no description.
    struct StatusDesc {
        std::string title;  // first descript line (e.g. "Endure")
        std::string body;   // remaining lines joined with '\n' (placeholder "%s" tokens dropped)
    };
    const StatusDesc* statusDesc(u32 efstId) const;

    // Skill-tree skill ids a job can learn, from skilltreeview.lub (SKILL_TREEVIEW_FOR_JOB), keyed by
    // the player job class id. Used to grey out not-yet-learned skills in the skill window like the
    // original client. nullptr if this job has no tree. Requires the client's JOBID enum, which the
    // original supplies from its EXE -- GrfData injects it (JobConst) so the lub resolves.
    const std::vector<u16>* skillTree(u16 jobId) const;

private:
    bool loaded_ = false;
    std::unordered_map<u32, std::string> jobSprite_;  // player job id -> body sprite folder
    std::unordered_map<u32, std::string> npcSprite_;  // npc/mob class id -> sprite folder
    std::unordered_map<u32, StatusDesc> statusDesc_;  // EFST id -> tooltip text
    std::unordered_map<u32, std::vector<u16>> skillTree_;  // player job id -> learnable skill ids
    std::string empty_;
};

}  // namespace uaro
