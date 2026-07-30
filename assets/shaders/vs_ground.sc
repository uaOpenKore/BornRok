$input a_position, a_normal, a_texcoord0, a_color0
$output v_texcoord0, v_color0, v_lightcoord, v_wnormal, v_wtangent

#include <bgfx_shader.sh>

// x = GND width (cells), y = GND height (cells). Used to turn the world XZ back into
// the [0,1] lightmap UV.
uniform vec4 u_mapDim;

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
    // The ground is built X-mirrored (worldX = width - cellX, kTile = 1) and worldZ =
    // cellZ, so recover the lightmap UV (stored in original orientation) straight from
    // the world position — no extra vertex attribute needed.
    v_lightcoord = vec2(1.0 - a_position.x / u_mapDim.x, a_position.z / u_mapDim.y);

    // Tangent frame for optional per-pixel normal mapping (#107). The ground mesh is
    // already in world space (model matrix is identity), and it carries a per-face
    // normal but NO tangent, so derive an orthonormal tangent from the normal + a fixed
    // world axis. Ground top-tiles face up (-> tangent = world Z) and walls face
    // sideways (-> a valid horizontal tangent); the exact roll is irrelevant because the
    // normal-map effect is applied as a bump RELATIVE to the geometric normal in fs.
    vec3 N = normalize(a_normal);
    vec3 ref = (abs(N.y) > 0.99) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    v_wnormal = N;
    v_wtangent = normalize(cross(ref, N));
}
