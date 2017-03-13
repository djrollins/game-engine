#include <stdint.h> /* (u)intXX_t */
#include <stddef.h> /* size_t */

#include "platform.h"

static void render_gradient(
	struct offscreen_buffer *buffer, int xoffset, int yoffset)
{
	const int width = buffer->width;
	const int height = buffer->height;
	const int pitch = buffer->pitch;

	uint8_t *row = (uint8_t*)buffer->pixels;
	for (int y = 0; y < height; ++y) {
		uint32_t *pixel = (uint32_t*)row;
		for (int x = 0; x < width; ++x) {
			uint8_t blue  = (x + xoffset);
			uint8_t green = (y + yoffset);
			uint8_t red   = 0;
			uint8_t alpha = 255;

			*pixel++ = (alpha << 24) | (red << 16) | (green << 8) | blue;
		}
		row += pitch;
	}
}

void render(struct offscreen_buffer *buffer, int xoffset, int yoffset)
{
	render_gradient(buffer, xoffset, yoffset);
}
