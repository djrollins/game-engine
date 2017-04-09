#ifndef PLATFORM_VIDEO_H
#define PLATFORM_VIDEO_H

#include <stdbool.h>

struct video_driver
{
	void *driver_data;
};

struct video_driver *init_video_driver(void);
bool create_window(struct video_driver *driver, int width, int height);
bool handle_events(struct video_driver *driver);
void close_video_driver(struct video_driver *driver);

#endif /* PLATFORM_VIDEO_H */

