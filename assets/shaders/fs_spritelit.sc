$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);
SAMPLER2D(s_nrm, 1);  // per-frame normal map generated from the sprite's own luminance

// x = sprite alpha (corpse dissolve), same contract as fs_sprite3d.
uniform vec4 u_spriteFade;
// xyz = the map's sun direction transformed into BILLBOARD (tangent) space — x along the
// quad's right, y along its up, z toward the camera; w = relief strength (0 = byte-identical
// to fs_sprite3d, ~0.35 = subtle emboss). Uniform, not a varying, so this pair shares
// vs_sprite3d's varyings exactly (no DX11 link trap).
uniform vec4 u_spriteLight;

void main()
{
    vec4 t = texture2D(s_tex, v_texcoord0);
    if (t.a < 0.5)
        discard;
    // Tangent-space normal from the luminance bump; N.z points at the camera. The quad "up"
    // in the normal map is texture -v, hence the y flip.
    vec3 n = texture2D(s_nrm, v_texcoord0).xyz * 2.0 - 1.0;
    n.y = -n.y;
    float ndl = max(dot(normalize(n), u_spriteLight.xyz), 0.0);
    // Ambient 0.7 + diffuse 0.6 * N.L, blended by strength so 0 stays identity.
    float lit = mix(1.0, 0.7 + 0.6 * ndl, u_spriteLight.w);
    gl_FragColor = vec4(t.rgb * v_color0.rgb * lit, u_spriteFade.x * v_color0.a);
}
