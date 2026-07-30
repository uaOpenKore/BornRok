$input v_color0

#include <bgfx_shader.sh>

// Solid translucent colour (alpha-blended by the render state). Used for the
// ground cell cursor and the walk-path line.
void main()
{
	gl_FragColor = v_color0;
}
