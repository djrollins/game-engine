#include <xcb/xcb.h>
#include <xcb/xcb_util.h>

#include <stdlib.h>
#include <stdio.h>

#include "../video.h"

const char *xcb_errors[] =
{
    "Success",
    "BadRequest",
    "BadValue",
    "BadWindow",
    "BadPixmap",
    "BadAtom",
    "BadCursor",
    "BadFont",
    "BadMatch",
    "BadDrawable",
    "BadAccess",
    "BadAlloc",
    "BadColor",
    "BadGC",
    "BadIDChoice",
    "BadName",
    "BadLength",
    "BadImplementation",
    "Unknown"
};

struct xcb
{
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	xcb_depth_t *depth;
	xcb_visualtype_t *visual;
	xcb_colormap_t colormap;
	xcb_window_t window;
	xcb_atom_t delete_window_atom;
};

static bool set_preferred_screen(struct xcb* xcb, const int preferred_screen)
{
	const xcb_setup_t *setup = xcb_get_setup(xcb->connection);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);

	for (int i = 0; i < preferred_screen; ++i) {
		if (iter.rem == 0) {
			break;
		}
		xcb_screen_next(&iter);
	}

	return (xcb->screen = iter.data);
}

static bool set_visual(struct xcb *xcb, const int depth,
		const xcb_visual_class_t class)
{
	xcb->depth = NULL;
	xcb->visual = NULL;

	xcb_depth_iterator_t depth_iter =
		xcb_screen_allowed_depths_iterator(xcb->screen);

	/* find matching depth that has associated visuals */
	while (depth_iter.rem) {
		if (depth_iter.data->depth == depth && depth_iter.data->visuals_len) {
			xcb->depth = depth_iter.data;
			break;
		}
		xcb_depth_next(&depth_iter);
	}

	if (!xcb->depth) {
		fprintf(stderr, "ERROR: Screen does not support %d bit color depth\n",
				depth);
		return false;
	}

	xcb_visualtype_iterator_t visual_iter =
		xcb_depth_visuals_iterator(xcb->depth);

	while (visual_iter.rem) {
		if (visual_iter.data->_class == class) {
			xcb->visual = visual_iter.data;
			break;
		}
		xcb_visualtype_next(&visual_iter);
	}

	if (!xcb->visual) {
		fprintf(stderr,
				"ERROR: Screen does not support specified visual class\n");
		return false;
	}

	return true;
}

static bool set_colormap(struct xcb *xcb)
{
	xcb_colormap_t colormap = xcb_generate_id(xcb->connection);
	xcb_void_cookie_t cookie = xcb_create_colormap_checked(
			xcb->connection,
			XCB_COLORMAP_ALLOC_NONE,
			colormap,
			xcb->screen->root,
			xcb->visual->visual_id);

	xcb_generic_error_t *error = xcb_request_check(xcb->connection, cookie);
	if (error) {
		fprintf(stderr, "ERROR: Failed to create colormap (%s)\n",
				xcb_errors[error->error_code]);
		free(error);
		return false;
	}

	xcb->colormap = colormap;

	return true;
}

static void set_wm_class(struct xcb *xcb)
{
	const char wm_class[] = "Engine\0GameDev";

	xcb_void_cookie_t cookie = xcb_change_property(
			xcb->connection,
			XCB_PROP_MODE_REPLACE,
			xcb->window,
			XCB_ATOM_WM_CLASS,
			XCB_ATOM_STRING,
			8,
			sizeof(wm_class),
			wm_class);

	xcb_generic_error_t *error = xcb_request_check(xcb->connection, cookie);

	if (error) {
		fprintf(stderr, "WARNING: Could not set WM_CLASS (%s)\n",
				xcb_errors[error->error_code]);
		free(error);
	}
}

static bool set_wm_protocols(struct xcb *xcb)
{
	const char protocols[] = "WM_PROTOCOLS";
	const char delete_window[] = "WM_DELETE_WINDOW";

	xcb_intern_atom_cookie_t proto_cookie = xcb_intern_atom(
			xcb->connection,
			false,
			sizeof(protocols) - 1,
			protocols);

	xcb_intern_atom_cookie_t dw_cookie = xcb_intern_atom(
			xcb->connection,
			false,
			sizeof(delete_window) - 1,
			delete_window);

	xcb_intern_atom_reply_t *proto_reply = xcb_intern_atom_reply(
			xcb->connection,
			proto_cookie,
			NULL);

	xcb_intern_atom_reply_t *dw_reply = xcb_intern_atom_reply(
			xcb->connection,
			dw_cookie,
			NULL);

	if (!proto_reply || !dw_reply) {
		fprintf(stderr,
				"ERROR: failed to internalise window manager protocol atoms\n");
		free(proto_reply);
		free(dw_reply);
		return false;
	}

    xcb_void_cookie_t cookie = xcb_change_property(
            xcb->connection,
            XCB_PROP_MODE_REPLACE,
            xcb->window,
            proto_reply->atom,
            4,
            32,
            1,
            &dw_reply->atom);

	xcb->delete_window_atom = dw_reply->atom;

	free(proto_reply);
	free(dw_reply);

	xcb_generic_error_t *error = xcb_request_check(xcb->connection, cookie);
    if (error) {
		fprintf(stderr, "ERROR: failed to set window manager protocols (%s)\n",
				xcb_errors[error->error_code]);
        free(error);
		return false;
    }

	return true;
}

static struct xcb *init_xcb()
{
	int preferred_screen;

	xcb_connection_t *connection = xcb_connect(NULL, &preferred_screen);

	if (xcb_connection_has_error(connection)) {
		fprintf(stderr, "ERROR: Failed to connect to X server\n"); return NULL;
	}

	struct xcb *xcb = calloc(1, sizeof(struct xcb));

	if (!xcb) {
		fprintf(stderr, "ERROR: Failed allocate memory for xcb\n");
		xcb_disconnect(connection);
		return NULL;
	}

	xcb->connection = connection;

	if (!set_preferred_screen(xcb, preferred_screen))
		goto error;

	if (!set_visual(xcb, 32, XCB_VISUAL_CLASS_TRUE_COLOR))
		goto error;

	if (!set_colormap(xcb))
		goto error;

	return xcb;
error:
	xcb_disconnect(xcb->connection);
	free(xcb);
	return NULL;
}

static bool create_xcb_window(struct xcb *xcb, int width, int height)
{
	unsigned int opaque = 0xFF000000;

	/* Must specify border pixel as we're not using the default depth and visual */
	unsigned int cw_mask =
		XCB_CW_BORDER_PIXEL | XCB_CW_BACK_PIXEL | XCB_CW_COLORMAP;

	unsigned int cw_values[] = {
		xcb->screen->white_pixel | opaque,
		xcb->screen->black_pixel | opaque,
		xcb->colormap
	};

	xcb_window_t window = xcb_generate_id(xcb->connection);
	xcb_void_cookie_t cookie = xcb_create_window_checked(
			xcb->connection,
			xcb->depth->depth,
			window,
			xcb->screen->root,
			0, 0,
			width, height,
			1,
			XCB_WINDOW_CLASS_INPUT_OUTPUT,
			xcb->visual->visual_id,
			cw_mask,
			cw_values);

	xcb_generic_error_t *error = xcb_request_check(xcb->connection, cookie);
	if (error) {
		fprintf(stderr, "ERROR: Failed to create window (%s)\n",
				xcb_errors[error->error_code]);
		free(error);
		return false;
	}

	xcb->window = window;

	set_wm_class(xcb);
	set_wm_protocols(xcb);

	xcb_map_window(xcb->connection, xcb->window);
	xcb_flush(xcb->connection);

	return true;
}

static bool handle_xcb_events(struct xcb *xcb)
{
	xcb_generic_event_t *event;

	while ((event = xcb_poll_for_event(xcb->connection))) {
		switch (event->response_type & ~0x80) {
			case XCB_CLIENT_MESSAGE: {
				xcb_client_message_event_t *c_event = (void*)event;
				if (*c_event->data.data32 == xcb->delete_window_atom)
					return false;
			}
		}
	}

	return true;
}

static void close_xcb(struct xcb *xcb)
{
	xcb_disconnect(xcb->connection);
	free(xcb);
}

struct video_driver *init_video_driver(void)
{
	struct video_driver *driver = malloc(sizeof(struct video_driver));
	if (!driver) {
		fprintf(stderr, "ERROR: could not allocate memory for video driver\n");
		return NULL;
	}

	driver->driver_data = init_xcb();

	if (!driver->driver_data) {
		free(driver);
		return NULL;
	}

	return driver;
}

bool create_window(struct video_driver *driver, int width, int height)
{
	return create_xcb_window(driver->driver_data, width, height);
}

bool handle_events(struct video_driver *driver)
{
	return handle_xcb_events(driver->driver_data);
}

void close_video_driver(struct video_driver *driver)
{
	close_xcb(driver->driver_data);
	free(driver);
}

