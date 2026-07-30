#include "audio/Audio.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

#include "core/Log.hpp"
#include "resource/Vfs.hpp"

#ifdef CLIENT_WITH_AUDIO
#include "audio/FsbVorbis.hpp"
#include "formats/UnityFs.hpp"
#endif

// The audio backend is compiled only when CLIENT_WITH_AUDIO is defined (the CMake option). It is
// built on plain SDL3 core audio (no SDL_mixer dependency -- SDL3_mixer's API churned between the
// SDL2-compat Mix_* and the new MIX_* and is not something we want to track), plus the vendored
// public-domain single-header decoders dr_wav / dr_mp3 (data/wav/*.wav SFX + BGM/*.mp3). Without
// the option every method below is a no-op so the client always builds and runs.
// The vendored decoders are shared by both device backends (SDL3 on desktop, XAudio2 on Xbox/UWP).
#if defined(CLIENT_WITH_AUDIO) || defined(CLIENT_AUDIO_XAUDIO2)
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "audio/dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "audio/dr_mp3.h"
// FLAC: S. re-packs all SFX as .flac in the content ZIP, loaded in preference to the .wav (lossless
// but ~half the size). dr_flac decodes it to the same interleaved-float Pcm as dr_wav.
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "audio/dr_flac.h"
#endif

#if defined(CLIENT_WITH_AUDIO)
#include <SDL3/SDL.h>
#endif
#if defined(CLIENT_AUDIO_XAUDIO2)
// Xbox/UWP device backend: SDL3 has no WinRT audio, so use the in-box XAudio2 (xaudio2_9). xaudio2.h
// itself pulls in WAVEFORMATEX (its API takes one); the float tag lives in mmreg.h, guarded here in
// case that isn't transitively included.
#include <xaudio2.h>
#ifndef WAVE_FORMAT_IEEE_FLOAT
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#endif
#endif

namespace uaro {

#if defined(CLIENT_WITH_AUDIO)
namespace {
// A fully decoded, interleaved PCM buffer plus the spec describing it.
struct Pcm {
    std::vector<float> f32;     // interleaved samples (SFX / ambients)
    SDL_AudioSpec spec{};       // SDL_AUDIO_F32, <channels>, <freq>
};

// Decode a whole WAV (data/wav/<name>) to interleaved float. Empty on failure.
Pcm decodeWav(const std::vector<u8>& bytes) {
    Pcm out;
    unsigned int channels = 0, freq = 0;
    drwav_uint64 frames = 0;
    float* pcm = drwav_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &channels,
                                                           &freq, &frames, nullptr);
    if (!pcm || channels == 0 || freq == 0) {
        if (pcm) drwav_free(pcm, nullptr);
        return out;
    }
    out.spec.format = SDL_AUDIO_F32;
    out.spec.channels = static_cast<int>(channels);
    out.spec.freq = static_cast<int>(freq);
    out.f32.assign(pcm, pcm + frames * channels);
    drwav_free(pcm, nullptr);
    return out;
}

// Decode a whole FLAC to interleaved float. Empty on failure.
Pcm decodeFlac(const std::vector<u8>& bytes) {
    Pcm out;
    unsigned int channels = 0, freq = 0;
    drflac_uint64 frames = 0;
    float* pcm = drflac_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &channels,
                                                            &freq, &frames, nullptr);
    if (!pcm || channels == 0 || freq == 0) {
        if (pcm) drflac_free(pcm, nullptr);
        return out;
    }
    out.spec.format = SDL_AUDIO_F32;
    out.spec.channels = static_cast<int>(channels);
    out.spec.freq = static_cast<int>(freq);
    out.f32.assign(pcm, pcm + frames * channels);
    drflac_free(pcm, nullptr);
    return out;
}

// Read <dir><name> as PCM, preferring a same-named .flac sibling over the original (S.: FLAC packed
// into the ZIP loads in priority over the .wav). Decodes by content magic ('fLaC' -> FLAC, else WAV),
// so a path that already ends in .flac, or a .wav whose bytes are actually FLAC, both work. Empty on miss.
Pcm loadAudioPreferFlac(const Vfs& vfs, const std::string& dir, const std::string& name) {
    std::string flac = name;
    if (const usize dot = flac.find_last_of('.'); dot != std::string::npos) flac.resize(dot);
    flac += ".flac";
    if (flac != name)
        if (auto b = vfs.readQuiet(dir + flac); b && !b->empty())
            if (Pcm p = decodeFlac(*b); !p.f32.empty()) return p;
    if (auto b = vfs.read(dir + name); b && !b->empty()) {
        if (b->size() >= 4 && (*b)[0] == 'f' && (*b)[1] == 'L' && (*b)[2] == 'a' && (*b)[3] == 'C')
            return decodeFlac(*b);
        return decodeWav(*b);
    }
    return {};
}
}  // namespace
#endif

#if defined(CLIENT_AUDIO_XAUDIO2)
namespace {
// Interleaved 32-bit-float PCM + its channel count / sample rate. XAudio2 consumes float directly
// (WAVE_FORMAT_IEEE_FLOAT) and resamples per source voice, so — unlike the SDL path — there is no
// separate S16 BGM buffer; everything is float. No SDL types here.
struct Pcm {
    std::vector<float> f32;
    int channels = 0;
    int freq = 0;
    bool empty() const { return f32.empty(); }
};

Pcm decodeWav(const std::vector<u8>& bytes) {
    Pcm out;
    unsigned int ch = 0, fr = 0;
    drwav_uint64 frames = 0;
    float* pcm = drwav_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &ch, &fr,
                                                           &frames, nullptr);
    if (!pcm || ch == 0 || fr == 0) {
        if (pcm) drwav_free(pcm, nullptr);
        return out;
    }
    out.channels = static_cast<int>(ch);
    out.freq = static_cast<int>(fr);
    out.f32.assign(pcm, pcm + frames * ch);
    drwav_free(pcm, nullptr);
    return out;
}

Pcm decodeFlac(const std::vector<u8>& bytes) {
    Pcm out;
    unsigned int ch = 0, fr = 0;
    drflac_uint64 frames = 0;
    float* pcm = drflac_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &ch, &fr,
                                                            &frames, nullptr);
    if (!pcm || ch == 0 || fr == 0) {
        if (pcm) drflac_free(pcm, nullptr);
        return out;
    }
    out.channels = static_cast<int>(ch);
    out.freq = static_cast<int>(fr);
    out.f32.assign(pcm, pcm + frames * ch);
    drflac_free(pcm, nullptr);
    return out;
}

Pcm decodeMp3(const std::vector<u8>& bytes) {
    Pcm out;
    drmp3_config cfg{};
    drmp3_uint64 frames = 0;
    float* pcm = drmp3_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &cfg, &frames,
                                                           nullptr);
    if (!pcm || cfg.channels == 0 || cfg.sampleRate == 0) {
        if (pcm) drmp3_free(pcm, nullptr);
        return out;
    }
    out.channels = static_cast<int>(cfg.channels);
    out.freq = static_cast<int>(cfg.sampleRate);
    out.f32.assign(pcm, pcm + frames * cfg.channels);
    drmp3_free(pcm, nullptr);
    return out;
}

// Read <dir><name> preferring a same-named .flac sibling over the original (S.: FLAC packed in the
// ZIP loads in priority over the .wav); decode by content magic. Empty Pcm on a miss.
Pcm loadAudioPreferFlac(const Vfs& vfs, const std::string& dir, const std::string& name) {
    std::string flac = name;
    if (const usize dot = flac.find_last_of('.'); dot != std::string::npos) flac.resize(dot);
    flac += ".flac";
    if (flac != name)
        if (auto b = vfs.readQuiet(dir + flac); b && !b->empty())
            if (Pcm p = decodeFlac(*b); !p.empty()) return p;
    if (auto b = vfs.read(dir + name); b && !b->empty()) {
        if (b->size() >= 4 && (*b)[0] == 'f' && (*b)[1] == 'L' && (*b)[2] == 'a' && (*b)[3] == 'C')
            return decodeFlac(*b);
        return decodeWav(*b);
    }
    return {};
}
}  // namespace
#endif

struct Audio::Impl {
    bool ready = false;
    float masterVol = 1.0f;            // scales both buses (Sound setup) (#103/#104)
    float sfxVol = 0.8f, bgmVol = 0.6f;
    bool sfxOn = true, bgmOn = true;
    Vec3 listener{0, 0, 0};

#if defined(CLIENT_WITH_AUDIO)
    SDL_AudioDeviceID dev = 0;     // one logical playback device; SDL mixes every stream bound to it
    SDL_AudioSpec devSpec{};       // the device's actual format (stream dst spec)

    std::unordered_map<std::string, Pcm> sfxCache;  // name -> decoded SFX (cache misses too)

    std::vector<SDL_AudioStream*> voices;  // live one-shot SFX streams, reaped when drained

    // BGM: a single bound stream re-fed from the fully decoded (S16, half the memory of F32) track.
    SDL_AudioStream* bgmStream = nullptr;
    std::vector<short> bgmPcm;     // interleaved S16, kept alive for looping
    SDL_AudioSpec bgmSpec{};
    std::string bgmPath;           // currently-playing track (so playBgm() is a no-op if unchanged)

    struct Amb {
        Pcm pcm;
        SDL_AudioStream* stream = nullptr;  // bound looping stream while audible, else null
        Vec3 pos;
        float range = 30.0f, volume = 1.0f;
    };
    std::vector<Amb> ambients;

    // Make a stream that converts `src` to the device and bind it; null on failure.
    SDL_AudioStream* makeBound(const SDL_AudioSpec& src) {
        SDL_AudioStream* s = SDL_CreateAudioStream(&src, &devSpec);
        if (!s) return nullptr;
        if (!SDL_BindAudioStream(dev, s)) { SDL_DestroyAudioStream(s); return nullptr; }
        return s;
    }

    const Pcm* loadSfx(const Vfs& vfs, const std::string& nameRaw) {
        // Key the cache case-insensitively: the map-entry preload enumerates archive vpaths (always
        // lowercased by the VFS), but play sites pass literal RO names of mixed case -- lowercasing
        // here makes both converge on one cache entry (no double decode, no preload miss). (S.)
        std::string name = nameRaw;
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (const auto it = sfxCache.find(name); it != sfxCache.end())
            return it->second.f32.empty() ? nullptr : &it->second;
        Pcm pcm;
        // ROeM SFX (#127): when the SFX source is RoM, try its sound banks first — the RO wav
        // name usually matches a bank ("poring_die.wav" -> audio/se/monster/poring_die).
        if (vfs.contentMode(ContentCategory::Sfx) == ContentSource::Rom)
            pcm = loadRomSfx(vfs, name);
        if (pcm.f32.empty())
            pcm = loadAudioPreferFlac(vfs, "data/wav/", name);  // .flac in priority over .wav (S.)
        const Pcm& stored = (sfxCache[name] = std::move(pcm));
        return stored.f32.empty() ? nullptr : &stored;
    }

    // Decode a RoM AudioClip bundle (UnityFS -> .resource -> FSB5 -> headerless Vorbis).
    // Empty Pcm when the name has no RoM counterpart.
    static Pcm loadRomSfx(const Vfs& vfs, const std::string& name) {
        Pcm out;
        std::string base = name;
        if (const usize sl = base.find_last_of("/\\"); sl != std::string::npos)
            base = base.substr(sl + 1);
        if (base.size() > 4 && base.compare(base.size() - 4, 4, ".wav") == 0)
            base.resize(base.size() - 4);
        for (char& c : base) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        // RO effect/.str wav names whose RoM bank is named differently (hand-matched; names
        // that coincide — bash, heal, mammonite, provoke, ... — need no entry).
        static const std::pair<const char*, const char*> kSfxAlias[] = {
            {"adrenaline", "adrenalinerush"},
            {"w_magnum", "magnumbreak"},
            {"lexdivina", "lexdivina_attack"},
            {"kyrie", "kyrieeleison_a"},
            {"magnus", "magnusexorcismus"},
            {"meteor1", "skill_meteor"},
            {"recovery", "skill_recovery"},
            {"firewall1", "firewall_fire"},
            {"sonicblow", "skill_weapon_sword_sonicblade_attack"},
            {"lord", "lordofvermilion"},
            {"cartrevolution", "skill_weapon_cart_hit"},
            {"steal_coin", "skill_movement_steal_attack"},
            {"spearstab", "spear_attack"},
            {"spearboomerang", "spear_attack"},
        };
        for (const auto& [from, to] : kSfxAlias)
            if (base == from) base = to;
        static const char* kSeDirs[] = {"monster/", "skill/", "common/", "ui/", "state/",
                                        "environment/"};
        for (const char* dir : kSeDirs) {
            auto bytes = vfs.readFrom(ContentSource::Rom,
                                      "Android/resources/public/audio/se/" + std::string(dir) +
                                          base + ".unity3d");
            if (!bytes) continue;
            UnityFsBundle bundle;
            if (!bundle.parse(*bytes)) return out;
            for (usize ni = 0; ni < bundle.nodes().size(); ++ni) {
                if (bundle.nodes()[ni].path.find(".resource") == std::string::npos) continue;
                auto res = bundle.nodeData(ni);
                if (!res) continue;
                auto bank = parseFsb5(*res);
                if (!bank || bank->samples.empty()) continue;
                auto dec = decodeFsbVorbis(bank->samples.front());
                if (!dec) continue;
                out.spec.format = SDL_AUDIO_F32;
                out.spec.channels = static_cast<int>(dec->channels);
                out.spec.freq = static_cast<int>(dec->frequency);
                out.f32 = std::move(dec->f32);
                return out;
            }
            return out;
        }
        return out;
    }

    // Drop finished one-shot voices (nothing queued and nothing left to output).
    void reap() {
        for (size_t i = 0; i < voices.size();) {
            SDL_AudioStream* s = voices[i];
            if (SDL_GetAudioStreamAvailable(s) == 0 && SDL_GetAudioStreamQueued(s) == 0) {
                SDL_DestroyAudioStream(s);  // also unbinds
                voices[i] = voices.back();
                voices.pop_back();
            } else {
                ++i;
            }
        }
    }

    // Keep a looping stream fed: append one more copy of the buffer when less than half a loop is
    // still queued. (SDL has no native loop; this is gapless enough for ambience/BGM.)
    static void topUp(SDL_AudioStream* s, const void* data, int bytes) {
        if (bytes > 0 && SDL_GetAudioStreamQueued(s) < bytes / 2)
            SDL_PutAudioStreamData(s, data, bytes);
    }
#endif

#if defined(CLIENT_AUDIO_XAUDIO2)
    IXAudio2* xa = nullptr;
    IXAudio2MasteringVoice* master = nullptr;  // default output device; mixes every source voice

    std::unordered_map<std::string, Pcm> sfxCache;  // name -> decoded SFX (cache misses too)

    // One-shot SFX voice + its OWNED pcm copy: XAudio2 references the submitted buffer until it
    // drains (it does NOT copy), so each voice keeps its own copy — clearSfxCache() mid-play is safe.
    struct Voice {
        IXAudio2SourceVoice* v = nullptr;
        std::vector<float> pcm;
    };
    std::vector<Voice> voices;

    // BGM: one looping source voice fed from the owned track buffer (XAUDIO2_LOOP_INFINITE — native
    // gapless loop, so no manual top-up like the SDL path needs).
    IXAudio2SourceVoice* bgmVoice = nullptr;
    std::vector<float> bgmPcm;
    std::string bgmPath;  // currently-playing track (playBgm() is a no-op if unchanged)

    struct Amb {
        Pcm pcm;
        IXAudio2SourceVoice* v = nullptr;  // looping source voice while audible, else null
        Vec3 pos;
        float range = 30.0f, volume = 1.0f;
    };
    std::vector<Amb> ambients;

    static WAVEFORMATEX wfmt(int channels, int freq) {
        WAVEFORMATEX w{};
        w.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        w.nChannels = static_cast<WORD>(channels);
        w.nSamplesPerSec = static_cast<DWORD>(freq);
        w.wBitsPerSample = 32;
        w.nBlockAlign = static_cast<WORD>(channels * 4);
        w.nAvgBytesPerSec = w.nSamplesPerSec * w.nBlockAlign;
        w.cbSize = 0;
        return w;
    }

    // Create + submit a source voice for a clip (buffer must outlive the voice). loop -> infinite.
    // Returns a STARTED voice, or null on failure.
    IXAudio2SourceVoice* startVoice(int channels, int freq, const float* data, size_t count,
                                    float volume, bool loop) {
        if (!xa || channels <= 0 || freq <= 0 || !data || count == 0) return nullptr;
        WAVEFORMATEX w = wfmt(channels, freq);
        IXAudio2SourceVoice* v = nullptr;
        if (FAILED(xa->CreateSourceVoice(&v, &w)) || !v) return nullptr;
        XAUDIO2_BUFFER buf{};
        buf.AudioBytes = static_cast<UINT32>(count * sizeof(float));
        buf.pAudioData = reinterpret_cast<const BYTE*>(data);
        buf.Flags = loop ? 0u : XAUDIO2_END_OF_STREAM;
        buf.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0u;
        if (FAILED(v->SubmitSourceBuffer(&buf))) { v->DestroyVoice(); return nullptr; }
        v->SetVolume(volume);
        v->Start(0);
        return v;
    }

    // Drop finished one-shot voices (no buffers left queued).
    void reap() {
        for (size_t i = 0; i < voices.size();) {
            XAUDIO2_VOICE_STATE st{};
            voices[i].v->GetState(&st);
            if (st.BuffersQueued == 0) {
                voices[i].v->DestroyVoice();
                if (i != voices.size() - 1) voices[i] = std::move(voices.back());
                voices.pop_back();
            } else {
                ++i;
            }
        }
    }
#endif
};

Audio::Audio() : impl_(std::make_unique<Impl>()) {}
Audio::~Audio() { shutdown(); }
bool Audio::ready() const { return impl_ && impl_->ready; }

#if defined(CLIENT_AUDIO_XAUDIO2)
// ---- XAudio2 backend (Xbox / UWP) ----------------------------------------------------------
// SDL3 has no WinRT audio device, so on Xbox/UWP the mix runs on the in-box XAudio2 (xaudio2_9):
// one mastering voice at unity, one source voice per SFX / ambient / BGM. XAudio2 resamples each
// clip to the device rate and mixes; per-voice SetVolume replaces SDL_SetAudioStreamGain, and a
// native XAUDIO2_LOOP_INFINITE replaces the manual SDL top-up. Decode is the same vendored
// dr_wav/dr_mp3/dr_flac (all float). RoM FSB SFX are desktop-only, so this path is classic
// wav/flac SFX + mp3/flac BGM only. (S. 2026-07-29: full Xbox package WITH sound.)
bool Audio::init() {
    if (impl_->ready) return true;
    if (FAILED(XAudio2Create(&impl_->xa, 0, XAUDIO2_DEFAULT_PROCESSOR)) || !impl_->xa) {
        log::warn("audio: XAudio2Create failed");
        return false;
    }
    if (FAILED(impl_->xa->CreateMasteringVoice(&impl_->master)) || !impl_->master) {
        log::warn("audio: CreateMasteringVoice failed");
        impl_->xa->Release();
        impl_->xa = nullptr;
        return false;
    }
    impl_->ready = true;
    log::info("audio: XAudio2 ready");
    return true;
}

void Audio::shutdown() {
    if (!impl_ || !impl_->ready) return;
    stopBgm();
    clearAmbients();
    for (auto& voice : impl_->voices) voice.v->DestroyVoice();
    impl_->voices.clear();
    impl_->sfxCache.clear();
    if (impl_->master) { impl_->master->DestroyVoice(); impl_->master = nullptr; }
    if (impl_->xa) { impl_->xa->Release(); impl_->xa = nullptr; }
    impl_->ready = false;
}

void Audio::update() {
    if (!impl_->ready) return;
    impl_->reap();  // BGM/ambients loop natively; only one-shot SFX voices need reaping
}

void Audio::clearSfxCache() {
    // One-shot voices keep their OWN pcm copy (XAudio2 references the submitted buffer), so dropping
    // the decoded-clip cache mid-play is safe; BGM streams separately and is unaffected.
    if (impl_) impl_->sfxCache.clear();
}

void Audio::playSfx(const Vfs& vfs, const std::string& name, float gain, float /*pan*/, float atten) {
    if (!impl_->ready || !impl_->sfxOn || impl_->sfxVol <= 0.0f) return;
    impl_->reap();
    std::string key = name;  // cache case-insensitively (preload uses lowercased vpaths)
    for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const Pcm* pcm = nullptr;
    if (const auto it = impl_->sfxCache.find(key); it != impl_->sfxCache.end())
        pcm = it->second.empty() ? nullptr : &it->second;
    else {
        const Pcm& stored = (impl_->sfxCache[key] = loadAudioPreferFlac(vfs, "data/wav/", key));
        pcm = stored.empty() ? nullptr : &stored;
    }
    if (!pcm) return;
    const float g = std::clamp(gain, 0.0f, 4.0f) * impl_->sfxVol * impl_->masterVol *
                    std::clamp(atten, 0.0f, 1.0f);
    Impl::Voice voice;
    voice.pcm = pcm->f32;  // own copy; moving the vector into `voices` preserves the buffer pointer
    IXAudio2SourceVoice* v = impl_->startVoice(pcm->channels, pcm->freq, voice.pcm.data(),
                                               voice.pcm.size(), g, /*loop=*/false);
    if (!v) return;
    voice.v = v;
    impl_->voices.push_back(std::move(voice));
}

void Audio::warmSfx(const Vfs& vfs, const std::string& name) {
    if (!impl_->ready) return;
    std::string key = name;
    for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (impl_->sfxCache.count(key)) return;
    impl_->sfxCache[key] = loadAudioPreferFlac(vfs, "data/wav/", key);  // decode + cache (miss too)
}

void Audio::playBgm(const Vfs& vfs, const std::string& path) {
    if (!impl_->ready) return;
    if (impl_->bgmVoice && path == impl_->bgmPath) return;  // same track already looping
    stopBgm();
    if (path.empty()) return;
    impl_->bgmPath = path;
    // Prefer a same-named .flac over the .mp3 (S.: all audio re-packed as FLAC in the ZIP).
    Pcm p;
    {
        std::string flacPath = path;
        if (const usize dot = flacPath.find_last_of('.'); dot != std::string::npos) flacPath.resize(dot);
        flacPath += ".flac";
        if (flacPath != path)
            if (auto fb = vfs.readQuiet(flacPath); fb && !fb->empty()) p = decodeFlac(*fb);
    }
    if (p.empty()) {
        auto bytes = vfs.read(path);
        if (!bytes) { log::info("audio: BGM not found: {}", path); return; }
        if (bytes->size() >= 4 && (*bytes)[0] == 'f' && (*bytes)[1] == 'L' && (*bytes)[2] == 'a' &&
            (*bytes)[3] == 'C')
            p = decodeFlac(*bytes);
        else
            p = decodeMp3(*bytes);
    }
    if (p.empty()) { log::warn("audio: BGM decode failed: {}", path); return; }
    const int ch = p.channels, fr = p.freq;
    impl_->bgmPcm = std::move(p.f32);
    const float g = (impl_->bgmOn ? impl_->bgmVol : 0.0f) * impl_->masterVol;
    impl_->bgmVoice = impl_->startVoice(ch, fr, impl_->bgmPcm.data(), impl_->bgmPcm.size(), g,
                                        /*loop=*/true);
    if (!impl_->bgmVoice) impl_->bgmPcm.clear();
}

void Audio::stopBgm() {
    if (!impl_->ready) return;
    if (impl_->bgmVoice) { impl_->bgmVoice->DestroyVoice(); impl_->bgmVoice = nullptr; }
    impl_->bgmPcm.clear();  // DestroyVoice blocks until the voice leaves the graph, so this is safe
    impl_->bgmPath.clear();
}

void Audio::clearAmbients() {
    if (!impl_->ready) return;
    for (auto& a : impl_->ambients)
        if (a.v) a.v->DestroyVoice();
    impl_->ambients.clear();
}

void Audio::setAmbients(const Vfs& vfs, const std::vector<AmbientSound>& list) {
    if (!impl_->ready) return;
    clearAmbients();
    for (const AmbientSound& s : list) {
        Impl::Amb a;
        a.pos = s.pos;
        a.range = s.range > 1.0f ? s.range : 30.0f;
        a.volume = s.volume;
        a.pcm = loadAudioPreferFlac(vfs, "data/wav/", s.wav);
        if (!a.pcm.empty()) impl_->ambients.push_back(std::move(a));
    }
}

void Audio::updateListener(const Vec3& listenerPos) {
    if (!impl_->ready) return;
    impl_->listener = listenerPos;
    const float sfxBus = impl_->sfxOn ? impl_->sfxVol : 0.0f;
    for (auto& a : impl_->ambients) {
        const float dx = a.pos.x - listenerPos.x, dz = a.pos.z - listenerPos.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        const float atten = d >= a.range ? 0.0f : 1.0f - d / a.range;
        if (atten <= 0.0f) {  // out of range -> stop & free the loop
            if (a.v) { a.v->DestroyVoice(); a.v = nullptr; }
            continue;
        }
        const float vol = std::clamp(a.volume * atten * sfxBus * impl_->masterVol, 0.0f, 1.0f);
        if (!a.v)  // entered range -> (re)start the loop
            a.v = impl_->startVoice(a.pcm.channels, a.pcm.freq, a.pcm.f32.data(), a.pcm.f32.size(),
                                    vol, /*loop=*/true);
        else
            a.v->SetVolume(vol);
    }
}

void Audio::setMasterVolume(float v) {
    impl_->masterVol = std::clamp(v, 0.0f, 1.0f);
    if (impl_->ready && impl_->bgmVoice)  // BGM re-applied live; SFX/ambients pick it up next frame
        impl_->bgmVoice->SetVolume((impl_->bgmOn ? impl_->bgmVol : 0.0f) * impl_->masterVol);
}
void Audio::setSfxVolume(float v) { impl_->sfxVol = std::clamp(v, 0.0f, 1.0f); }
void Audio::setSfxOn(bool on) { impl_->sfxOn = on; }
void Audio::setBgmVolume(float v) {
    impl_->bgmVol = std::clamp(v, 0.0f, 1.0f);
    if (impl_->ready && impl_->bgmVoice)
        impl_->bgmVoice->SetVolume((impl_->bgmOn ? impl_->bgmVol : 0.0f) * impl_->masterVol);
}
void Audio::setBgmOn(bool on) {
    impl_->bgmOn = on;
    if (impl_->ready && impl_->bgmVoice)
        impl_->bgmVoice->SetVolume((on ? impl_->bgmVol : 0.0f) * impl_->masterVol);
}

#elif !defined(CLIENT_WITH_AUDIO)
// ---- Silent stub (built without audio) -----------------------------------------------------
bool Audio::init() {
    log::info("audio: built without audio support (silent)");
    return false;
}
void Audio::shutdown() {}
void Audio::update() {}
void Audio::playSfx(const Vfs&, const std::string&, float, float, float) {}
void Audio::warmSfx(const Vfs&, const std::string&) {}
void Audio::playBgm(const Vfs&, const std::string&) {}
void Audio::stopBgm() {}
void Audio::setAmbients(const Vfs&, const std::vector<AmbientSound>&) {}
void Audio::updateListener(const Vec3&) {}
void Audio::clearAmbients() {}
void Audio::setMasterVolume(float) {}
void Audio::setSfxVolume(float) {}
void Audio::setSfxOn(bool) {}
void Audio::setBgmVolume(float) {}
void Audio::setBgmOn(bool) {}

#else
// ---- SDL3 core-audio backend ---------------------------------------------------------------
bool Audio::init() {
    if (impl_->ready) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log::warn("audio: SDL_INIT_AUDIO failed: {}", SDL_GetError());
        return false;
    }
    impl_->dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (impl_->dev == 0) {
        log::warn("audio: SDL_OpenAudioDevice failed: {}", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    if (!SDL_GetAudioDeviceFormat(impl_->dev, &impl_->devSpec, nullptr)) {
        // Fall back to a sane default the converter can target.
        impl_->devSpec.format = SDL_AUDIO_F32;
        impl_->devSpec.channels = 2;
        impl_->devSpec.freq = 48000;
    }
    impl_->ready = true;
    log::info("audio: SDL3 ready ({} ch @ {} Hz)", impl_->devSpec.channels, impl_->devSpec.freq);
    return true;
}

void Audio::update() {
    if (!impl_->ready) return;
    // Keep the looping BGM topped up so it never runs dry (works in every scene, incl. login).
    if (impl_->bgmStream && !impl_->bgmPcm.empty())
        Impl::topUp(impl_->bgmStream, impl_->bgmPcm.data(),
                    static_cast<int>(impl_->bgmPcm.size() * sizeof(short)));
    impl_->reap();  // drop finished one-shot SFX voices
}

void Audio::clearSfxCache() {
    // Drop the decoded-SFX clips (map-server disconnect, #cache). BGM streams separately and is
    // unaffected. Clips re-decode lazily on the next playSfx.
    if (impl_) impl_->sfxCache.clear();
}

void Audio::shutdown() {
    if (!impl_ || !impl_->ready) return;
    stopBgm();
    clearAmbients();
    for (SDL_AudioStream* s : impl_->voices) SDL_DestroyAudioStream(s);
    impl_->voices.clear();
    impl_->sfxCache.clear();
    SDL_CloseAudioDevice(impl_->dev);
    impl_->dev = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    impl_->ready = false;
}

void Audio::playSfx(const Vfs& vfs, const std::string& name, float gain, float /*pan*/, float atten) {
    if (!impl_->ready || !impl_->sfxOn || impl_->sfxVol <= 0.0f) return;
    impl_->reap();
    const Pcm* pcm = impl_->loadSfx(vfs, name);
    if (!pcm) return;
    SDL_AudioStream* s = impl_->makeBound(pcm->spec);
    if (!s) return;
    const float g = std::clamp(gain, 0.0f, 4.0f) * impl_->sfxVol * impl_->masterVol * std::clamp(atten, 0.0f, 1.0f);
    SDL_SetAudioStreamGain(s, g);
    SDL_PutAudioStreamData(s, pcm->f32.data(), static_cast<int>(pcm->f32.size() * sizeof(float)));
    SDL_FlushAudioStream(s);  // one-shot: no more data is coming, so let it drain to the end
    impl_->voices.push_back(s);
}

void Audio::warmSfx(const Vfs& vfs, const std::string& name) {
    if (!impl_->ready) return;
    impl_->loadSfx(vfs, name);  // decode + cache (or negative-cache a miss); no playback
}

void Audio::playBgm(const Vfs& vfs, const std::string& path) {
    if (!impl_->ready) return;
    // Already playing this exact track? Keep it going (don't restart on a scene change that
    // shares the same BGM, e.g. login -> char-select both on new_zone01's theme).
    if (impl_->bgmStream && path == impl_->bgmPath) return;
    stopBgm();
    if (path.empty()) return;
    impl_->bgmPath = path;
    // Prefer a same-named .flac over the .mp3 (S.: all audio re-packed as FLAC in the ZIP). dr_flac
    // decodes straight to S16, matching the BGM pipeline. Falls back to the mp3 decoder on a miss.
    bool decoded = false;
    {
        std::string flacPath = path;
        if (const usize dot = flacPath.find_last_of('.'); dot != std::string::npos) flacPath.resize(dot);
        flacPath += ".flac";
        if (flacPath != path)
            if (auto fb = vfs.readQuiet(flacPath); fb && !fb->empty()) {
                unsigned int ch = 0, fr = 0;
                drflac_uint64 fc = 0;
                drflac_int16* fp = drflac_open_memory_and_read_pcm_frames_s16(
                    fb->data(), fb->size(), &ch, &fr, &fc, nullptr);
                if (fp && ch && fr) {
                    impl_->bgmPcm.assign(fp, fp + fc * ch);
                    impl_->bgmSpec.format = SDL_AUDIO_S16;
                    impl_->bgmSpec.channels = static_cast<int>(ch);
                    impl_->bgmSpec.freq = static_cast<int>(fr);
                    decoded = true;
                }
                if (fp) drflac_free(fp, nullptr);
            }
    }
    if (!decoded) {
        auto bytes = vfs.read(path);
        if (!bytes) {
            log::info("audio: BGM not found: {}", path);
            return;
        }
        drmp3_config cfg{};
        drmp3_uint64 frames = 0;
        drmp3_int16* pcm = drmp3_open_memory_and_read_pcm_frames_s16(bytes->data(), bytes->size(),
                                                                     &cfg, &frames, nullptr);
        if (!pcm || cfg.channels == 0 || cfg.sampleRate == 0) {
            if (pcm) drmp3_free(pcm, nullptr);
            log::warn("audio: BGM decode failed: {}", path);
            return;
        }
        impl_->bgmPcm.assign(pcm, pcm + frames * cfg.channels);
        drmp3_free(pcm, nullptr);
        impl_->bgmSpec.format = SDL_AUDIO_S16;
        impl_->bgmSpec.channels = static_cast<int>(cfg.channels);
        impl_->bgmSpec.freq = static_cast<int>(cfg.sampleRate);
    }
    impl_->bgmStream = impl_->makeBound(impl_->bgmSpec);
    if (!impl_->bgmStream) { impl_->bgmPcm.clear(); return; }
    SDL_SetAudioStreamGain(impl_->bgmStream, (impl_->bgmOn ? impl_->bgmVol : 0.0f) * impl_->masterVol);
    SDL_PutAudioStreamData(impl_->bgmStream, impl_->bgmPcm.data(),
                           static_cast<int>(impl_->bgmPcm.size() * sizeof(short)));
}

void Audio::stopBgm() {
    if (!impl_->ready) return;
    if (impl_->bgmStream) {
        SDL_DestroyAudioStream(impl_->bgmStream);  // unbinds
        impl_->bgmStream = nullptr;
    }
    impl_->bgmPcm.clear();
    impl_->bgmPath.clear();
}

void Audio::clearAmbients() {
    if (!impl_->ready) return;
    for (auto& a : impl_->ambients)
        if (a.stream) SDL_DestroyAudioStream(a.stream);
    impl_->ambients.clear();
}

void Audio::setAmbients(const Vfs& vfs, const std::vector<AmbientSound>& list) {
    if (!impl_->ready) return;
    clearAmbients();
    for (const AmbientSound& s : list) {
        Impl::Amb a;
        a.pos = s.pos;
        a.range = s.range > 1.0f ? s.range : 30.0f;
        a.volume = s.volume;
        a.pcm = loadAudioPreferFlac(vfs, "data/wav/", s.wav);  // .flac in priority over .wav (S.)
        if (!a.pcm.f32.empty()) impl_->ambients.push_back(std::move(a));
    }
}

void Audio::updateListener(const Vec3& listenerPos) {
    if (!impl_->ready) return;
    impl_->listener = listenerPos;
    // (BGM looping + voice reaping are handled by Audio::update(), called every main-loop frame.)
    const float sfxBus = impl_->sfxOn ? impl_->sfxVol : 0.0f;
    for (auto& a : impl_->ambients) {
        const float dx = a.pos.x - listenerPos.x, dz = a.pos.z - listenerPos.z;
        const float d = std::sqrt(dx * dx + dz * dz);
        const float atten = d >= a.range ? 0.0f : 1.0f - d / a.range;
        if (atten <= 0.0f) {  // out of range -> stop & free the loop
            if (a.stream) { SDL_DestroyAudioStream(a.stream); a.stream = nullptr; }
            continue;
        }
        if (!a.stream) {  // entered range -> (re)start the loop
            a.stream = impl_->makeBound(a.pcm.spec);
            if (a.stream)
                SDL_PutAudioStreamData(a.stream, a.pcm.f32.data(),
                                       static_cast<int>(a.pcm.f32.size() * sizeof(float)));
        }
        if (!a.stream) continue;
        SDL_SetAudioStreamGain(a.stream, std::clamp(a.volume * atten * sfxBus * impl_->masterVol, 0.0f, 1.0f));
        Impl::topUp(a.stream, a.pcm.f32.data(), static_cast<int>(a.pcm.f32.size() * sizeof(float)));
    }
}

void Audio::setMasterVolume(float v) {
    impl_->masterVol = std::clamp(v, 0.0f, 1.0f);
    if (impl_->ready && impl_->bgmStream)  // BGM gain re-applied live; SFX picks it up on next play
        SDL_SetAudioStreamGain(impl_->bgmStream, (impl_->bgmOn ? impl_->bgmVol : 0.0f) * impl_->masterVol);
}
void Audio::setSfxVolume(float v) { impl_->sfxVol = std::clamp(v, 0.0f, 1.0f); }
void Audio::setSfxOn(bool on) { impl_->sfxOn = on; }
void Audio::setBgmVolume(float v) {
    impl_->bgmVol = std::clamp(v, 0.0f, 1.0f);
    if (impl_->ready && impl_->bgmStream)
        SDL_SetAudioStreamGain(impl_->bgmStream, (impl_->bgmOn ? impl_->bgmVol : 0.0f) * impl_->masterVol);
}
void Audio::setBgmOn(bool on) {
    impl_->bgmOn = on;
    if (impl_->ready && impl_->bgmStream)
        SDL_SetAudioStreamGain(impl_->bgmStream, (on ? impl_->bgmVol : 0.0f) * impl_->masterVol);
}
#endif

} // namespace uaro
