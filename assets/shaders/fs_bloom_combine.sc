$input v_texcoord0

#include <bgfx_shader.sh>

// Bloom composite (#bloom): the scene (s_tex) plus the blurred bright texture (s_bloom) scaled by the
// intensity, then the same brightness/contrast grade as fs_grade so bloom coexists with the grade pass
// (this path owns the final blit when bloom is on).
SAMPLER2D(s_tex, 0);
SAMPLER2D(s_bloom, 1);
uniform vec4 u_bloom;  // y = intensity
uniform vec4 u_grade;  // x = brightness (0.5 neutral), y = contrast (0.5 neutral)

vec3 applyGrade(vec3 c, vec2 g) {
	float bd = (g.x - 0.5) * 1.0;
	float cf = g.y * 2.0;
	return clamp((c - 0.5) * cf + 0.5 + bd, 0.0, 1.0);
}

void main()
{
	vec3 c = texture2D(s_tex, v_texcoord0).rgb;
	vec3 glow = texture2D(s_bloom, v_texcoord0).rgb;
	c = c + glow * u_bloom.y;
	gl_FragColor = vec4(applyGrade(c, u_grade.xy), 1.0);
}
