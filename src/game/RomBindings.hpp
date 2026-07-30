#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include "core/Types.hpp"

namespace uaro {

class Application;
class RomActor;

// RO-sprite-name -> RoM 3D model bindings (aliases, reskins, bundle layout), shared by
// GameScene (ROeM Mobs render) and the --view content browser. Split out of GameScene so
// the viewer lists/loads models without a game session.
namespace rombind {

using Cache = std::unordered_map<std::string, std::unique_ptr<RomActor>>;

// Load (or fetch from `cache`) the RoM model for an RO sprite name. Caches nullptr on a
// miss so each unmapped name logs/probes once.
// `skin` (optional): force a specific texture from the model's family skins — the --view
// dyes tab; cached under "name#skin".
RomActor* actorFor(Application& app, const std::string& name, Cache& cache,
                   const std::string& skin = {});

// Resolve WITHOUT loading: the bundle vpath actorFor would read ("" if none). Existence
// only -- a bundle can still fail to assemble (empty mesh) when actually loaded.
std::string resolveBundle(Application& app, const std::string& name);

// The player model for a job/sex/hair (+ optional weapon model in hand and a peco under
// the saddle): body + face + hair assembled per the ROeM Chars pipeline. Cached by a
// composite key in `cache`.
RomActor* playerFor(Application& app, u16 jobClass, u8 sex, u16 hair, u16 weapon,
                    bool riding, Cache& cache, u16 headTop = 0, u16 headMid = 0,
                    u16 headBottom = 0);
// The peco mount (cached under "mount:pecopeco").
RomActor* mountFor(Application& app, Cache& cache);
// RO weapon view id -> default RoM weapon model path fragment (61_* series), or nullptr.
const char* weaponModelFor(u16 view);

}  // namespace rombind
}  // namespace uaro
