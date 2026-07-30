$input v_texcoord0

#include <bgfx_shader.sh>

// Standalone brightness/contrast grade pass (used when neither HDR nor FSR is on, so the scene still
// goes through an offscreen and real contrast -- including INCREASE -- is possible; the old overlay
// could only darken/lighten/desaturate). u_grade.x = brightness (0.5 neutral), .y = contrast (0.5).
SAMPLER2D(s_tex, 0);
uniform vec4 u_grade;

vec3 applyGrade(vec3 c, vec2 g) {
    float bd = (g.x - 0.5) * 1.0;  // brightness delta [-0.5..+0.5]
    float cf = g.y * 2.0;          // contrast factor: 0.5 -> 1.0, 0 -> flat grey, 1 -> 2x
    return clamp((c - 0.5) * cf + 0.5 + bd, 0.0, 1.0);
}

void main()
{
    vec3 c = texture2D(s_tex, v_texcoord0).rgb;
    gl_FragColor = vec4(applyGrade(c, u_grade.xy), 1.0);
}
