#include "platform/video.h"

#include <assert.h>

int main()
{
	struct video_driver *video_driver = init_video_driver();

	assert(create_window(video_driver, 1600, 900));

	while(handle_events(video_driver));

	close_video_driver(video_driver);
}
