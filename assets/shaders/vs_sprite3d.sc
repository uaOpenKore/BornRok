$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

// x = clip-space depth bias toward the camera. Set per sprite layer so the body,
// head and headgear get distinct depths (body < head < headgear) and each draws
// strictly over the one below — without it the coplanar billboards share a depth
// and DEPTH_TEST_LESS rejects whichever loses the tie.
uniform vec4 u_spriteBias;

// Billboard variant of vs_model. The quad is built world-vertical on the CPU (up =
// world Y, height-compensated for the camera tilt) so a sprite's depth is its honest
// 3D depth: a model the actor stands in front of no longer eats the head, and a model
// it stands behind still occludes it. Only the small per-layer bias nudges parts apart.
void main()
{
	vec4 p = mul(u_modelViewProj, vec4(a_position, 1.0));
	p.z -= u_spriteBias.x * p.w;
	gl_Position = p;
	v_texcoord0 = a_texcoord0;
	v_color0 = a_color0;
}
