$input a_position, a_texcoord0
$output v_texcoord0

#include <bgfx_shader.sh>

// Fullscreen pass: the vertex buffer already holds clip-space positions (a big triangle
// covering the screen), so pass them straight through.
void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_texcoord0 = a_texcoord0;
}
