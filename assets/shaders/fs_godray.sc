$input v_texcoord0

#include <bgfx_shader.sh>

// Screen-space volumetric light / "god rays" (#117). A radial blur of the bright (sky/sun) scene
// texels toward the sun's screen position (GPU Gems 3 "light shafts"), added over the scene. Runs
// as the final composite when god-rays are on (like fs_tonemap), so it also folds the
// brightness/contrast grade. Only bright texels above u_godray2.z contribute, so the shafts emanate
// from the light and the rest of the scene doesn't smear.
SAMPLER2D(s_tex, 0);
uniform vec4 u_godray;   // xy = sun screen UV, z = intensity, w = decay per step
uniform vec4 u_godray2;  // x = density, y = per-sample weight, z = bright threshold, w = unused
uniform vec4 u_grade;    // x = brightness (0.5 neutral), y = contrast (0.5 neutral)

vec3 applyGrade(vec3 c, vec2 g) {
    float bd = (g.x - 0.5);
    float cf = g.y * 2.0;
    return clamp((c - 0.5) * cf + 0.5 + bd, 0.0, 1.0);
}

void main()
{
    vec2 uv = v_texcoord0;
    vec3 base = texture2D(s_tex, uv).rgb;

    const int SAMPLES = 48;
    // Step from this pixel toward the sun; density spreads the shafts.
    vec2 delta = (uv - u_godray.xy) * (u_godray2.x / float(SAMPLES));
    vec2 coord = uv;
    float illum = 1.0;
    vec3 rays = vec3_splat(0.0);
    for (int i = 0; i < SAMPLES; ++i) {
        coord -= delta;
        vec3 s = texture2D(s_tex, coord).rgb;
        float lum = dot(s, vec3(0.299, 0.587, 0.114));
        s *= smoothstep(u_godray2.z, 1.0, lum);  // bright-pass: only sky/sun casts shafts
        rays += s * illum * u_godray2.y;
        illum *= u_godray.w;                     // exponential decay along the ray
    }
    vec3 col = base + rays * u_godray.z;
    gl_FragColor = vec4(applyGrade(col, u_grade.xy), 1.0);
}
