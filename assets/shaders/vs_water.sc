$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

// Water surface vertex shader (reflections, #water). Same MVP transform as vs_sprite3d but
// WITHOUT the per-layer depth bias (the water plane is flat, one layer). The reflection lookup
// in fs_water uses gl_FragCoord / u_viewRect, so no screen-position varying is needed here --
// keeping the varying set identical to what fs_water reads avoids the DX11 link trap where a
// varying the FS ignores fails to link.
void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
	v_texcoord0 = a_texcoord0;
	v_color0 = a_color0;
}
