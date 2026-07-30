#include "resource/GrfData.hpp"

#include "resource/LuaTables.hpp"

namespace uaro {

namespace {
constexpr const char* kBase = "data/luafiles514/lua files/";
}

bool GrfData::load(const Reader& read) {
    loaded_ = false;
    jobSprite_.clear();
    npcSprite_.clear();

    LuaTables lt;
    auto runLub = [&](const std::string& rel, const char* chunk) -> bool {
        auto bytes = read(std::string(kBase) + rel);
        if (!bytes || bytes->empty()) return false;
        return lt.run(bytes->data(), bytes->size(), chunk);
    };

    // Enum bootstrap: npcidentity.lub builds `jobtbl` (JT_NOVICE->id, ...); promote it to bare globals
    // so the data lubs below resolve `JT_*` exactly like the original client does.
    const bool haveEnum = runLub("datainfo/npcidentity.lub", "npcidentity");
    if (haveEnum) lt.promoteToGlobals("jobtbl");

    // NPC / monster / pet sprite folder names: JobNameTable[classId] = folder.
    if (runLub("datainfo/jobname.lub", "jobname")) {
        std::vector<std::pair<i64, std::string>> m;
        if (lt.stringMap("JobNameTable", m))
            for (auto& e : m)
                if (e.first >= 0) npcSprite_[static_cast<u32>(e.first)] = std::move(e.second);
    }

    // Player body sprite folder names (gendered file): pcJobTbl2[jobId] = folder.
    if (runLub("datainfo/pcjobnamegender_f.lub", "pcjobname")) {
        std::vector<std::pair<i64, std::string>> m;
        if (lt.stringMap("pcJobTbl2", m))
            for (auto& e : m)
                if (e.first >= 0) jobSprite_[static_cast<u32>(e.first)] = std::move(e.second);
    }

    // Status-effect tooltip text: efstids.lub defines the EFST_* ids that stateiconinfo.lub keys its
    // StateIconList by; running efstids first lets the numeric keys resolve like the original client.
    runLub("stateicon/efstids.lub", "efstids");
    if (runLub("stateicon/stateiconinfo.lub", "stateiconinfo")) {
        std::vector<std::pair<i64, std::vector<std::string>>> rows;
        if (lt.descriptRows("StateIconList", "descript", rows)) {
            for (auto& r : rows) {
                if (r.first < 0 || r.second.empty()) continue;
                StatusDesc sd;
                sd.title = r.second.front();
                for (usize i = 1; i < r.second.size(); ++i) {
                    if (r.second[i] == "%s") continue;  // inline value placeholder, no standalone text
                    if (!sd.body.empty()) sd.body += '\n';
                    sd.body += r.second[i];
                }
                statusDesc_[static_cast<u32>(r.first)] = std::move(sd);
            }
        }
    }

    // Skill tree per job (greyed not-yet-learned skills), from skilltreeview.lub. That lub indexes a
    // `JOBID` enum + `SKID`; the original client injects JOBID from its EXE (it is NOT in any GRF lub
    // -- jobtbl is NPC-centric), so we inject the player job class ids here (values = official JobConst
    // / eAthena job ids) exactly like the original, then read SKILL_TREEVIEW_FOR_JOB[jobId].
    lt.injectEnum("JOBID", {
        {"JT_NOVICE", 0}, {"JT_SWORDMAN", 1}, {"JT_MAGICIAN", 2}, {"JT_ARCHER", 3},
        {"JT_ACOLYTE", 4}, {"JT_MERCHANT", 5}, {"JT_THIEF", 6}, {"JT_KNIGHT", 7},
        {"JT_PRIEST", 8}, {"JT_WIZARD", 9}, {"JT_BLACKSMITH", 10}, {"JT_HUNTER", 11},
        {"JT_ASSASSIN", 12}, {"JT_CRUSADER", 14}, {"JT_MONK", 15}, {"JT_SAGE", 16},
        {"JT_ROGUE", 17}, {"JT_ALCHEMIST", 18}, {"JT_BARD", 19}, {"JT_DANCER", 20},
        {"JT_SUPERNOVICE", 23}, {"JT_GUNSLINGER", 24}, {"JT_NINJA", 25}, {"JT_TAEKWON", 4046},
        {"JT_STAR", 4047}, {"JT_LINKER", 4049},
        {"JT_KNIGHT_H", 4008}, {"JT_PRIEST_H", 4009}, {"JT_WIZARD_H", 4010}, {"JT_BLACKSMITH_H", 4011},
        {"JT_HUNTER_H", 4012}, {"JT_ASSASSIN_H", 4013}, {"JT_CRUSADER_H", 4015}, {"JT_MONK_H", 4016},
        {"JT_SAGE_H", 4017}, {"JT_ROGUE_H", 4018}, {"JT_ALCHEMIST_H", 4019}, {"JT_BARD_H", 4020},
        {"JT_DANCER_H", 4021}, {"JT_SUPERNOVICE2", 4190},
        {"JT_RUNE_KNIGHT", 4054}, {"JT_WARLOCK", 4055}, {"JT_RANGER", 4056}, {"JT_ARCHBISHOP", 4057},
        {"JT_MECHANIC", 4058}, {"JT_GUILLOTINE_CROSS", 4059}, {"JT_ROYAL_GUARD", 4066},
        {"JT_SORCERER", 4067}, {"JT_MINSTREL", 4068}, {"JT_WANDERER", 4069}, {"JT_SURA", 4070},
        {"JT_GENETIC", 4071}, {"JT_SHADOW_CHASER", 4072}, {"JT_KAGEROU", 4211}, {"JT_OBORO", 4212},
    });
    runLub("skillinfoz/skillid.lub", "skillid");  // builds SKID (skill-id enum) the tree indexes
    if (runLub("skillinfoz/skilltreeview.lub", "skilltreeview")) {
        std::vector<std::pair<i64, std::vector<i64>>> rows;
        if (lt.intSubValues("SKILL_TREEVIEW_FOR_JOB", rows)) {
            for (auto& r : rows) {
                if (r.first < 0) continue;
                std::vector<u16> ids;
                ids.reserve(r.second.size());
                for (i64 s : r.second)
                    if (s > 0 && s < 0x10000) ids.push_back(static_cast<u16>(s));
                if (!ids.empty()) skillTree_[static_cast<u32>(r.first)] = std::move(ids);
            }
        }
    }

    loaded_ = !jobSprite_.empty() || !npcSprite_.empty() || !statusDesc_.empty() || !skillTree_.empty();
    return loaded_;
}

const std::vector<u16>* GrfData::skillTree(u16 jobId) const {
    auto it = skillTree_.find(jobId);
    return it != skillTree_.end() ? &it->second : nullptr;
}

const GrfData::StatusDesc* GrfData::statusDesc(u32 efstId) const {
    auto it = statusDesc_.find(efstId);
    return it != statusDesc_.end() ? &it->second : nullptr;
}

const std::string& GrfData::jobSpriteName(u16 classId) const {
    auto it = jobSprite_.find(classId);
    return it != jobSprite_.end() ? it->second : empty_;
}

const std::string& GrfData::npcSpriteName(u32 classId) const {
    auto it = npcSprite_.find(classId);
    return it != npcSprite_.end() ? it->second : empty_;
}

}  // namespace uaro
