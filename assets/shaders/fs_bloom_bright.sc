$input v_texcoord0

#include <bgfx_shader.sh>

// Bloom bright-pass (#bloom): 4-tap bilinear downsample of the scene, keeping only what is brighter than
// the threshold, with a soft knee so nothing pops on. Runs into a half-res target.
SAMPLER2D(s_tex, 0);
uniform vec4 u_bloom;  // x = threshold, y = intensity (used by combine), zw = source texel size (1/w,1/h)

void main()
{
	vec2 t = u_bloom.zw;
	vec3 s = texture2D(s_tex, v_texcoord0 + vec2(-t.x, -t.y)).rgb +
	         texture2D(s_tex, v_texcoord0 + vec2( t.x, -t.y)).rgb +
	         texture2D(s_tex, v_texcoord0 + vec2(-t.x,  t.y)).rgb +
	         texture2D(s_tex, v_texcoord0 + vec2( t.x,  t.y)).rgb;
	s *= 0.25;
	float l = max(s.r, max(s.g, s.b));
	float over = max(l - u_bloom.x, 0.0) / max(1.0 - u_bloom.x, 1e-3);
	gl_FragColor = vec4(s * (over * over / max(l, 1e-3)), 1.0);
}
