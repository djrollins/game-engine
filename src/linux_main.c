#include "platform/video.h"

#include <stdint.h>
#include <time.h>
#include <stdio.h>

int main(void)
{
	struct video_driver *video_driver = init_video_driver();

	struct window *window = create_window(video_driver, 1280, 720);

	const int buffer_size = window->width * window->height;

	uint32_t *buffer = window->buffer;

	struct timespec start_time;
	clock_gettime(CLOCK_REALTIME, &start_time);

	uint8_t blue = 0;
	int samples = 0;
	float frametime = 0;
	while (handle_events(video_driver)) {
		for (int i = 0; i < buffer_size; ++i)
			buffer[i] = 0xFF440000 | blue;

		blit_buffer(window);
		++blue;

		struct timespec end_time;
		clock_gettime(CLOCK_MONOTONIC, &end_time);
		if (samples == 0) {
			samples = 200;
			printf("\r%5.3f ms", (frametime / samples) / 1000000);
			frametime = 0;
			fflush(stdout);
		} else if (end_time.tv_nsec > start_time.tv_nsec) {
			frametime += ((float)end_time.tv_nsec - (float)start_time.tv_nsec);
			--samples;
		}

		start_time = end_time;
	}

	close_video_driver(video_driver);
}
