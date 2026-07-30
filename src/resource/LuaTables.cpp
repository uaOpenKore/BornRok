#include "resource/LuaTables.hpp"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace uaro {

LuaTables::LuaTables() {
    L_ = luaL_newstate();
    if (L_) luaL_openlibs(L_);
}

LuaTables::~LuaTables() {
    if (L_) lua_close(L_);
}

void LuaTables::injectEnum(const char* name, const std::vector<std::pair<std::string, i64>>& entries) {
    if (!L_) return;
    lua_getglobal(L_, name);
    if (!lua_istable(L_, -1)) {
        lua_pop(L_, 1);
        lua_newtable(L_);
    }
    for (const auto& e : entries) {
        lua_pushinteger(L_, static_cast<lua_Integer>(e.second));
        lua_setfield(L_, -2, e.first.c_str());  // t[key] = val
    }
    lua_setglobal(L_, name);  // pops the table
}

bool LuaTables::run(const u8* bytes, usize n, const char* chunkName) {
    error_.clear();
    if (!L_) { error_ = "no lua state"; return false; }
    if (luaL_loadbuffer(L_, reinterpret_cast<const char*>(bytes), n, chunkName) != 0) {
        error_ = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "load failed";
        lua_pop(L_, 1);
        return false;
    }
    if (lua_pcall(L_, 0, 0, 0) != 0) {
        error_ = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "run failed";
        lua_pop(L_, 1);
        return false;
    }
    return true;
}

bool LuaTables::intKeys(const char* name, std::vector<i64>& out) {
    if (!L_) return false;
    lua_getglobal(L_, name);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return false; }
    lua_pushnil(L_);
    while (lua_next(L_, -2) != 0) {
        // key @ -2, value @ -1. Only read the key when it is a number (converting a string key in
        // place would corrupt lua_next); a number key is safe to read without conversion.
        if (lua_type(L_, -2) == LUA_TNUMBER)
            out.push_back(static_cast<i64>(lua_tointeger(L_, -2)));
        lua_pop(L_, 1);  // pop value, keep key for the next iteration
    }
    lua_pop(L_, 1);  // pop table
    return true;
}

bool LuaTables::intArrayAt(const char* name, i64 key, std::vector<i64>& out) {
    if (!L_) return false;
    lua_getglobal(L_, name);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return false; }
    lua_pushinteger(L_, static_cast<lua_Integer>(key));
    lua_gettable(L_, -2);  // global[key]
    if (!lua_istable(L_, -1)) { lua_pop(L_, 2); return false; }
    const int len = static_cast<int>(lua_objlen(L_, -1));
    for (int i = 1; i <= len; ++i) {
        lua_rawgeti(L_, -1, i);
        if (lua_isnumber(L_, -1)) out.push_back(static_cast<i64>(lua_tointeger(L_, -1)));
        lua_pop(L_, 1);
    }
    lua_pop(L_, 2);  // value + global table
    return true;
}

bool LuaTables::intRowsAt(const char* name, i64 key, std::vector<std::vector<i64>>& out) {
    if (!L_) return false;
    lua_getglobal(L_, name);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return false; }
    lua_pushinteger(L_, static_cast<lua_Integer>(key));
    lua_gettable(L_, -2);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 2); return false; }
    const int len = static_cast<int>(lua_objlen(L_, -1));
    for (int i = 1; i <= len; ++i) {
        lua_rawgeti(L_, -1, i);  // row
        std::vector<i64> row;
        if (lua_istable(L_, -1)) {
            const int rl = static_cast<int>(lua_objlen(L_, -1));
            for (int j = 1; j <= rl; ++j) {
                lua_rawgeti(L_, -1, j);
                if (lua_isnumber(L_, -1)) row.push_back(static_cast<i64>(lua_tointeger(L_, -1)));
                lua_pop(L_, 1);
            }
        }
        out.push_back(std::move(row));
        lua_pop(L_, 1);  // row
    }
    lua_pop(L_, 2);
    return true;
}

bool LuaTables::stringMap(const char* name, std::vector<std::pair<i64, std::string>>& out) {
    if (!L_) return false;
    lua_getglobal(L_, name);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return false; }
    lua_pushnil(L_);
    while (lua_next(L_, -2) != 0) {
        // key @ -2, value @ -1. Read only number keys with string values (don't convert the key).
        if (lua_type(L_, -2) == LUA_TNUMBER && lua_type(L_, -1) == LUA_TSTRING) {
            const i64 k = static_cast<i64>(lua_tointeger(L_, -2));
            const char* v = lua_tostring(L_, -1);
            out.emplace_back(k, v ? v : "");
        }
        lua_pop(L_, 1);
    }
    lua_pop(L_, 1);
    return true;
}

bool LuaTables::descriptRows(const char* name, const char* field,
                             std::vector<std::pair<i64, std::vector<std::string>>>& out) {
    if (!L_) return false;
    lua_getglobal(L_, name);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return false; }
    lua_pushnil(L_);
    while (lua_next(L_, -2) != 0) {
        // key @ -2 (read only number keys; don't convert), value @ -1 (the per-id record table).
        if (lua_type(L_, -2) == LUA_TNUMBER && lua_istable(L_, -1)) {
            const i64 k = static_cast<i64>(lua_tointeger(L_, -2));
            lua_getfield(L_, -1, field);  // record[field]
            if (lua_istable(L_, -1)) {
                std::vector<std::string> lines;
                const int len = static_cast<int>(lua_objlen(L_, -1));
                for (int i = 1; i <= len; ++i) {
                    lua_rawgeti(L_, -1, i);  // element = { text, {r,g,b} } or a bare string
                    if (lua_type(L_, -1) == LUA_TSTRING) {
                        const char* s = lua_tostring(L_, -1);
                        if (s) lines.emplace_back(s);
                    } else if (lua_istable(L_, -1)) {
                        lua_rawgeti(L_, -1, 1);  // element[1] = the text
                        if (lua_type(L_, -1) == LUA_TSTRING) {
                            const char* s = lua_tostring(L_, -1);
                            if (s) lines.emplace_back(s);
                        }
                        lua_pop(L_, 1);
                    }
                    lua_pop(L_, 1);  // element
                }
                if (!lines.empty()) out.emplace_back(k, std::move(lines));
            }
            lua_pop(L_, 1);  // record[field]
        }
        lua_pop(L_, 1);  // pop value, keep key
    }
    lua_pop(L_, 1);  // pop name table
    return true;
}

bool LuaTables::intSubValues(const char* name, std::vector<std::pair<i64, std::vector<i64>>>& out) {
    if (!L_) return false;
    lua_getglobal(L_, name);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return false; }
    lua_pushnil(L_);
    while (lua_next(L_, -2) != 0) {
        // key @ -2 (int only; don't convert), value @ -1 (the per-job sub-table).
        if (lua_type(L_, -2) == LUA_TNUMBER && lua_istable(L_, -1)) {
            const i64 k = static_cast<i64>(lua_tointeger(L_, -2));
            std::vector<i64> vals;
            lua_pushnil(L_);
            while (lua_next(L_, -2) != 0) {  // iterate the sub-table's entries
                if (lua_type(L_, -1) == LUA_TNUMBER)
                    vals.push_back(static_cast<i64>(lua_tointeger(L_, -1)));
                lua_pop(L_, 1);
            }
            if (!vals.empty()) out.emplace_back(k, std::move(vals));
        }
        lua_pop(L_, 1);  // pop value, keep key
    }
    lua_pop(L_, 1);  // pop name table
    return true;
}

void LuaTables::promoteToGlobals(const char* src) {
    if (!L_) return;
    lua_getglobal(L_, src);
    if (!lua_istable(L_, -1)) { lua_pop(L_, 1); return; }
    lua_pushnil(L_);
    while (lua_next(L_, -2) != 0) {
        // key @ -2, value @ -1. Set _G[key] = value for string keys. Duplicate the pair so lua_next's
        // key stays intact (lua_setglobal on a copied key; keep the original key for iteration).
        if (lua_type(L_, -2) == LUA_TSTRING) {
            const char* k = lua_tostring(L_, -2);
            lua_pushvalue(L_, -1);        // copy value
            lua_setglobal(L_, k);         // _G[k] = value (pops the copy)
        }
        lua_pop(L_, 1);                   // pop value, keep key
    }
    lua_pop(L_, 1);                       // pop src table
}

void LuaTables::aliasGlobal(const char* dst, const char* src) {
    if (!L_) return;
    lua_getglobal(L_, src);   // push src
    lua_setglobal(L_, dst);   // dst = src (pops)
}

}  // namespace uaro
