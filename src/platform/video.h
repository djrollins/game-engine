#ifndef PLATFORM_VIDEO_H
#define PLATFORM_VIDEO_H

#include <stdbool.h>

struct window {
	int width;
	int height;
	int bits_per_pixel;
	void *buffer;
	struct video_driver *parent;
};

struct video_driver {
	struct window *window;
	void *driver_data;

};

struct video_driver *init_video_driver(void);
struct window *create_window(struct video_driver *driver, int width, int height);
void blit_buffer(struct window *window);
bool handle_events(struct video_driver *driver);
void close_video_driver(struct video_driver *driver);

#endif /* PLATFORM_VIDEO_H */

