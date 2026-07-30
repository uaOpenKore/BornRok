$input a_position, a_color0
$output v_color0

#include <bgfx_shader.sh>

// Flat, untextured ground-decal vertex shader: the cell cursor and the walk-path
// line. Position is already in world space; just project and pass the (translucent)
// vertex colour through to the fragment stage.
void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_color0 = a_color0;
}
