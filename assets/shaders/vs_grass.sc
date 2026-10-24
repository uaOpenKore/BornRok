$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

// Depth bias toward the camera (kept for parity with vs_sprite3d; grass passes 0).
uniform vec4 u_spriteBias;

// Wind sway for the ground grass (#grasswind). The tuft cross-quads are built in WORLD space on the
// CPU, so a_position is already a world coordinate and the sway phase can key off it directly (the
// waves then travel across the field with the wind, not with the camera). xy = wind direction over the
// ground (world XZ) scaled by its strength, z = time in seconds, w = sway amplitude in world units
// (0 disables, so the toggle just zeroes w and the same program keeps drawing).
uniform vec4 u_wind;

void main()
{
	vec3 pos = a_position;
	// texcoord0.y is 1.0 at the rooted base and 0.0 at the blade tip (see GrassRenderer::buildMesh),
	// so `up` grows toward the tip; up*up keeps the base planted and bends the top the most.
	float up = 1.0 - a_texcoord0.y;
	float phase = pos.x * 0.35 + pos.z * 0.22;
	float wave = sin(u_wind.z * 1.7 + phase) * 0.6 + sin(u_wind.z * 2.7 + phase * 1.7) * 0.4;
	float s = up * up * u_wind.w * wave;
	pos.x += u_wind.x * s;
	pos.z += u_wind.y * s;
	vec4 p = mul(u_modelViewProj, vec4(pos, 1.0));
	p.z -= u_spriteBias.x * p.w;
	gl_Position = p;
	v_texcoord0 = a_texcoord0;
	v_color0 = a_color0;
}
