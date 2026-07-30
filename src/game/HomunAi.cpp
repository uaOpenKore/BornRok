#include "game/HomunAi.hpp"

#include <vector>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "core/Log.hpp"
#include "resource/Vfs.hpp"

namespace uaro {

namespace {

// Pull the host back-pointer stashed as the closure's upvalue when the bridge was registered.
HomunAiHost* host(lua_State* L) {
    return static_cast<HomunAiHost*>(lua_touserdata(L, lua_upvalueindex(1)));
}

// Push a flat int list (Const.lua command tuple) as a 1-based Lua array. Empty -> {NONE_CMD=0}.
void pushCommand(lua_State* L, const std::vector<int>& cmd) {
    lua_newtable(L);
    if (cmd.empty()) {
        lua_pushinteger(L, 0);  // NONE_CMD
        lua_rawseti(L, -2, 1);
        return;
    }
    for (usize i = 0; i < cmd.size(); ++i) {
        lua_pushinteger(L, cmd[i]);
        lua_rawseti(L, -2, static_cast<int>(i) + 1);
    }
}

// --- the twelve native bridges (see data/ai/Const.lua) --------------------------------------------

int l_traceAI(lua_State* L) {
    if (auto* h = host(L))
        if (const char* s = lua_tostring(L, 1)) h->trace(s);
    return 0;
}

int l_moveToOwner(lua_State* L) {  // MoveToOwner(id) -- id is always the homun
    if (auto* h = host(L)) h->moveToOwner();
    return 0;
}

int l_move(lua_State* L) {  // Move(id, x, y)
    if (auto* h = host(L))
        h->moveTo(static_cast<int>(luaL_optinteger(L, 2, 0)),
                  static_cast<int>(luaL_optinteger(L, 3, 0)));
    return 0;
}

int l_attack(lua_State* L) {  // Attack(id, target)
    if (auto* h = host(L))
        h->attack(static_cast<u32>(luaL_optinteger(L, 2, 0)));
    return 0;
}

int l_getV(lua_State* L) {  // GetV(V_, id [, skill])
    auto* h = host(L);
    const int prop = static_cast<int>(luaL_checkinteger(L, 1));
    const u32 id = static_cast<u32>(luaL_optinteger(L, 2, 0));
    if (!h) { lua_pushinteger(L, -1); return 1; }
    switch (prop) {
        case 0:  // V_OWNER
            lua_pushinteger(L, static_cast<lua_Integer>(h->ownerGid()));
            return 1;
        case 1: {  // V_POSITION -> x, y
            int x = -1, y = -1;
            h->actorCell(id, x, y);  // leaves -1,-1 when unknown
            lua_pushinteger(L, x);
            lua_pushinteger(L, y);
            return 2;
        }
        case 2:  // V_TYPE (unused by AI.lua)
            lua_pushinteger(L, 0);
            return 1;
        case 3:  // V_MOTION
            lua_pushinteger(L, h->actorMotion(id));
            return 1;
        case 4:  // V_ATTACKRANGE
            lua_pushinteger(L, h->attackRange());
            return 1;
        case 5:  // V_TARGET
            lua_pushinteger(L, static_cast<lua_Integer>(h->actorTarget(id)));
            return 1;
        case 6:  // V_SKILLATTACKRANGE(id, skill)
            lua_pushinteger(L, h->skillRange(static_cast<int>(luaL_optinteger(L, 3, 0))));
            return 1;
        case 7:  // V_HOMUNTYPE
            lua_pushinteger(L, h->homunType());
            return 1;
        default:  // V_HP/SP/MAXHP/MAXSP (8..11) -- AI.lua doesn't read them
            lua_pushinteger(L, 0);
            return 1;
    }
}

int l_getActors(lua_State* L) {  // GetActors() -> { gid, ... }
    std::vector<u32> a;
    if (auto* h = host(L)) h->listActors(a);
    lua_newtable(L);
    for (usize i = 0; i < a.size(); ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(a[i]));
        lua_rawseti(L, -2, static_cast<int>(i) + 1);
    }
    return 1;
}

int l_getTick(lua_State* L) {  // GetTick()
    lua_pushinteger(L, host(L) ? static_cast<lua_Integer>(host(L)->tickMs()) : 0);
    return 1;
}

int l_getMsg(lua_State* L) {  // GetMsg(id) -> command tuple (consumes one)
    std::vector<int> c;
    if (auto* h = host(L)) h->popCommand(c);
    pushCommand(L, c);
    return 1;
}

int l_getResMsg(lua_State* L) {  // GetResMsg(id) -> reserved command tuple
    std::vector<int> c;
    if (auto* h = host(L)) h->peekReserved(c);
    pushCommand(L, c);
    return 1;
}

int l_skillObject(lua_State* L) {  // SkillObject(id, level, skill, target)
    if (auto* h = host(L))
        h->skillObject(static_cast<int>(luaL_optinteger(L, 2, 0)),
                       static_cast<int>(luaL_optinteger(L, 3, 0)),
                       static_cast<u32>(luaL_optinteger(L, 4, 0)));
    return 0;
}

int l_skillGround(lua_State* L) {  // SkillGround(id, level, skill, x, y)
    if (auto* h = host(L))
        h->skillGround(static_cast<int>(luaL_optinteger(L, 2, 0)),
                       static_cast<int>(luaL_optinteger(L, 3, 0)),
                       static_cast<int>(luaL_optinteger(L, 4, 0)),
                       static_cast<int>(luaL_optinteger(L, 5, 0)));
    return 0;
}

int l_isMonster(lua_State* L) {  // IsMonster(id) -> 1/0
    const u32 id = static_cast<u32>(luaL_optinteger(L, 1, 0));
    lua_pushinteger(L, (host(L) && host(L)->isMonster(id)) ? 1 : 0);
    return 1;
}

}  // namespace

HomunAi::HomunAi() = default;

HomunAi::~HomunAi() {
    if (L_) lua_close(L_);
}

bool HomunAi::runScript(const Vfs& vfs, const char* vpath) {
    auto bytes = vfs.read(vpath);
    if (!bytes || bytes->empty()) {
        error_ = std::string("missing AI script: ") + vpath;
        return false;
    }
    if (luaL_loadbuffer(L_, reinterpret_cast<const char*>(bytes->data()), bytes->size(), vpath) != 0 ||
        lua_pcall(L_, 0, 0, 0) != 0) {
        error_ = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "AI script load failed";
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

bool HomunAi::init(HomunAiHost& host, const Vfs& vfs) {
    host_ = &host;
    ready_ = false;
    error_.clear();
    if (L_) { lua_close(L_); L_ = nullptr; }
    L_ = luaL_newstate();
    if (!L_) { error_ = "no lua state"; return false; }
    luaL_openlibs(L_);

    // The scripts open with `require "./AI/Const.lua"` etc.; we preload the three ourselves in one
    // state (below), so neutralise require to a no-op that succeeds. Mirrors how the exe injects the
    // native bridges into a single shared VM rather than resolving files.
    luaL_dostring(L_, "require = function(...) return true end");

    // Register the native bridges, each carrying the host pointer as its upvalue.
    const struct { const char* name; lua_CFunction fn; } kBridges[] = {
        {"TraceAI", l_traceAI},         {"MoveToOwner", l_moveToOwner},
        {"Move", l_move},               {"Attack", l_attack},
        {"GetV", l_getV},               {"GetActors", l_getActors},
        {"GetTick", l_getTick},         {"GetMsg", l_getMsg},
        {"GetResMsg", l_getResMsg},     {"SkillObject", l_skillObject},
        {"SkillGround", l_skillGround}, {"IsMonster", l_isMonster},
    };
    for (const auto& b : kBridges) {
        lua_pushlightuserdata(L_, host_);
        lua_pushcclosure(L_, b.fn, 1);
        lua_setglobal(L_, b.name);
    }

    // Load the three scripts in dependency order (Const defines the enums Util/AI read; Util defines
    // the List + geometry helpers AI calls). GRF paths are lowercase.
    if (!runScript(vfs, "data/ai/const.lua") || !runScript(vfs, "data/ai/util.lua") ||
        !runScript(vfs, "data/ai/ai.lua")) {
        log::warn("HomunAi: {}", error_);
        return false;
    }

    // Confirm the entry point is present.
    lua_getglobal(L_, "AI");
    const bool haveAI = lua_isfunction(L_, -1);
    lua_pop(L_, 1);
    if (!haveAI) { error_ = "AI.lua defines no AI() function"; log::warn("HomunAi: {}", error_); return false; }

    ready_ = true;
    log::info("HomunAi: AI scripts loaded (data/ai/ai.lua + const + util)");
    return true;
}

void HomunAi::tick() {
    if (!ready_ || !host_ || !L_) return;
    const u32 hid = host_->homunGid();
    if (!hid) return;  // no active homunculus -> nothing to drive

    lua_getglobal(L_, "AI");
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return; }
    lua_pushinteger(L_, static_cast<lua_Integer>(hid));
    if (lua_pcall(L_, 1, 0, 0) != 0) {
        // A script error shouldn't spam every tick; log once then swallow.
        static bool logged = false;
        if (!logged) {
            log::warn("HomunAi: AI() error: {}", lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "?");
            logged = true;
        }
        lua_pop(L_, 1);
    }
}

}  // namespace uaro
