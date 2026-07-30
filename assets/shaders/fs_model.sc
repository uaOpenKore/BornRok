$input v_texcoord0, v_color0, v_wnormal, v_wtangent

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);
SAMPLER2D(s_nrm, 1);  // normal map (texture_n.png); only sampled when u_nrmParams.x > 0.5

// u_fade.x scales the final alpha: 1.0 = opaque, <1.0 = camera-occlude fade so a
// building between the camera and the player goes semi-transparent (set per draw).
uniform vec4 u_fade;
// Normal-mapping controls (#107): x = has-normal-map (1/0), y = bump strength.
uniform vec4 u_nrmParams;
// RSW "sun": u_lightDir.xyz = normalized world direction TO the light; the others carry colours.
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;  // diffuse
uniform vec4 u_ambient;     // ambient

void main()
{
    // Cut out on the texture's own alpha (magenta key set it to 0). RSM vertex
    // colours often store alpha 0, so they must NOT gate the cutout.
    vec4 t = texture2D(s_tex, v_texcoord0);
    if (t.a < 0.5)
        discard;

    vec3 rgb = t.rgb;
    if (u_nrmParams.x > 0.5) {
        // Rebuild the tangent frame and light the surface with the RSW sun. Without a normal map
        // (this branch skipped) the output is byte-identical to the classic unlit look.
        vec3 N = normalize(v_wnormal);
        vec3 T = normalize(v_wtangent);
        vec3 B = cross(N, T);
        vec3 nt = texture2D(s_nrm, v_texcoord0).xyz * 2.0 - 1.0;
        nt.xy *= u_nrmParams.y;  // bump strength
        vec3 Nw = normalize(nt.x * T + nt.y * B + nt.z * N);
        float ndl = max(dot(Nw, normalize(u_lightDir.xyz)), 0.0);
        vec3 lit = u_ambient.rgb + u_lightColor.rgb * ndl;
        rgb *= lit;
    }
    gl_FragColor = vec4(rgb, u_fade.x);
}
