$input v_texcoord0

#include <bgfx_shader.sh>

// FSR1 RCAS (Robust Contrast-Adaptive Sharpening), compact port. Runs at output resolution on the
// EASU-upscaled image to restore crispness. u_fsr.xy = 1/outputSize; u_fsr.z = sharpness (0..1,
// higher = sharper).
SAMPLER2D(s_tex, 0);
uniform vec4 u_fsr;
// u_grade.x = brightness (0.5 neutral), .y = contrast (0.5 neutral). Applied after sharpening.
uniform vec4 u_grade;

vec3 ld(vec2 uv) { return texture2D(s_tex, uv).rgb; }

void main()
{
    vec2 rcp = u_fsr.xy;
    vec3 e = ld(v_texcoord0);
    vec3 n = ld(v_texcoord0 + vec2(0.0, -rcp.y));
    vec3 s = ld(v_texcoord0 + vec2(0.0,  rcp.y));
    vec3 w = ld(v_texcoord0 + vec2(-rcp.x, 0.0));
    vec3 ee = ld(v_texcoord0 + vec2(rcp.x, 0.0));

    // Contrast-adaptive: how much can we sharpen without ringing (min/max of the cross neighbourhood).
    vec3 mn = min(min(min(n, s), min(w, ee)), e);
    vec3 mx = max(max(max(n, s), max(w, ee)), e);
    vec3 rangeMin = mn;                 // headroom to darks
    vec3 rangeMax = vec3_splat(1.0) - mx;  // headroom to brights (assumes LDR 0..1)
    vec3 amp = clamp(min(rangeMin, rangeMax) / max(mx, vec3_splat(1e-4)), 0.0, 1.0);

    float sharp = u_fsr.z;
    // Unsharp mask: centre minus the 4-neighbour average, scaled by the adaptive amplitude.
    vec3 avg = (n + s + w + ee) * 0.25;
    vec3 outc = e + (e - avg) * (sharp * amp);
    outc = clamp((outc - 0.5) * (u_grade.y * 2.0) + 0.5 + (u_grade.x - 0.5), 0.0, 1.0);
    gl_FragColor = vec4(clamp(outc, 0.0, 1.0), 1.0);
}
