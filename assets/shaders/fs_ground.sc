$input v_texcoord0, v_color0, v_lightcoord, v_wnormal, v_wtangent

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);
SAMPLER2D(s_lightmap, 1);
SAMPLER2D(s_nrm, 2);     // optional tangent-space normal map (only read when u_nrmParams.x > 0.5)

uniform vec4 u_ambient;   // rgb = RSW ambient light
uniform vec4 u_diffuse;   // rgb = RSW diffuse (sun) light
uniform vec4 u_fade;      // x = global ground opacity (1 = opaque; <1 in Camera Lock x-ray)
uniform vec4 u_nrmParams; // x = has normal map (1/0), y = bump strength (#107)
uniform vec4 u_lightDir;  // xyz = normalized direction TO the RSW sun (#107)

void main()
{
    // Cut out the magenta colour-key (the texture load set those texels to alpha 0): RO uses
    // #ff00ff as the transparent border of floating walkways/platforms (S.: "розовый в текстурах
    // должен быть прозрачным"). Test the TEXTURE alpha, not texel — the ground vertex colour can
    // carry a low alpha that must not gate the cutout.
    vec4 tex0 = texture2D(s_tex, v_texcoord0);
    if (tex0.a < 0.5)
        discard;
    vec4 texel = tex0 * v_color0;              // texture * per-tile GND vertex colour
    vec4 lm    = texture2D(s_lightmap, v_lightcoord);

    // Ground lighting ported 1:1 from GRF Editor's map preview (gnd.vert/gnd.frag) — S.: "повтори
    // источник света вместо штатного". Directional N·L off the terrain normal with GRF Editor's
    // ambient/diffuse cross-mix `mult`, times the shadowmap alpha at FULL strength (no floor — the
    // pale 0.30 floor was what washed the shadows out; the baked a=0 "black dots" are already filled
    // in GroundMesh::buildLightmap, so no floor is needed), plus the coloured lightmap added on top.
    // A normal map (#107) perturbs N when present. u_lightDir = RSW sun direction (set every frame).
    vec3 N = normalize(v_wnormal);
    if (u_nrmParams.x > 0.5)
    {
        vec3 T = normalize(v_wtangent);
        vec3 B = cross(N, T);
        vec3 nt = texture2D(s_nrm, v_texcoord0).xyz * 2.0 - 1.0;
        nt.xy *= u_nrmParams.y;                 // bump strength
        N = normalize(nt.x * T + nt.y * B + nt.z * N);
    }
    float NL = clamp(dot(N, normalize(u_lightDir.xyz)), 0.0, 1.0);
    vec3 ambC = u_ambient.rgb, difC = u_diffuse.rgb;
    vec3 aF = (1.0 - ambC) * ambC;
    vec3 amb = ambC - aF + aF * difC;
    vec3 dF = (1.0 - difC) * difC;
    vec3 dif = difC - dF + dF * ambC;
    vec3 m1 = min(NL * dif + amb, 1.0);
    vec3 mxc = max(difC, ambC), mnc = min(difC, ambC);
    vec3 m2 = min(mxc + (1.0 - mxc) * mnc, 1.0);   // cap when ambient+diffuse > 1 (no blow-out)
    vec3 mult = clamp(min(m1, m2), 0.0, 1.0);

    vec3 lit = texel.rgb * mult * lm.a + lm.rgb;   // lm.a at full range = crisp dark shadows
    gl_FragColor = vec4(lit, texel.a * u_fade.x);
}
