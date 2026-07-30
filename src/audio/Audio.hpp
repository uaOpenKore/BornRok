#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/Types.hpp"
#include "core/math/Math.hpp"

namespace uaro {

class Vfs;

// A positional ambient sound source authored in the map RSW (cricket chirps, a forge's clang, a
// waterfall, ...). Looped while the listener is within `range`, attenuated by distance (#103).
struct AmbientSound {
    std::string wav;          // file under data/wav/
    Vec3 pos{0, 0, 0};        // world position
    float range = 30.0f;      // audible radius (world units)
    float volume = 1.0f;      // authored 0..1
};

// SFX + BGM mixer (SDL3 core audio + dr_wav/dr_mp3). When built without audio (CLIENT_WITH_AUDIO
// undefined) every method is a silent no-op, so the client always builds and runs. The public
// interface is SDL-free; all backend state lives in the .cpp.
class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool init();          // open the audio device; returns false if audio is unavailable/disabled
    void shutdown();
    bool ready() const;

    // Per-frame pump (call once each main-loop iteration, in any scene): keeps the looping BGM
    // fed so it never runs dry. Ambient loops are driven separately by updateListener().
    void update();

    // Play data/wav/<name> once. `gain` 0..1 scales the SFX bus; `pan` -1..1 (left..right);
    // `atten` 0..1 distance attenuation (1 = at the listener, 0 = inaudible). Chunks are cached.
    void playSfx(const Vfs& vfs, const std::string& name, float gain = 1.0f, float pan = 0.0f,
                 float atten = 1.0f);

    // Decode + cache one SFX (data/wav/<name>) so a later playSfx has no decode hitch. No-op if
    // already cached. Used by the map-entry sound preload (S.: cache every SFX once on map connect,
    // freed only on exit). The cache is never cleared except in shutdown().
    void warmSfx(const Vfs& vfs, const std::string& name);

    // Loop a BGM track. `path` is a full VFS/loose path to an .mp3 (from mp3nametable). "" stops.
    void playBgm(const Vfs& vfs, const std::string& path);
    void stopBgm();

    // Map ambience: set the RSW sound list on map entry, then call updateListener() each frame with
    // the player position; the mixer fades each loop in/out by distance.
    void setAmbients(const Vfs& vfs, const std::vector<AmbientSound>& list);
    void updateListener(const Vec3& listenerPos);
    void clearAmbients();
    void clearSfxCache();  // free decoded SFX clips on map-server disconnect (#cache); BGM unaffected

    // Settings (0..1). on=false mutes that bus without forgetting the volume. Master scales both buses.
    void setMasterVolume(float v);
    void setSfxVolume(float v);
    void setSfxOn(bool on);
    void setBgmVolume(float v);
    void setBgmOn(bool on);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace uaro
