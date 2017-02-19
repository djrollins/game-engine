/* standard library */
#include <stdio.h>
#include <stdint.h>
#include <string.h> /* strstr() */

/* system headers */
#include <sys/mman.h>
#include <linux/joystick.h>
#include <fcntl.h> /* open() */
#include <unistd.h> /* read() */
#include <libudev.h>

/* X11 headers */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

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
	XImage ximage;
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

static void resize_ximage(
	struct x11_device *device,
	int width, int height)
{
	const size_t new_buffer_size = width * height * 4;

	if (new_buffer_size > device->backbuffer.buffer_size) {
		munmap(device->backbuffer.pixels, device->backbuffer.buffer_size);
		device->backbuffer.pixels = mmap(
				NULL, new_buffer_size,
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE,
				-1, 0);

		device->backbuffer.buffer_size = new_buffer_size;
	}

	device->width = width;
	device->height = height;
	device->ximage.width = width;
	device->ximage.height = height;
	device->ximage.format = ZPixmap;
	device->ximage.byte_order = XImageByteOrder(device->display);
	device->ximage.bitmap_unit = XBitmapUnit(device->display);
	device->ximage.bitmap_bit_order = XBitmapBitOrder(device->display);
	device->ximage.red_mask = device->vinfo.visual->red_mask;
	device->ximage.blue_mask = device->vinfo.visual->blue_mask;
	device->ximage.green_mask = device->vinfo.visual->green_mask;
	device->ximage.xoffset = 0;
	device->ximage.bitmap_pad = 32;
	device->ximage.depth = device->vinfo.depth;
	device->ximage.data = device->backbuffer.pixels;
	device->ximage.bits_per_pixel = 32;
	device->ximage.bytes_per_line = 0;

	XInitImage(&device->ximage);
}

static void update_window(
	struct x11_device *device,
	int xoffset, int yoffset)
{
	const int width = device->width;
	const int height = device->height;
	const int pitch = device->width * 4;

	if (!device->backbuffer.pixels)
		resize_ximage(device, width, height);

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

	XPutImage(
		device->display, device->window,
		device->gc, &device->ximage,
		0, 0,
		0, 0,
		width, height);
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
	float x;
	float y;
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
				const float value = joystick_event.value * 100.f / 32767.f;
				if (joystick_event.number == 0) {
					state->x = value;
				} else if (joystick_event.number == 1) {
					state->y = value;
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

	int width = 1600;
	int height = 900;

	static struct x11_device device;
	device.display = XOpenDisplay(NULL);

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

	XClassHint class_hint = { "Handmade Engine", "GameDev" };

	XSetClassHint(device.display, device.window, &class_hint);

	Atom wm_delete_window = XInternAtom(device.display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(device.display, device.window, &wm_delete_window, 1);

	const int joystick_count = init_joysticks();

	int xoffset = 0;
	int yoffset = 0;

	int running = 1;
	struct joystick_state state = {0};
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
					width = e.xconfigure.width;
					height = e.xconfigure.height;
					resize_ximage(&device, width, height);
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

		xoffset += state.x / 25;
		yoffset += state.y / 25;

		update_window(&device, xoffset, yoffset);
		xoffset += 1;
		yoffset += 1;
	}

	XCloseDisplay(device.display);
	return 0;
}
