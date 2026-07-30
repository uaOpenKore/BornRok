$input v_texcoord0, v_normal, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

// xyz = the map sun direction (world), w unused. Simple lambert + ambient so the RoM
// toon-styled textures read naturally in RO's lighting.
uniform vec4 u_lightDir;
uniform vec4 u_ambient;
uniform vec4 u_lightColor;

void main()
{
    // Opaque models — no alpha discard: on backends without native ASTC a fallback texture
    // must never be able to erase the whole mesh via bad alpha.
    vec4 t = texture2D(s_tex, v_texcoord0);
    float ndl = max(dot(normalize(v_normal), u_lightDir.xyz), 0.0);
    vec3 lit = u_ambient.rgb + u_lightColor.rgb * ndl;
    gl_FragColor = vec4(t.rgb * clamp(lit, 0.0, 1.3), 1.0);
}
