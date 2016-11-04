/* standard library */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* X11 headers */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

Display *display;
XVisualInfo vinfo;
XImage ximage;
void *pixels;

static void resize_ximage(int width, int height)
{
	// TODO(djr): Need old width and height to free memory using munmap
	if (pixels) {
		free(pixels);
	}

	pixels = malloc(width * height * 4);

	ximage.width = width;
	ximage.height = height;
	ximage.format = ZPixmap;
	ximage.byte_order = XImageByteOrder(display);
	ximage.bitmap_unit = XBitmapUnit(display);
	ximage.bitmap_bit_order = XBitmapBitOrder(display);
	ximage.red_mask = vinfo.visual->red_mask;
	ximage.blue_mask = vinfo.visual->blue_mask;
	ximage.green_mask = vinfo.visual->green_mask;
	ximage.xoffset = 0;
	ximage.bitmap_pad = 32;
	ximage.depth = vinfo.depth;
	ximage.data = pixels;
	ximage.bits_per_pixel = 32;
	ximage.bytes_per_line = 0;


	XInitImage(&ximage);
}

static void update_window(
		Window window, GC gc,
		int width, int height,
		int xoffset, int yoffset)
{
	if (!pixels)
		resize_ximage(width, height);

	int pitch = width * 4;

	uint8_t *row = (uint8_t*)pixels;
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
			display, window,
			gc, &ximage,
			0, 0,
			0, 0,
			width, height);
}

int main()
{
	XInitThreads();

	int width = 1600;
	int height = 900;
	
	display = XOpenDisplay(NULL);

	if (!display) {
		/* TODO(djr): Logging */
		fputs("X11: Unable to create connection to display server", stderr);
		return -1;
	}

	int screen = DefaultScreen(display);
	int root = RootWindow(display, screen);
	
	if (!XMatchVisualInfo(display, screen, 32, TrueColor, &vinfo)) {
		/* TODO(djr): Logging */
		fputs("X11: Unable to find supported visual info", stderr);
		return -1;
	}

	Colormap colormap = XCreateColormap(
			display, root, vinfo.visual, AllocNone);

	const unsigned long wamask = CWBorderPixel | CWBackPixel | CWColormap | CWEventMask;

	XSetWindowAttributes wa;
	wa.colormap = colormap;
	wa.background_pixel = WhitePixel(display, screen);
	wa.border_pixel = 0;
	wa.event_mask = KeyPressMask | ExposureMask | StructureNotifyMask;

	Window window = XCreateWindow(
			display,
			root,
			0, 0,
			width, height,
			0, /* border width */
			vinfo.depth,
			InputOutput,
			vinfo.visual,
			wamask,
			&wa);
			
	if (!window) {
		/* TODO(djr): Logging */
		fputs("X11: Unable to create window", stderr);
		return -1;
	}

	XMapWindow(display, window);

	GC xgc = XCreateGC(display, window, 0, NULL);

	XStoreName(display, window, "Simple Engine");

	Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &wm_delete_window, 1);

	int xoffset = 0;
	int yoffset = 0;

	int running = 1;
	while(running) {
		XEvent e;
		while(XPending(display)) {
			XNextEvent(display, &e);
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
					resize_ximage(width, height);
					break;
				case Expose:
					break;
				default:
					printf("Unhandled XEvent (%d)\n", e.type);
			}
		}
		update_window(window, xgc, width, height, xoffset, yoffset);

		++xoffset;
		yoffset += 2;
	}

	XCloseDisplay(display);
	return 0;
}
