$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

// Additive light glow (#117 part B): a soft radial falloff quad, tinted by the RSW point light's
// colour (carried in v_color0). Drawn additively so overlapping lights build up. Procedural — no
// texture. v_color0.a carries the master intensity (baked per-vertex on the CPU).
void main()
{
    float d = length(v_texcoord0 - vec2(0.5, 0.5)) * 2.0;  // 0 at centre -> 1 at the quad edge
    float f = clamp(1.0 - d, 0.0, 1.0);
    f = f * f;                                // soft quadratic falloff (round, not square)
    gl_FragColor = vec4(v_color0.rgb * f * v_color0.a, f);
}
