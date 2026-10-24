$input v_texcoord0

#include <bgfx_shader.sh>

// Bloom separable Gaussian blur (#bloom): nine taps along one axis. u_bloom.zw is the per-tap UV step
// (texel size times the axis direction), so the same shader does the horizontal and the vertical pass.
SAMPLER2D(s_tex, 0);
uniform vec4 u_bloom;  // zw = per-tap UV step (1/w or 1/h on the active axis, 0 on the other)

void main()
{
	vec2 d = u_bloom.zw;
	vec3 sum = texture2D(s_tex, v_texcoord0).rgb * 0.2270270270;
	sum += texture2D(s_tex, v_texcoord0 + d * 1.3846153846).rgb * 0.3162162162;
	sum += texture2D(s_tex, v_texcoord0 - d * 1.3846153846).rgb * 0.3162162162;
	sum += texture2D(s_tex, v_texcoord0 + d * 3.2307692308).rgb * 0.0702702703;
	sum += texture2D(s_tex, v_texcoord0 - d * 3.2307692308).rgb * 0.0702702703;
	gl_FragColor = vec4(sum, 1.0);
}
