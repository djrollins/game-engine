/* standard library */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* X11 headers */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

int main()
{
	XInitThreads();

	int width = 1600;
	int height = 900;
	
	Display *display = XOpenDisplay(NULL);

	if (!display) {
		/* TODO(djr): Logging */
		fputs("X11: Unable to create connection to display server", stderr);
		return -1;
	}

	int screen = DefaultScreen(display);
	int root = RootWindow(display, screen);
	
	XVisualInfo vinfo;
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

	uint32_t *pixels;
	XImage *ximage = NULL;

	XStoreName(display, window, "Simple Engine");

	Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &wm_delete_window, 1);

	int running = 1;
	while (running) {
		XEvent e;
		XNextEvent(display, &e);
		switch (e.type) {
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
				if (ximage && (width != e.xconfigure.width || height != e.xconfigure.height)) {
					width = e.xconfigure.width;
					height = e.xconfigure.height;
					XDestroyImage(ximage);
					ximage = NULL;
				}
				break;
			case Expose:
				if (!ximage) {
					pixels = (uint32_t*)malloc(width * height * sizeof(uint32_t));

					for (int i = 0; i < width * height; ++i) {
						/* 0xBBGGRRAA */
						pixels[i] = 0xFF0000FF;
					}

					ximage = XCreateImage(
							display, CopyFromParent,
							vinfo.depth, ZPixmap,
							0, (char*)pixels,
							width, height,
							32, 0);
				}
				XPutImage(
						display, window,
						xgc, ximage,
						0, 0,
						0, 0,
						width, height);
				break;
			default:
				printf("Unhandled XEvent (%d)\n", e.type);
		}
	}

	XCloseDisplay(display);
	return 0;
}
