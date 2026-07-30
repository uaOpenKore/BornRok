$input a_position, a_texcoord0, a_color0, a_normal, a_tangent
$output v_texcoord0, v_color0, v_wnormal, v_wtangent

#include <bgfx_shader.sh>

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
    // World-space normal/tangent for normal mapping (#107). w=0 drops the translation; assumes
    // near-uniform scale in the node transform, which RSM models use.
    v_wnormal  = mul(u_model[0], vec4(a_normal,  0.0)).xyz;
    v_wtangent = mul(u_model[0], vec4(a_tangent, 0.0)).xyz;
}
