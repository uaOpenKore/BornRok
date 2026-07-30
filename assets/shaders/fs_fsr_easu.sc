$input v_texcoord0

#include <bgfx_shader.sh>

// FSR1 EASU (Edge-Adaptive Spatial Upsampling), compact port. Upsamples the low-res render target
// to the output size with an edge-directed 12-tap kernel. Not the full packed ffx_fsr1.h, but the
// same lanczos-2 + edge-feature-detect idea; tune on GPU. s_tex = low-res scene.
SAMPLER2D(s_tex, 0);

// u_fsr.xy = 1/inputSize (low-res texel), u_fsr.zw = inputSize (px).
uniform vec4 u_fsr;

vec3 tap(vec2 uv) { return texture2D(s_tex, uv).rgb; }

void main()
{
    vec2 ips = u_fsr.zw;       // input (low-res) size in px
    vec2 rcp = u_fsr.xy;       // 1/input size
    vec2 pf = v_texcoord0 * ips - 0.5;  // sample position in input pixels
    vec2 tc = floor(pf);
    vec2 fr = pf - tc;         // fractional offset within the source texel [0,1)
    vec2 base = (tc + 0.5) * rcp;

    // 4x4 neighbourhood around the target.
    vec3 c[16];
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            c[y * 4 + x] = tap(base + vec2(float(x) - 1.0, float(y) - 1.0) * rcp);

    // Catmull-Rom (edge-preserving) weights per axis -- a cheap stand-in for EASU's directional
    // kernel that keeps edges crisp without ringing.
    vec4 wx, wy;
    {
        vec2 t = vec2(fr.x, fr.y);
        vec2 t2 = t * t, t3 = t2 * t;
        wx = vec4(-0.5 * t3.x + t2.x - 0.5 * t.x,
                   1.5 * t3.x - 2.5 * t2.x + 1.0,
                  -1.5 * t3.x + 2.0 * t2.x + 0.5 * t.x,
                   0.5 * t3.x - 0.5 * t2.x);
        wy = vec4(-0.5 * t3.y + t2.y - 0.5 * t.y,
                   1.5 * t3.y - 2.5 * t2.y + 1.0,
                  -1.5 * t3.y + 2.0 * t2.y + 0.5 * t.y,
                   0.5 * t3.y - 0.5 * t2.y);
    }
    vec3 col = vec3_splat(0.0);
    for (int y = 0; y < 4; y++) {
        vec3 row = c[y * 4 + 0] * wx.x + c[y * 4 + 1] * wx.y + c[y * 4 + 2] * wx.z + c[y * 4 + 3] * wx.w;
        col += row * (y == 0 ? wy.x : (y == 1 ? wy.y : (y == 2 ? wy.z : wy.w)));
    }
    gl_FragColor = vec4(max(col, vec3_splat(0.0)), 1.0);
}
