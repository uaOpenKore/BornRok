$input v_texcoord0

#include <bgfx_shader.sh>

// Screen-space ambient occlusion (#ao). Depth-only (no normal buffer): for each pixel we compare the
// linearised scene depth against a ring of neighbours; neighbours that sit CLOSER to the camera (in
// front) within a limited range darken the pixel, which lands soft contact shadows where geometry
// meets the ground and in crevices. RO's near-top-down camera hides the usual normal-free haloing.
// This pass owns the final blit when AO is on, so it also applies the brightness/contrast grade
// (same maths as fs_grade) so AO coexists with the grade.
SAMPLER2D(s_tex, 0);    // scene colour
SAMPLER2D(s_depth, 1);  // scene depth (D24S8 sampled; .x = device depth 0..1)
uniform vec4 u_ao;        // x = intensity, y = radius (UV), z = range (linear eye-z), w = homogeneousDepth(0/1)
uniform vec4 u_aoParams;  // x = 1/width, y = 1/height, z = zNear, w = zFar
uniform vec4 u_grade;     // x = brightness (0.5 neutral), y = contrast (0.5 neutral)

vec3 applyGrade(vec3 c, vec2 g) {
	float bd = (g.x - 0.5) * 1.0;
	float cf = g.y * 2.0;
	return clamp((c - 0.5) * cf + 0.5 + bd, 0.0, 1.0);
}

// Device depth 0..1 -> positive linear eye-space distance. The two clip conventions need DIFFERENT
// maths (not just a remap): homogeneous-depth backends (GL) use a [-1,1] clip-z projection, the rest
// (D3D/Metal/Vulkan) a [0,1] one, so we compute both and select on homog.
float lineariseDepth(float d, float zn, float zf, float homog) {
	float zGL  = (2.0 * zn * zf) / (zf + zn - (d * 2.0 - 1.0) * (zf - zn));  // GL [-1,1] clip
	float zD3D = (zn * zf) / (zf - d * (zf - zn));                          // D3D/Vulkan/Metal [0,1] clip
	return mix(zD3D, zGL, homog);
}

void main()
{
	vec3 scene = texture2D(s_tex, v_texcoord0).rgb;
	float zn = u_aoParams.z, zf = u_aoParams.w;
	float cd = texture2D(s_depth, v_texcoord0).x;
	float cz = lineariseDepth(cd, zn, zf, u_ao.w);

	// 8-tap ring (unit circle), scaled by the UV radius. Constant screen-space radius keeps the look
	// stable under the slow RO orbit without needing a per-pixel depth divide.
	vec2 texel = u_aoParams.xy;
	float radius = u_ao.y;
	vec2 k[8];
	k[0] = vec2( 1.0,  0.0); k[1] = vec2( 0.7,  0.7); k[2] = vec2( 0.0,  1.0); k[3] = vec2(-0.7,  0.7);
	k[4] = vec2(-1.0,  0.0); k[5] = vec2(-0.7, -0.7); k[6] = vec2( 0.0, -1.0); k[7] = vec2( 0.7, -0.7);

	float range = u_ao.z;
	float bias = range * 0.02;   // ignore near-coplanar samples (self-occlusion / depth precision)
	float occ = 0.0;
	for (int i = 0; i < 8; ++i) {
		vec2 suv = v_texcoord0 + k[i] * radius;
		float sz = lineariseDepth(texture2D(s_depth, suv).x, zn, zf, u_ao.w);
		float diff = cz - sz;                 // >0 : neighbour is in FRONT of us (an occluder)
		float rangeCheck = smoothstep(1.0, 0.0, (diff - bias) / range);  // fades past the range
		occ += step(bias, diff) * rangeCheck;
	}
	occ *= 0.125;  // /8

	float ao = clamp(1.0 - occ * u_ao.x, 0.0, 1.0);
	vec3 c = scene * ao;
	gl_FragColor = vec4(applyGrade(c, u_grade.xy), 1.0);
}
