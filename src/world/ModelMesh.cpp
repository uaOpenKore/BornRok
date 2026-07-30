#include "world/ModelMesh.hpp"

#include <algorithm>
#include <cmath>

namespace uaro {

ModelMesh ModelMesh::build(const Rsm& rsm) {
    ModelMesh out;
    out.nodes.reserve(rsm.nodes().size());

    struct Tri {
        i32 tex;
        ModelVertex v[3];
    };

    for (usize ni = 0; ni < rsm.nodes().size(); ++ni) {
        const RsmNode& node = rsm.nodes()[ni];

        std::vector<Tri> tris;
        tris.reserve(node.faces.size());
        for (const RsmFace& f : node.faces) {
            i32 modelTex = -1;
            if (f.texId < node.textureIndices.size()) modelTex = node.textureIndices[f.texId];

            Tri t;
            t.tex = modelTex;
            for (int i = 0; i < 3; ++i) {
                ModelVertex mv{};
                const u16 vi = f.vertIdx[i];
                const u16 ti = f.tvertIdx[i];
                if (vi < node.vertices.size()) {
                    mv.x = node.vertices[vi][0];
                    mv.y = node.vertices[vi][1];
                    mv.z = node.vertices[vi][2];
                }
                if (ti < node.tvertices.size()) {
                    mv.u = node.tvertices[ti].u;
                    mv.v = node.tvertices[ti].v;
                    mv.color = node.tvertices[ti].color;
                } else {
                    mv.color = 0xffffffff;
                }
                t.v[i] = mv;
            }
            // Flat per-face normal + UV-gradient tangent, written to all 3 verts (geometry is
            // unshared, so flat normals are fine; the normal map supplies the fine detail). #107.
            const float e1x = t.v[1].x - t.v[0].x, e1y = t.v[1].y - t.v[0].y, e1z = t.v[1].z - t.v[0].z;
            const float e2x = t.v[2].x - t.v[0].x, e2y = t.v[2].y - t.v[0].y, e2z = t.v[2].z - t.v[0].z;
            float nx = e1y * e2z - e1z * e2y;
            float ny = e1z * e2x - e1x * e2z;
            float nz = e1x * e2y - e1y * e2x;
            float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nlen > 1e-8f) { nx /= nlen; ny /= nlen; nz /= nlen; }
            else { nx = 0.0f; ny = 1.0f; nz = 0.0f; }
            const float du1 = t.v[1].u - t.v[0].u, dv1 = t.v[1].v - t.v[0].v;
            const float du2 = t.v[2].u - t.v[0].u, dv2 = t.v[2].v - t.v[0].v;
            float tx, ty, tz;
            const float denom = du1 * dv2 - du2 * dv1;
            if (std::fabs(denom) > 1e-8f) {
                const float r = 1.0f / denom;
                tx = r * (dv2 * e1x - dv1 * e2x);
                ty = r * (dv2 * e1y - dv1 * e2y);
                tz = r * (dv2 * e1z - dv1 * e2z);
            } else {  // degenerate UVs: fall back to the first edge
                tx = e1x; ty = e1y; tz = e1z;
            }
            // Gram-Schmidt: make the tangent orthonormal to the normal.
            const float td = tx * nx + ty * ny + tz * nz;
            tx -= nx * td; ty -= ny * td; tz -= nz * td;
            float tlen = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (tlen > 1e-8f) { tx /= tlen; ty /= tlen; tz /= tlen; }
            else { tx = 1.0f; ty = 0.0f; tz = 0.0f; }
            for (int k = 0; k < 3; ++k) {
                t.v[k].nx = nx; t.v[k].ny = ny; t.v[k].nz = nz;
                t.v[k].tx = tx; t.v[k].ty = ty; t.v[k].tz = tz;
            }
            tris.push_back(t);
        }

        // Group triangles by texture into contiguous index batches.
        std::stable_sort(tris.begin(), tris.end(),
                         [](const Tri& a, const Tri& b) { return a.tex < b.tex; });

        ModelNodeMesh nm;
        nm.nodeIndex = static_cast<u32>(ni);
        if (!tris.empty()) {
            nm.vertices.reserve(tris.size() * 3);
            nm.indices.reserve(tris.size() * 3);
            ModelBatch batch{tris.front().tex, 0, 0};
            for (const Tri& t : tris) {
                if (t.tex != batch.textureId) {
                    nm.batches.push_back(batch);
                    batch = ModelBatch{t.tex, static_cast<u32>(nm.indices.size()), 0};
                }
                const u32 base = static_cast<u32>(nm.vertices.size());
                for (int i = 0; i < 3; ++i) nm.vertices.push_back(t.v[i]);
                nm.indices.push_back(base);
                nm.indices.push_back(base + 1);
                nm.indices.push_back(base + 2);
                batch.indexCount += 3;
            }
            nm.batches.push_back(batch);
        }
        out.nodes.push_back(std::move(nm));
    }
    return out;
}

} // namespace uaro
