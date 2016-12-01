/* standard library */
#include <stdio.h>
#include <stdint.h>

/* system headers */
#include <sys/mman.h>

/* X11 headers */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

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
	size_t new_buffer_size = width * height * 4;

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
	int width = device->width;
	int height = device->height;
	int pitch = device->width * 4;

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

	Atom wm_delete_window = XInternAtom(device.display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(device.display, device.window, &wm_delete_window, 1);

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
		update_window(&device, xoffset, yoffset);

		++xoffset;
		yoffset += 2;
	}

	XCloseDisplay(device.display);
	return 0;
}
