/* standard library */
#include <stdio.h>

/* X11 headers */
#include <X11/Xlib.h>
#include <X11/keysym.h>

int main()
{
	Display *display = XOpenDisplay(NULL);
	
	if (!display) {
		// TODO(djr): Logging
		fputs("X11: Unable to create connection to display server", stderr);
		return -1;
	}

	int screen = DefaultScreen(display);

	unsigned long black = BlackPixel(display, screen);
	unsigned long white = WhitePixel(display, screen);
	int width = 1600;
	int height = 900;

	Window window = XCreateSimpleWindow(
			display, DefaultRootWindow(display),
			0, 0,
			width, height,
			20, white,
			black);

	if (!window) {
		// TODO(djr): Logging
		fputs("X11: Unable to create window", stderr);
		return -1;
	}

	XStoreName(display, window, "Simple Engine");
	XSetIconName(display, window, "Simple Engine");

	// Select which events to report
	XSelectInput(display, window, KeyPressMask|ExposureMask);

	// Allow us to handle a close event from the window manager
	Atom wm_delete_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &wm_delete_window, 1);

	// Show the window
	XMapWindow(display, window);

	int running = 1;
	while (running) {
		XEvent e;
		XNextEvent(display, &e);
		switch (e.type) {
			case ClientMessage:
				if (((Atom)e.xclient.data.l[0] == wm_delete_window)) {
					running = 0;
				}
			case KeyPress:
				if (XLookupKeysym(&e.xkey, 0) == XK_Escape) {
					running = 0;
				}
			case Expose:
			case ConfigureNotify:
				break;
			case UnmapNotify:
				// TODO(djr): figure out why this event fires so often
				break;
			default:
				printf("Unhandled XEvent (%d)\n", e.type);
		}
	}

	XCloseDisplay(display);
	return 0;
}
