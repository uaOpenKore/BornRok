$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// Animated water surface with optional reflections (#water). One program drives all four
// quality modes (u_waterRefl.x): 0 = Off (classic translucent texture, byte-for-byte the old
// look), 1 = Light (blend a sky/ambient colour in -> cheap reflective sheen, no extra pass),
// 2 = SSR / 3 = Planar (sample a planar-reflection texture rendered from the mirror camera).
// Modes 2/3 differ only on the CPU side (reflection resolution + strength/ripple); the shader
// path is the same. s_refl is ALWAYS bound (DX11 requires every declared sampler set) -- in
// modes 0/1 it just isn't read.
SAMPLER2D(s_tex,  0);  // current water animation frame (opaque JPG/PNG)
SAMPLER2D(s_refl, 1);  // planar reflection colour (modes 2/3)

uniform vec4 u_spriteFade;  // x = surface alpha (translucency)
uniform vec4 u_waterRefl;   // x = mode(0..3), y = reflectivity 0..1, z = flipReflY, w = ripple amount
uniform vec4 u_waterSky;    // rgb = sky/ambient reflection colour (Light tier)

void main()
{
    vec4 t = texture2D(s_tex, v_texcoord0);
    vec3 base = t.rgb * v_color0.rgb;
    float alpha = u_spriteFade.x * v_color0.a;

    int mode = int(u_waterRefl.x + 0.5);
    if (mode == 0) {
        // Off: identical to the classic fs_sprite3d water path (no cutout -- water texels are opaque).
        gl_FragColor = vec4(base, alpha);
        return;
    }

    if (mode == 1) {
        // Light: fold a sky/ambient colour into the surface for a cheap reflective tint. The water
        // texture's own luma gives a subtle animated sheen so it isn't a flat wash.
        float sheen = dot(t.rgb, vec3(0.299, 0.587, 0.114));
        vec3 sky = u_waterSky.rgb * (0.7 + 0.3 * sheen);
        vec3 col = mix(base, sky, clamp(u_waterRefl.y, 0.0, 1.0));
        gl_FragColor = vec4(col, min(1.0, alpha + 0.15 * u_waterRefl.y));
        return;
    }

    // SSR / Planar: the reflection texture was rendered with the mirror camera + the SAME
    // projection, so this fragment's own screen position indexes the reflected geometry.
    vec2 suv = gl_FragCoord.xy / u_viewRect.zw;
    if (u_waterRefl.z > 0.5) suv.y = 1.0 - suv.y;  // RT origin flip (CPU passes caps.originBottomLeft)
    // Ripple: perturb the lookup by the water texture's deviation from grey so the mirror shimmers.
    vec2 ripple = (t.rg - vec2(0.5, 0.5)) * (u_waterRefl.w);
    vec3 refl = texture2D(s_refl, clamp(suv + ripple, vec2(0.0, 0.0), vec2(1.0, 1.0))).rgb;
    // Keep a little of the water's own colour so it still reads as water, not a mirror.
    vec3 col = mix(base, refl, clamp(u_waterRefl.y, 0.0, 1.0));
    gl_FragColor = vec4(col, min(1.0, alpha + 0.2 * u_waterRefl.y));
}
