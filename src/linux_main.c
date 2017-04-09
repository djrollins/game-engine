#include "platform/video.h"

#include <stdint.h>

int main(void)
{
	struct video_driver *video_driver = init_video_driver();

	struct window *window = create_window(video_driver, 1600, 900);

	const int buffer_size = window->width * window->height;

	uint32_t *buffer = window->buffer;

	while (handle_events(video_driver)) {
		for (int i = 0; i < buffer_size; ++i)
			buffer[i] = 0xFF440099;

		blit_buffer(window);
	}

	close_video_driver(video_driver);
}
