$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

// x = sprite alpha (1.0 = opaque; < 1.0 fades a dying actor's corpse out so it
// dissolves like a "wet spot" instead of freezing). Living actors pass 1.0, so this
// shader is byte-for-byte equivalent to fs_model for them.
uniform vec4 u_spriteFade;

void main()
{
    // Same alpha cutout as fs_model (magenta key set transparent texels to a=0); the
    // surviving opaque texels then take the fade alpha so the corpse can blend out.
    vec4 t = texture2D(s_tex, v_texcoord0);
    if (t.a < 0.5)
        discard;
    // Multiply by the ACT layer tint (v_color0): RO recolours mobs this way (e.g. drainliar.act
    // tints the purple sprite red). White (the common case) is identity. Alpha folds the layer
    // alpha into the corpse-fade so semi-transparent layers stay translucent.
    gl_FragColor = vec4(t.rgb * v_color0.rgb, u_spriteFade.x * v_color0.a);
}
