/* standard library */
#include <stdint.h>
#include <string.h> /* strstr() */
#include <math.h>
#include <time.h>

/* system headers */
#include <sys/mman.h>
#include <linux/joystick.h>
#include <fcntl.h> /* open() */
#include <unistd.h> /* read() */
#include <libudev.h>
#include <pthread.h>

/* X11 headers */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#include <alsa/asoundlib.h>

#define USE_MIT_SHM
#define MIN(x, y) (x) < (y) ? (x) : (y)

struct joystick
{
	const char *device_node;
	const char *system_path;
	int file_descriptor;
};

static struct joystick *joysticks;

struct offscreen_buffer
{
	void *pixels;
	size_t buffer_size;
};

struct x11_device
{
	XImage *ximage;
	XShmSegmentInfo *shm;
	XVisualInfo vinfo;
	struct offscreen_buffer backbuffer;
	Display *display;
	Window window;
	GC gc;
	int root;
	int screen;
	int width;
	int height;
};

static void destroy_shm(struct x11_device *device)
{
#ifdef USE_MIT_SHM
	if (device->shm) {
		XShmDetach(device->display, device->shm);
		XDestroyImage(device->ximage);
		shmdt(device->shm->shmaddr);
		shmctl(device->shm->shmid, IPC_RMID, 0);
	}
#endif
}

static void resize_ximage(
	struct x11_device *device,
	int width, int height)
{
	if (device->width == width && device->height == height)
		return;

#ifdef USE_MIT_SHM
	if (!device->shm) {
		device->shm = malloc(sizeof(XShmSegmentInfo));
	}

	if (device->ximage) {
		destroy_shm(device);
	}

	device->ximage = XShmCreateImage(
		device->display,
		device->vinfo.visual,
		device->vinfo.depth,
		ZPixmap,
		NULL,
		device->shm,
		width,
		height);

	assert(device->ximage);

	device->shm->shmid = shmget(
		IPC_PRIVATE,
		device->ximage->bytes_per_line * height,
		IPC_CREAT|0777);

	device->shm->shmaddr = device->ximage->data = shmat(device->shm->shmid, 0, 0);
	memset(device->shm->shmaddr, 255, device->ximage->bytes_per_line * height);

	device->shm->readOnly = False;
	XShmAttach(device->display, device->shm);

	device->backbuffer.pixels = device->shm->shmaddr;
	device->backbuffer.buffer_size = device->ximage->bytes_per_line * height;
	device->width = width;
	device->height = height;

#else
	const size_t new_buffer_size = width * height * 4;

	if (device->ximage)
		XDestroyImage(device->ximage);

	device->backbuffer.pixels = malloc(new_buffer_size);
	device->backbuffer.buffer_size = new_buffer_size;
	device->width = width;
	device->height = height;

	device->ximage = XCreateImage(
		device->display,
		device->vinfo.visual,
		device->vinfo.depth,
		ZPixmap,
		0,
		device->backbuffer.pixels,
		width,
		height,
		32,
		width * 4);

	assert(device->ximage);
#endif
}

static void update_window(
	struct x11_device *device,
	int xoffset, int yoffset)
{
	const int width = device->width;
	const int height = device->height;
	const int pitch = device->width * 4;

	uint8_t *row = (uint8_t*)device->backbuffer.pixels;
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

	/* find out window size to centralise
	 * image rather than resize backbuffer
	 */
	struct {
		Window root;
		int x;
		int y;
		unsigned int w;
		unsigned int h;
		unsigned int border_width;
		unsigned int depth;
	} winattrs;

	XGetGeometry(
		device->display, device->window,
		&winattrs.root,
		&winattrs.x,
		&winattrs.y,
		&winattrs.w,
		&winattrs.h,
		&winattrs.border_width,
		&winattrs.depth);

	int x = (winattrs.w - width) / 2;
	int y = (winattrs.h - height) / 2;

#ifndef USE_MIT_SHIM
	XPutImage(
		device->display, device->window,
		device->gc, device->ximage,
		0, 0,
		x, y,
		width, height);
#else
	XShmPutImage(
		device->display, device->window,
		device->gc, device->ximage,
		0, 0,
		x, y,
		width, height,
		False);
	XSync(device->display, False);
#endif
}

struct ring_buffer
{
	unsigned int size;
	unsigned int frame_size;
	unsigned int read_cursor;
	unsigned int write_cursor;
	unsigned int target_latency;
	void *data;
	pthread_mutex_t mutex;
};

struct alsa_context
{
	unsigned int rate;
	unsigned int channels;
	unsigned int periods;
	unsigned int period_size;
	snd_pcm_t *pcm_handle;
	void *play_buffer;
	struct ring_buffer buffer;
};

static int update_audio(struct alsa_context *context)
{
	struct ring_buffer *buffer = &context->buffer;
	pthread_mutex_t *const mutex = &buffer->mutex;

	if (pthread_mutex_trylock(mutex) == 0) {
		const int read_cursor = buffer->read_cursor;
		const int write_cursor = buffer->write_cursor;
		const int cursor_diff = write_cursor - read_cursor;
		const int buffer_size = buffer->size;
		const unsigned int frame_size = buffer->frame_size;
		const unsigned int frames_to_end = buffer_size - read_cursor;
		const unsigned int period_size = context->period_size;

		unsigned int frames_to_write = 0;

		if (cursor_diff > 0) {
			frames_to_write = MIN((unsigned)cursor_diff, period_size);
		} else if (cursor_diff < 0) {
			frames_to_write = MIN(frames_to_end, period_size);
		}

		memcpy(context->play_buffer, buffer->data + (read_cursor * frame_size), frames_to_write * frame_size);

		buffer->read_cursor = (read_cursor + frames_to_write) % buffer_size;

		pthread_mutex_unlock(mutex);

		if (frames_to_write) {
			snd_pcm_uframes_t frames_left = frames_to_write;
			const int16_t *play_buffer = context->play_buffer;

			while (frames_left > 0) {
				int status;

				status = snd_pcm_writei(
					context->pcm_handle,
					play_buffer,
					frames_left);


				/* TODO(djr): logging */
				if (status < 0) {
					if (status == -EAGAIN) {
						const struct timespec delay = { 0, 1000L }; /* 1ms */
						nanosleep(&delay, NULL);
						continue;
					}

					/* TODO(djr): Find a better way to handle this error */
					if (status == -EPIPE) {
						/* underrun detected, increase latency and silence play_buffer */
						pthread_mutex_lock(mutex);
						const unsigned int latency = buffer->target_latency;
						memset(context->play_buffer, 0, frames_left * frame_size);
						buffer->target_latency += latency / 10;
						fprintf(stderr, "audio latency increased: %d -> %d\n", latency, buffer->target_latency);
						pthread_mutex_unlock(mutex);
					}

					status = snd_pcm_recover(context->pcm_handle, status, 0);

					if (status < 0) {
						fprintf(stderr, "alsa unable to recover\n");
						return 0; /* kill audio thread */
					}

					/* successfully recovered */
					continue;
				}

				play_buffer += status * frame_size;
				frames_left -= status;
			}


		} else {
			const struct timespec delay = { 0, 16000L }; /* 16ms */
			nanosleep(&delay, NULL);
		}
	}

	return 1;
}

static void *update_audio_thread_driver(void *context)
{
	printf("Starting audio thread\n");
	while (update_audio(context));
	printf("Audio thread stopped\n");
	return NULL;
}

/*
 * NOTE: So I can understand later:
 * Sample:	  The amplitude of a single channel of sound at a point in time.
 * Frame:		Contains a single sample per channel (Mono: 1, Stereo: 2)
 * Frame size:	Size in bytes of a frame (8-bit Mono: 1 byte, 16-bit Stereo: 4 bytes)
 * Rate:		Number of frames per second
 * Period Size: How many frames that are sent in a single batch
 * Periods:		How many batches of frames that alsa processes in one go
 */
static struct ring_buffer *init_audio(unsigned int sample_rate, unsigned buffer_size)
{
	int status;
	snd_pcm_t *pcm_handle;
	snd_pcm_hw_params_t *hw_params;
	struct alsa_context *context;

	const unsigned int rate = sample_rate;
	const unsigned int channels = 2;
	const unsigned int frame_size = 2 * sizeof(int16_t);
	const snd_pcm_uframes_t period_size = 1024;

	unsigned int periods = 2;

#define ALSA_CHECK(status, msg) \
	if (status < 0) { \
		fprintf(stderr, "%s: %s\n", msg, snd_strerror(status)); \
		return NULL; \
	}

	/* open connection to default pcm device in playback mode
	 * TODO: Make this configurable
	 */
	status = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
	ALSA_CHECK(status, "Unable to open default pcm device");

	/* allocate struct for hardware parameters */
	snd_pcm_hw_params_alloca(&hw_params);

	/* fill hardware parameters with defaults */
	status = snd_pcm_hw_params_any(pcm_handle, hw_params);
	ALSA_CHECK(status, "Unable to get default hardware configuration for pcm device");

	status = snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
	ALSA_CHECK(status, "Unable to set interleaved access to pcm device");

	status = snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);
	ALSA_CHECK(status, "Unable to set format for pcm device");

	status = snd_pcm_hw_params_set_channels(pcm_handle, hw_params, channels);
	ALSA_CHECK(status, "Unable to set channels for pcm device");

	status = snd_pcm_hw_params_set_rate(pcm_handle, hw_params, rate, 0);
	ALSA_CHECK(status, "Unable to set sample rate for pcm device");

	status = snd_pcm_hw_params_set_period_size(pcm_handle, hw_params, period_size, 0);
	ALSA_CHECK(status, "Unable to set period size for pcm device");

	status = snd_pcm_hw_params_set_periods_near(pcm_handle, hw_params, &periods, 0);
	ALSA_CHECK(status, "Unable to set period count for pcm device");
#undef ALSA_CHECK

	status = snd_pcm_hw_params(pcm_handle, hw_params);

	/* Check the buffer size is as expected */
	const unsigned int expected_buffer_time = (1e6 * periods * period_size * 2) / (rate * 2);
	unsigned int actual_buffer_time;
	snd_pcm_hw_params_get_buffer_time(hw_params, &actual_buffer_time, 0);

	assert(actual_buffer_time == expected_buffer_time);

	/* allocate enough memory for the context struct, the play_buffer and the ring buffer */
	const size_t context_size = sizeof(struct alsa_context);
	const size_t play_buffer_size = (frame_size * period_size);
	const size_t ring_buffer_size = (frame_size * buffer_size);
	const size_t total_memory_size = context_size + play_buffer_size + ring_buffer_size;

	void *memory = malloc(total_memory_size);

	if (!memory) {
		fprintf(stderr, "Unable to allocate space for ALSA context\n");
		return NULL;
	}

	memset(memory, 0, total_memory_size);

	context = memory;
	context->pcm_handle = pcm_handle;
	context->rate = rate;
	context->channels = channels;
	context->periods = periods;
	context->period_size = period_size;
	context->play_buffer = memory + context_size;

	context->buffer.data = memory + context_size + play_buffer_size;
	context->buffer.size = buffer_size;
	context->buffer.frame_size = frame_size;
	context->buffer.target_latency = buffer_size / 60;

	/* start audio thread */
	pthread_t audio_thread;

	status = pthread_mutex_init(&context->buffer.mutex, NULL);
	if (status) {
		fprintf(stderr, "Unable to create mutex for audio thread: %s\n", strerror(status));
		free(memory);
		return NULL;
	}

	status = pthread_create(&audio_thread, NULL, update_audio_thread_driver, context);
	if (status) {
		fprintf(stderr, "Unable to create audio thread: %s\n", strerror(status));
		free(memory);
		return NULL;
	}

	/* ready to roll! */
	return &context->buffer;
}

static int init_joysticks()
{
	int joystick_count;
	const int max_joystick_count = 4;

	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *device_list, *device_list_entry;
	struct udev_device *device;
#if 0
	struct udev_monitor *udev_monitor;
#endif

	if (!joysticks) {
		joysticks = mmap(
			NULL, max_joystick_count * sizeof(struct joystick),
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE,
			-1, 0);
	}

	for (int i = 0; i < max_joystick_count; ++i) {
		joysticks[i].system_path = NULL;
		joysticks[i].device_node = NULL;
	}

	udev = udev_new();
	if (!udev) {
		return 0;
	}

#if 0
	/* TODO(djr): Actually do something with new controllers */
	udev_monitor = udev_monitor_new_from_netlink(udev, "udev");
	if (!udev_monitor) {
		fputs("Unable to create udev monitor\n", stderr);
	} else {
		if (udev_monitor_filter_add_match_subsystem_devtype(udev_monitor, "input", NULL) < 0) {
			goto error;
		} else {
			if (udev_monitor_enable_receiving(udev_monitor) < 0) {
				error:
				fputs("Unable to create udev monitor\n", stderr);
				udev_monitor_unref(udev_monitor);
				udev_monitor = NULL;
			}
		}
	}
#endif

	enumerate = udev_enumerate_new(udev);

	udev_enumerate_add_match_subsystem(enumerate, "input");
	udev_enumerate_scan_devices(enumerate);
	device_list = udev_enumerate_get_list_entry(enumerate);

	joystick_count = 0;

	udev_list_entry_foreach(device_list_entry, device_list) {
		const char *system_path;
		const char *device_node;
		int file_descriptor;

		system_path = udev_list_entry_get_name(device_list_entry);
		device = udev_device_new_from_syspath(udev, system_path);
		device_node = udev_device_get_devnode(device);

		if (device_node && strstr(device_node, "/js")) {
			if ((file_descriptor = open(device_node, O_RDONLY | O_NONBLOCK)) >= 0) {
				printf("Device system path: %s\n", system_path);
				printf("Device node path: %s\n", device_node);
				printf("Device file descriptor: %d\n", file_descriptor);
				joysticks[joystick_count].system_path = system_path;
				joysticks[joystick_count].device_node = device_node;
				joysticks[joystick_count].file_descriptor = file_descriptor;
			}
			++joystick_count;
		}

	}

	return joystick_count;
}

struct joystick_state
{
	float left_stick_x;
	float left_stick_y;
};

static void update_joystick(const int index, struct joystick_state* state)
{
	int result;
	struct js_event joystick_event;
	const int file_descriptor = joysticks[index].file_descriptor;

	result = read(file_descriptor, &joystick_event, sizeof(joystick_event));

	while (result > 0) {
		switch (joystick_event.type & ~JS_EVENT_INIT) {

			case JS_EVENT_AXIS: {
				const float value = joystick_event.value / 32767.f;
				if (joystick_event.number == 0) {
					state->left_stick_x = value;
				} else if (joystick_event.number == 1) {
					state->left_stick_y = value;
				}
				break;
			}
		}

		result = read(file_descriptor, &joystick_event, sizeof(joystick_event));
	}
}

int main()
{
	XInitThreads();

	int width = 1280;
	int height = 720;

	static struct x11_device device;
	device.display = XOpenDisplay(NULL);

	assert(True == XShmQueryExtension(device.display));

	if (!device.display) {
		/* TODO(djr): Logging */
		fputs("X11: Unable to create connection to display server", stderr);
		return -1;
	}

	device.screen = DefaultScreen(device.display);
	device.root = RootWindow(device.display, device.screen);

	if (!XMatchVisualInfo(device.display, device.screen, 32, TrueColor, &device.vinfo)) {
		/* TODO(djr): Logging */
		fputs("X11: Unable to find supported visual info", stderr);
		return -1;
	}

	Colormap colormap = XCreateColormap(
			device.display, device.root, device.vinfo.visual, AllocNone);

	const unsigned long wamask = CWBorderPixel | CWBackPixel | CWColormap | CWEventMask;

	XSetWindowAttributes wa;
	wa.colormap = colormap;
	wa.background_pixel = BlackPixel(device.display, device.screen);
	wa.border_pixel = 0;
	wa.event_mask = KeyPressMask | ExposureMask | StructureNotifyMask;

	device.window = XCreateWindow(
		device.display,
		device.root,
		0, 0,
		width, height,
		0, /* border width */
		device.vinfo.depth,
		InputOutput,
		device.vinfo.visual,
		wamask,
		&wa);

	if (!device.window) {
		/* TODO(djr): Logging */
		fputs("X11: Unable to create window", stderr);
		return -1;
	}

	XMapWindow(device.display, device.window);

	device.gc = XCreateGC(device.display, device.window, 0, NULL);

	XStoreName(device.display, device.window, "Simple Engine");

	/* Give the window a class name so i3 can float it. */
	XClassHint class_hint = { "Handmade Engine", "GameDev" };
	XSetClassHint(device.display, device.window, &class_hint);

	Atom wm_delete_window = XInternAtom(device.display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(device.display, device.window, &wm_delete_window, 1);

	XSizeHints size_hints;
	size_hints.flags = PMinSize | PMaxSize;
	size_hints.min_width = size_hints.max_width = width;
	size_hints.min_height = size_hints.max_height = height;

	resize_ximage(&device, width, height);

	const int joystick_count = init_joysticks();

	const int base_hz = 261; /* middle c */
	const int audio_sample_rate = 48000;
	const int16_t tone_volume = 6000;
	struct ring_buffer *audio_buffer = init_audio(audio_sample_rate, audio_sample_rate);

	struct joystick_state state = {0};

	struct timespec t_start;
	clock_gettime(CLOCK_REALTIME, &t_start);

	int xoffset = 0;
	int yoffset = 0;
	int running = 1;

	while(running) {
		XEvent e;
		while(XPending(device.display)) {
			XNextEvent(device.display, &e);
			switch(e.type) {
				case ClientMessage:
					if (((Atom)e.xclient.data.l[0] == wm_delete_window)) {
						running = 0;
					}
					break;
				case KeyPress:
					if (XLookupKeysym(&e.xkey, 0) == XK_Escape) {
						running = 0;
					}
					break;
				case ConfigureNotify:
					break;
				case Expose:
					break;
				default:
					printf("Unhandled XEvent (%d)\n", e.type);
			}
		}

		if(joystick_count) {
			update_joystick(0, &state);
		}

		pthread_mutex_t *const mutex = &audio_buffer->mutex;
		if (pthread_mutex_lock(mutex) == 0) {
			int16_t *sample_ptr;
			unsigned int frames_to_write;
			unsigned int region_one_size;
			unsigned int region_two_size;

			static double t = 0;
			static unsigned int running_sample_index = 0;
			static float previous_tone_hz = 0;

			const unsigned int buffer_size = audio_buffer->size;
			const unsigned int frame_size = audio_buffer->frame_size;
			const unsigned int read_cursor = audio_buffer->read_cursor;
			const unsigned int latency = audio_buffer->target_latency;
			const unsigned int sample_index = running_sample_index % buffer_size;
			const unsigned int target_cursor = (read_cursor + latency) % buffer_size;

			const float tone_hz = base_hz + ((state.left_stick_x + state.left_stick_y) * base_hz);
			const float tone_diff = tone_hz - previous_tone_hz;

			if (sample_index > target_cursor) {
				frames_to_write = buffer_size - sample_index + target_cursor;
			} else {
				frames_to_write = target_cursor - sample_index;
			}

			if (frames_to_write) {
				const float tone_step = tone_diff / frames_to_write;
				float curr_hz = previous_tone_hz;

				if ((sample_index + frames_to_write) >= buffer_size) {
					region_one_size = buffer_size - sample_index;
				} else {
					region_one_size = frames_to_write;
				}

				region_two_size = frames_to_write - region_one_size;

				sample_ptr = audio_buffer->data + (sample_index * frame_size);
				for (unsigned int i = 0; i < region_one_size; ++i) {
					const double wave_period = audio_sample_rate / curr_hz;
					const int16_t value = sinf(t) * tone_volume;
					*sample_ptr++ = value;
					*sample_ptr++ = value;
					t += (2.0f * M_PI) / wave_period;
					curr_hz += tone_step;
					++running_sample_index;
				}

				sample_ptr = audio_buffer->data;
				for (unsigned int i = 0; i < region_two_size; ++i) {
					const double wave_period = audio_sample_rate / curr_hz;
					const int16_t value = sinf(t) * tone_volume;
					*sample_ptr++ = value;
					*sample_ptr++ = value;
					t += (2.0f * M_PI) / wave_period;
					curr_hz += tone_step;
					++running_sample_index;
				}

			}
			previous_tone_hz = tone_hz;
			audio_buffer->write_cursor = target_cursor;
			pthread_mutex_unlock(mutex);
		}

		xoffset += state.left_stick_x * 5 + 1;
		yoffset += state.left_stick_y * 5 + 1;

		update_window(&device, xoffset, yoffset);

		struct timespec t_end;
		clock_gettime(CLOCK_REALTIME, &t_end);
		long t_delta = t_end.tv_nsec - t_start.tv_nsec;

		if (t_delta > 0) {
			printf("\r%4.2f ms, %4.2f fps", t_delta * 1e-6, 1 / (t_delta * 1e-9));
		}

		t_start = t_end;
	}

	destroy_shm(&device);
	XCloseDisplay(device.display);
	return 0;
}
