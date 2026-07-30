#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// Unity AnimationClip (class 74) decode for the RoM optimized-rig clips (Unity 2021.3):
// walks the ClipMuscleConstant (offsets verified byte-by-byte on the real 'wait' clip),
// decodes the StreamedClip / DenseClip / ConstantClip curve slots, and BAKES every slot
// to per-frame values at the clip's sample rate. Bindings map slot ranges to bones by
// the same path hashes the mesh/avatar matching uses (attribute 1 = position, 2 =
// quaternion rotation, 3 = scale, 4 = euler).
struct UnityAnimBinding {
    u32 pathHash = 0;   // avatar m_ID space
    u32 attribute = 0;  // 1 t, 2 q, 3 s, 4 euler
    u32 dim = 0;        // floats per frame (q = 4, others = 3)
    u32 firstSlot = 0;  // first curve slot of this binding
};

struct UnityAnimClipData {
    std::string name;       // "wait" / "walk" / "atk" / ...
    float sampleRate = 30;
    float duration = 0;     // seconds (m_StopTime - m_StartTime)
    u32 frameCount = 0;
    u32 slotCount = 0;      // total curve slots
    std::vector<UnityAnimBinding> bindings;
    std::vector<float> baked;  // frameCount x slotCount, row-major
};

std::optional<UnityAnimClipData> parseUnityAnimClip(const std::vector<u8>& objectBytes);

}  // namespace uaro
