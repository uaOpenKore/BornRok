#include "world/MapLoader.hpp"

#include <map>

#include "core/Log.hpp"
#include "formats/ImageIO.hpp"

namespace uaro {

std::optional<MapData> MapLoader::load(Vfs& vfs, const std::string& mapName) {
    MapData m;
    m.name = mapName;

    auto rswBytes = vfs.read("data/" + mapName + ".rsw");
    if (!rswBytes) {
        log::error("map: {}.rsw not found", mapName);
        return std::nullopt;
    }
    auto rsw = Rsw::parse(*rswBytes);
    if (!rsw) return std::nullopt;
    m.rsw = std::move(*rsw);
    {
        // Dark-map lift (S. live-QA, final: "верни на общий, просто +10% а не +25"):
        // authored-dark maps (sewb1 amb .2/dif .4) read brighter in the official client.
        // Lift BOTH terms 10% when the overall level is low; consumers clamp to 1, so
        // bright maps are untouched.
        RswLight& L = m.rsw.light();
        const float amb = (L.ambient[0] + L.ambient[1] + L.ambient[2]) / 3.0f;
        const float lum = amb + (L.diffuse[0] + L.diffuse[1] + L.diffuse[2]) / 3.0f;
        // Skip high-ambient interiors: prt_in is authored at amb ~0.87 with tiny diffuse
        // (lum 1.05) and the lift blew it out (S.: "в локациях _in очень ярко").
        if (lum < 1.2f && amb < 0.6f) {
            for (float& c : L.ambient) c *= 1.10f;
            for (float& c : L.diffuse) c *= 1.10f;
        }
    }

    auto gndBytes = vfs.read("data/" + m.rsw.gndFile());
    if (!gndBytes) {
        log::error("map: GND '{}' not found", m.rsw.gndFile());
        return std::nullopt;
    }
    auto gnd = Gnd::parse(*gndBytes);
    if (!gnd) return std::nullopt;
    m.gnd = std::move(*gnd);

    // The GAT the client loads for a map is NOT chosen by the server or any table -- it is the filename
    // written inside this map's own .rsw header (m.rsw.gatFile()). The server sends only the map NAME; the
    // client opens data/<name>.rsw and follows the .gnd/.gat names it declares. If that GAT isn't in the
    // GRF the map has no collision data (can't walk) -- log the exact name so a content/server mismatch is
    // visible (S.: "сервер ссылается на неправильный гат, которого в клиенте нету").
    if (auto gatBytes = vfs.read("data/" + m.rsw.gatFile())) m.gat = Gat::parse(*gatBytes);
    else log::error("map '{}': GAT '{}' (named by {}.rsw) not found in GRF -- no collision map",
                    mapName, m.rsw.gatFile(), mapName);

    m.ground = GroundMesh::build(m.gnd);

    // Ground textures live under data/texture/.
    m.textures.reserve(m.gnd.textures().size());
    for (const auto& name : m.gnd.textures()) {
        // Prefer a hi-res .png sibling over the .bmp GND texture (S.); decodeImage sniffs the format.
        const std::string tpath = "data/texture/" + name;
        // Log WHERE each ground texture resolves (loose folder / which GRF / png override / MISSING) so a
        // content maker can tell if their override actually won (S.: "с какой грф он берёт или с папки").
        log::debug("map texture: {} <- {}", tpath, vfs.whence(tpath));  // per-texture diag -> debug (S.: не спамить лог)
        if (auto bytes = vfs.readPreferPng(tpath))
            m.textures.push_back(decodeImage(*bytes));  // BMP/PNG/TGA/JPG by magic
        else
            m.textures.push_back(std::nullopt);
    }

    // Models live under data/model/, deduplicated by filename.
    std::map<std::string, int> cache;
    m.placements.reserve(m.rsw.models().size());
    for (const auto& obj : m.rsw.models()) {
        int idx = -1;
        auto it = cache.find(obj.filename);
        if (it != cache.end()) {
            idx = it->second;
        } else {
            if (auto bytes = vfs.read("data/model/" + obj.filename)) {
                if (auto rsm = Rsm::parse(*bytes)) {
                    idx = static_cast<int>(m.rsmCache.size());
                    m.rsmCache.push_back(std::move(*rsm));
                } else {
                    // Parsed-but-rejected models silently vanished (fountains/banners/lamps not
                    // showing, #31); log which RSM the parser refused so it can be pinpointed.
                    log::warn("map {}: model '{}' failed to parse (RSM rejected)", mapName,
                              obj.filename);
                }
            } else {
                log::warn("map {}: model '{}' not found under data/model/", mapName, obj.filename);
            }
            cache.emplace(obj.filename, idx);
        }
        m.placements.emplace_back(obj, idx);
    }

    log::info("map {}: ground {} verts, {} textures, {} placements ({} unique models)",
              mapName, m.ground.vertices.size(), m.textures.size(), m.placements.size(),
              m.rsmCache.size());
    return m;
}

} // namespace uaro
