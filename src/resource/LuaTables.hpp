#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/Types.hpp"

struct lua_State;

namespace uaro {

// Loads the RO client's compiled .lub data tables (skill tree, sprite offsets, status icons,
// effects, ...) straight from the GRF, running them in an embedded Lua 5.1 VM exactly like the
// original client. Owns one lua_State; you inject the host-side enums the tables reference (JOBID,
// SKID — see the RO client), run each .lub's bytecode (dependency lubs first, same state), then read
// the resulting global tables. The vendored Lua's bytecode loader is patched to accept RO's 32-bit
// .lub on a 64-bit host (see third_party/lua51/lundump.c).
class LuaTables {
public:
    LuaTables();
    ~LuaTables();
    LuaTables(const LuaTables&) = delete;
    LuaTables& operator=(const LuaTables&) = delete;

    // Define a global table `name` = { key = value, ... } before running lubs that index it (e.g.
    // JOBID.JT_NOVICE). Merges into an existing global of that name if present.
    void injectEnum(const char* name, const std::vector<std::pair<std::string, i64>>& entries);

    // Run a .lub (or plain .lua) chunk in this state. Returns false + sets error() on load/run failure.
    bool run(const u8* bytes, usize n, const char* chunkName);
    const std::string& error() const { return error_; }

    // --- table reading (each call is self-contained: pushes what it needs and cleans up) --------
    // The integer keys of global table `name` (e.g. which job ids have a skill tree). false if missing.
    bool intKeys(const char* name, std::vector<i64>& out);

    // global[key] read as an array of integers (e.g. SKILL_TREEVIEW_FOR_JOB[jobid] -> skill ids).
    bool intArrayAt(const char* name, i64 key, std::vector<i64>& out);

    // global[key] read as an array of {int...} rows (e.g. offset tables: each row several ints).
    bool intRowsAt(const char* name, i64 key, std::vector<std::vector<i64>>& out);

    // global table read as {int key -> string value} (e.g. JobNameTable[jobid] = sprite folder name).
    // Non-integer keys / non-string values are skipped.
    bool stringMap(const char* name, std::vector<std::pair<i64, std::string>>& out);

    // For each integer key k of global table `name`, read the list field `field` of global[name][k]
    // as text lines. Each list element is `{ text, {r,g,b} }` (roBrowser's StateIconList.descript
    // shape) or a bare string; only the text (element[1] / the string) is taken -- colours are
    // dropped. out is keyed by k, lines in table order (title first). Records without the field are
    // skipped. Used for status-icon descriptions straight from the GRF.
    bool descriptRows(const char* name, const char* field,
                      std::vector<std::pair<i64, std::vector<std::string>>>& out);

    // For each integer key k of global table `name`, read the integer VALUES of its sub-table into a
    // list: out[k] = [v1, v2, ...]. e.g. SKILL_TREEVIEW_FOR_JOB[jobId] = { pos -> skillId } yields
    // jobId -> [skillId...]. Non-table entries / non-int values are skipped.
    bool intSubValues(const char* name, std::vector<std::pair<i64, std::vector<i64>>>& out);

    // Promote every entry of global table `src` (string key -> value) to a global of that name, so
    // lubs that reference the enum as bare globals (e.g. JT_NOVICE) resolve. Mirrors the original
    // client's enum bootstrap. No-op if `src` is missing.
    void promoteToGlobals(const char* src);

    // Make global `dst` refer to global `src` (dst = src). Used when a data lub reads the enum as a
    // TABLE under a different name than the lub that built it (e.g. skilltreeview reads JOBID.JT_*,
    // while npcidentity builds the same table as `jobtbl` -> aliasGlobal("JOBID","jobtbl")).
    void aliasGlobal(const char* dst, const char* src);

    lua_State* L() const { return L_; }

private:
    lua_State* L_ = nullptr;
    std::string error_;
};

}  // namespace uaro
