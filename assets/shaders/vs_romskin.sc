$input a_position, a_normal, a_texcoord0, a_weight, a_indices
$output v_texcoord0, v_normal, v_color0

#include <bgfx_shader.sh>

// Skinned RoM model (feat/content-sources, ROeM mobs): 4-bone linear-blend skinning.
// u_bones = worldOf(avatar node) * bindPose, built on the CPU each frame from the
// baked clip tracks; u_model[0] places the mob in the world.
uniform mat4 u_bones[70];

void main()
{
    vec4 p = vec4(a_position, 1.0);
    vec4 skinned =
        mul(u_bones[int(a_indices.x)], p) * a_weight.x +
        mul(u_bones[int(a_indices.y)], p) * a_weight.y +
        mul(u_bones[int(a_indices.z)], p) * a_weight.z +
        mul(u_bones[int(a_indices.w)], p) * a_weight.w;
    vec4 n = vec4(a_normal, 0.0);
    vec4 sn =
        mul(u_bones[int(a_indices.x)], n) * a_weight.x +
        mul(u_bones[int(a_indices.y)], n) * a_weight.y +
        mul(u_bones[int(a_indices.z)], n) * a_weight.z +
        mul(u_bones[int(a_indices.w)], n) * a_weight.w;
    gl_Position = mul(u_modelViewProj, vec4(skinned.xyz, 1.0));
    v_normal = normalize(mul(u_model[0], vec4(sn.xyz, 0.0)).xyz);
    v_texcoord0 = a_texcoord0;
    v_color0 = vec4(1.0, 1.0, 1.0, 1.0);
}
