$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);  // the HDR (RGBA16F) scene target

// u_tonemap.x = exposure. ACES-ish filmic tonemap of the linear HDR scene down to the display,
// then gamma. Gives smooth highlights + no 8-bit banding on gradients vs. rendering straight to LDR.
uniform vec4 u_tonemap;
// u_grade.x = brightness (0.5 neutral), .y = contrast (0.5 neutral). Applied after tonemapping.
uniform vec4 u_grade;

vec3 aces(vec3 x)
{
    // Narkowicz 2015 ACES approximation.
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdr = texture2D(s_tex, v_texcoord0).rgb * u_tonemap.x;
    vec3 mapped = aces(hdr);
    mapped = clamp((mapped - 0.5) * (u_grade.y * 2.0) + 0.5 + (u_grade.x - 0.5), 0.0, 1.0);
    gl_FragColor = vec4(mapped, 1.0);
}
