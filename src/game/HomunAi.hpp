#pragma once
#include <string>
#include <vector>

#include "core/Types.hpp"

struct lua_State;

namespace uaro {

class Vfs;

// The data the AI scripts read and the actions they take, abstracted away from the scene. GameScene
// implements this against its live actor registry + net connection. All positions are GAT cells; all
// ids are server gids. Mirrors the original client's host C-API surface (see data/ai/Const.lua): the
// twelve native bridges the exe exposes to AI.lua -- everything else (GetDistance, IsInAttackSight,
// IsOutOfSight, GetOwnerPosition, the List helpers) is pure Lua built on top of these, so we only
// have to provide these.
struct HomunAiHost {
    virtual ~HomunAiHost() = default;

    // identity ------------------------------------------------------------------------------------
    virtual u32 homunGid() const = 0;   // the controlled homunculus' gid (0 = none active)
    virtual u32 ownerGid() const = 0;   // the master's (our own) account id

    // queries -------------------------------------------------------------------------------------
    virtual bool actorCell(u32 gid, int& x, int& y) const = 0;  // false -> unknown (Lua sees -1,-1)
    virtual int  actorMotion(u32 gid) const = 0;   // MOTION_* : 0 stand / 1 move / 2 attack / 3 dead
    virtual u32  actorTarget(u32 gid) const = 0;   // who `gid` is attacking (0 = nobody we've seen)
    virtual bool isMonster(u32 gid) const = 0;     // true for mob units (not PCs/NPCs)
    virtual void listActors(std::vector<u32>& out) const = 0;  // visible units (excludes the master)
    virtual int  homunType() const = 0;            // Const.lua LIF..VANILMIRTH_H2 (drives passive/aggro)
    virtual int  attackRange() const = 0;          // homun melee range, cells
    virtual int  skillRange(int skillId) const = 0;// cast range of a homun skill, cells
    virtual u32  tickMs() const = 0;               // GetTick()

    // owner command queue (the homun window's Move/Attack/Follow/Hold buttons feed this). Returns
    // false when empty -> the AI sees NONE_CMD and runs its autonomous behaviour. Each command is a
    // flat int list {CMD, arg, ...} exactly like Const.lua's command tuples.
    virtual bool popCommand(std::vector<int>& out) = 0;      // GetMsg  (consumes one)
    virtual bool peekReserved(std::vector<int>& out) = 0;    // GetResMsg (repeating reserved command)

    // actions ---------------------------------------------------------------------------------------
    virtual void moveTo(int x, int y) = 0;                       // 0x232 hommoveto
    virtual void moveToOwner() = 0;                              // 0x234 hommovetomaster
    virtual void attack(u32 target) = 0;                         // 0x233 homattack
    virtual void skillObject(int level, int skill, u32 target) = 0;  // 0x113 (server routes HM_ skills)
    virtual void skillGround(int level, int skill, int x, int y) = 0;// 0x116
    virtual void trace(const char* msg) = 0;                     // TraceAI (debug log)
};

// Runs the RO homunculus AI (data/ai/AI.lua + Const.lua + Util.lua) in an embedded Lua 5.1 VM,
// exactly like the original client: load the three scripts from the GRF, register the native
// bridges, then call the global AI(homunId) once per AI tick. All decisions live in the scripts.
class HomunAi {
public:
    HomunAi();
    ~HomunAi();
    HomunAi(const HomunAi&) = delete;
    HomunAi& operator=(const HomunAi&) = delete;

    // Load the AI scripts from the GRF (data/ai/ai.lua + const.lua + util.lua) and bind `host`.
    // Returns false + sets error() if a script is missing or fails to compile. Safe to call again to
    // reload. `host` must outlive this object.
    bool init(HomunAiHost& host, const Vfs& vfs);
    bool ready() const { return ready_; }
    const std::string& error() const { return error_; }

    // Drive one AI step: calls the Lua global AI(homunGid). No-op if not ready or no homun is active.
    // Call at the AI cadence (the original client ticks it a few times a second, not every frame).
    void tick();

private:
    bool runScript(const Vfs& vfs, const char* vpath);

    lua_State* L_ = nullptr;
    HomunAiHost* host_ = nullptr;
    bool ready_ = false;
    std::string error_;
};

}  // namespace uaro
