$input v_texcoord0

#include <bgfx_shader.sh>

// Volumetric light "Rays" mode (#117 B v2): the sun radial shafts (same as fs_godray) PLUS a
// screen-space radial shaft from each local light — the glow cores drawn into the scene are smeared
// toward each light's screen position, tinted by the light colour. Reuses the proven sun-shaft math;
// no depth reconstruction. Runs as the final composite, so it also folds the grade.
#define MAX_VOL_LIGHTS 16
SAMPLER2D(s_tex, 0);
uniform vec4 u_godray;    // xy = sun screen uv, z = sun intensity (0 when off-screen), w = decay
uniform vec4 u_godray2;   // x = density, y = per-sample weight, z = bright threshold
uniform vec4 u_grade;     // x = brightness (0.5), y = contrast (0.5)
uniform vec4 u_volParams; // x = local light count, y = local intensity, z = local density, w = local decay
uniform vec4 u_lightScr[MAX_VOL_LIGHTS];  // xy = screen uv, z = visible (1/0)
uniform vec4 u_lightCol[MAX_VOL_LIGHTS];  // rgb tint

vec3 applyGrade(vec3 c, vec2 g) {
    float bd = (g.x - 0.5);
    float cf = g.y * 2.0;
    return clamp((c - 0.5) * cf + 0.5 + bd, 0.0, 1.0);
}

// Radial accumulation of bright texels from `uv` toward `src`, `steps` samples.
vec3 shaft(vec2 uv, vec2 src, float density, float weight, float threshold, float decay) {
    const int STEPS = 32;
    vec2 delta = (uv - src) * (density / float(STEPS));
    vec2 coord = uv;
    float illum = 1.0;
    vec3 acc = vec3_splat(0.0);
    for (int i = 0; i < STEPS; ++i) {
        coord -= delta;
        vec3 s = texture2D(s_tex, coord).rgb;
        float lum = dot(s, vec3(0.299, 0.587, 0.114));
        s *= smoothstep(threshold, 1.0, lum);
        acc += s * illum * weight;
        illum *= decay;
    }
    return acc;
}

void main()
{
    vec2 uv = v_texcoord0;
    vec3 base = texture2D(s_tex, uv).rgb;
    vec3 col = base;

    // Sun shafts (skipped when intensity 0 / sun off-screen).
    if (u_godray.z > 0.0)
        col += shaft(uv, u_godray.xy, u_godray2.x, u_godray2.y, u_godray2.z, u_godray.w) * u_godray.z;

    // Local light shafts: smear the glow core toward each on-screen light, tint by its colour.
    int nL = int(u_volParams.x);
    for (int l = 0; l < MAX_VOL_LIGHTS; ++l) {
        if (l >= nL) break;
        if (u_lightScr[l].z < 0.5) continue;
        vec3 a = shaft(uv, u_lightScr[l].xy, u_volParams.z, 1.0, 0.2, u_volParams.w);
        col += a * u_lightCol[l].rgb * u_volParams.y;
    }

    gl_FragColor = vec4(applyGrade(col, u_grade.xy), 1.0);
}
