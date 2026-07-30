#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/Types.hpp"

namespace uaro {

// UnityFS AssetBundle CONTAINER reader (#110 / feat/content-sources, ROeM). Parses the
// bundle header, the (LZ4/uncompressed) BlocksInfo, and reassembles the node payloads —
// the SerializedFiles and .resS/.resource blobs inside a .unity3d. The RoM bundles are
// stock UnityFS v8, Unity 2021.3.21f1, no custom crypto (verified on the real archive).
// Object-level parsing (Mesh/Texture2D/...) is the NEXT layer on top of the node bytes.
struct UnityFsNode {
    std::string path;  // e.g. "CAB-xxxx" (a SerializedFile) or "CAB-xxxx.resS" (raw resources)
    u64 offset = 0;    // into the decompressed data stream
    u64 size = 0;
    u32 flags = 0;     // bit 2 (0x4): the node is a SerializedFile
};

class UnityFsBundle {
public:
    // Parses the container and DECOMPRESSES the whole data stream up front (RoM bundles are
    // small, tens of KB..MB). Returns false on a non-UnityFS signature, an unsupported
    // compression (LZMA — not used by the RoM pack), or corrupt block data.
    bool parse(const std::vector<u8>& bytes);

    const std::string& unityVersion() const { return unityVersion_; }
    const std::vector<UnityFsNode>& nodes() const { return nodes_; }

    // The reassembled bytes of one node (by index into nodes()).
    std::optional<std::vector<u8>> nodeData(usize index) const;

private:
    std::string unityVersion_;
    std::vector<UnityFsNode> nodes_;
    std::vector<u8> data_;  // concatenated decompressed blocks
};

}  // namespace uaro
