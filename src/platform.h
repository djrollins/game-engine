#ifndef HANDMADE_PLATFORM
#define HANDMADE_PLATFORM

struct offscreen_buffer
{
	void *pixels;
	size_t width;
	size_t height;
	size_t pitch;
};

void render(struct offscreen_buffer *buffer, int xoffset, int yoffset);

#endif /* HANDMADE_PLATFORM */
