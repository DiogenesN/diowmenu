/* diowmenu
 * Left click on the button toggles the menu
 * Right click on the button terminates the application
 */

#include <stdio.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include "runcmd.h"
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include "externvars.h"
#include "configsgen.c"
#include "create-shm.h"
#include <wayland-client.h>
#include <wayland-cursor.h>
#include "getvaluefromconf.h"
#include <wayland-client-core.h>
#include "getstrfromsubstrinfile.h"
#include <librsvg-2.0/librsvg/rsvg.h>
#include "xdg-shell-client-protocol.h"
#include "process-directory-desktop-files.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

int endPoint = 40;
const int fontSize = 25;
const int startPoint = 70;
const int frameThickness = 1;
static int nrOfItems = 0;
static int xdg_serial = 0;
static int parentAppId = 0;
static bool popupEntered = false;
static bool panelClicked = false;
static bool parentEntered = false;
char *config = NULL;
char *cacheFilePath = NULL;

enum pointer_event_mask {
	POINTER_EVENT_ENTER = 1 << 0,
	POINTER_EVENT_LEAVE = 1 << 1,
	POINTER_EVENT_MOTION = 1 << 2,
	POINTER_EVENT_BUTTON = 1 << 3,
	POINTER_EVENT_AXIS_DISCRETE = 1 << 7,
};

struct pointer_event {
	uint32_t event_mask;
	wl_fixed_t surface_x;
	wl_fixed_t surface_y;
	uint32_t button;
	uint32_t state;
	uint32_t time;
	uint32_t serial;
	struct {
		bool valid;
		wl_fixed_t value;
		int32_t discrete;
	} axes[2];
};

// main state struct
struct client_state {
	/* Globals */
	struct wl_shm *wl_shm;
	struct wl_display *wl_display;
	struct wl_registry *wl_registry;
    struct xdg_wm_base *xdg_wm_base;
	struct wl_compositor *wl_compositor;
	struct wl_seat *wl_seat;
	/* Objects */
	struct wl_surface *wl_surface;
	struct wl_surface *wl_surface_popup;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_pointer *wl_pointer;
	struct wl_cursor *wl_cursor;
	struct wl_buffer *wl_cursor_buffer;
	struct wl_cursor_theme *wl_cursor_theme;
	struct wl_surface *wl_cursor_surface;
	struct wl_cursor_image *wl_cursor_image;
	/* State */
	struct pointer_event pointer_event;
	int32_t width;
	int32_t height;
	int32_t width_popup;
	int32_t height_popup;
	int32_t x_popup;
	int32_t y_popup;
	int32_t x_motion;
	int32_t y_motion;
	bool closed;
};

/* Dummy function */
static void noop() {}

/* Global functions */
static const struct wl_registry_listener wl_registry_listener;
static const struct xdg_surface_listener xdg_surface_listener;
static const struct wl_callback_listener wl_surface_frame_listener;
static struct wl_buffer *draw_frame_popup(struct client_state *state);
static const struct zwlr_layer_surface_v1_listener layer_surface_listener;
static void zwlr_layer_surface_close(void *data, struct zwlr_layer_surface_v1 *surface);

/* Pointer events */
static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
						struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	(void)wl_pointer;
	struct client_state *client_state = data;

	/// disable evens for panel button and make it only work on the child (toplevel)
	int popupAppId = wl_proxy_get_id((struct wl_proxy *)surface);
	if (parentAppId == 0) {
		parentAppId = wl_proxy_get_id((struct wl_proxy *)surface);
	}

	if (popupAppId > parentAppId) {
		///printf("Entered Popup\n");
		popupEntered = true;
		/* requestion server to notify the client when it's good time to draw a new frame
		 * by emitting 'done' event
		 */
		struct wl_callback *cb = wl_display_sync(client_state->wl_display);
		wl_callback_add_listener(cb, &wl_surface_frame_listener, client_state);
	}
	else {
		///printf("Entered parent\n");
		parentEntered = true;
	}

	client_state->pointer_event.event_mask |= POINTER_EVENT_ENTER;
	client_state->pointer_event.serial = serial;
	client_state->pointer_event.surface_x = surface_x,
	client_state->pointer_event.surface_y = surface_y;
	/// Set our pointer               
	wl_pointer_set_cursor(wl_pointer,
						  client_state->pointer_event.serial,
						  client_state->wl_cursor_surface,
						  client_state->wl_cursor_image->hotspot_x,
						  client_state->wl_cursor_image->hotspot_y);
}

static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
																struct wl_surface *surface) {
	(void)surface;
	(void)wl_pointer;
	popupEntered = false;
	parentEntered = false;
	struct client_state *client_state = data;
	client_state->pointer_event.serial = serial;
	client_state->pointer_event.event_mask |= POINTER_EVENT_LEAVE;
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time,
												wl_fixed_t surface_x, wl_fixed_t surface_y) {
	(void)wl_pointer;
	struct client_state *client_state = data;
	client_state->pointer_event.event_mask |= POINTER_EVENT_MOTION;
	client_state->pointer_event.time = time;
	client_state->pointer_event.surface_x = surface_x,
	client_state->pointer_event.surface_y = surface_y;
}

static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
               								uint32_t time, uint32_t button, uint32_t state) {
	(void)wl_pointer;
	struct client_state *client_state = data;
	client_state->pointer_event.event_mask |= POINTER_EVENT_BUTTON;
	client_state->pointer_event.time = time;
	client_state->pointer_event.serial = serial;
	client_state->pointer_event.button = button,
	client_state->pointer_event.state = state;
}

static void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
													uint32_t axis, int32_t discrete) {
	(void)wl_pointer;
	struct client_state *client_state = data;
	client_state->pointer_event.event_mask |= POINTER_EVENT_AXIS_DISCRETE;
	client_state->pointer_event.axes[axis].valid = true;
	client_state->pointer_event.axes[axis].discrete = discrete;
}

static int count_items_in_directory(const char *path) {
	DIR *dir;
	struct dirent *entry;
	int count = 0;
	// Open the directory
	dir = opendir(path);
	if (dir == NULL) {
		perror("opendir");
		return -1;
	}
	// Read each directory entry
	while ((entry = readdir(dir)) != NULL) {
		// Skip the "." and ".." entries
		if (entry->d_name[0] == '.' && (entry->d_name[1] == '\0' || 
			(entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
			continue;
		}
		count = count + 1;
	}
	// Close the directory
	if (closedir(dir) != 0) {
		perror("closedir");
		return -1;
	}
	return count;
}

static void initial_popup_open(struct client_state *state) {
	xdg_surface_set_window_geometry(state->xdg_surface, state->x_popup, state->y_popup,
														state->width_popup, state->height_popup);
	wl_surface_commit(state->wl_surface_popup);
	panelClicked = true;
	///printf("------------------ First time panel opened -----------------\n");
	return;
}

static void close_popup(struct client_state *state) {
	if (state->xdg_toplevel) {
		xdg_toplevel_destroy(state->xdg_toplevel);
		state->xdg_toplevel = NULL;
	}
	if (state->xdg_surface) {
		xdg_surface_destroy(state->xdg_surface);
		state->xdg_surface = NULL;
	}
    if (state->xdg_wm_base) {
		xdg_wm_base_destroy(state->xdg_wm_base);
		state->xdg_wm_base = NULL;
	}
    if (state->wl_surface_popup) {
		wl_surface_destroy(state->wl_surface_popup);
		state->wl_surface_popup = NULL;
    }
	if (state->wl_registry) {
		wl_registry_destroy(state->wl_registry);
		state->wl_registry = NULL;
	}
	panelClicked = false;
	popupEntered = false;
	item_counter = 0;
	///printf("----------------------- Popup Closed -----------------------\n");
	return;
}

static void show_popup(struct client_state *state) {
	close_popup(state);
	if (!state->wl_registry) {
		state->wl_registry = wl_display_get_registry(state->wl_display);
		wl_registry_add_listener(state->wl_registry, &wl_registry_listener, state);
		wl_display_roundtrip(state->wl_display);
	}
	if (!state->wl_surface_popup) {
		state->wl_surface_popup = wl_compositor_create_surface(state->wl_compositor);
		wl_surface_commit(state->wl_surface_popup);
	}
	if (!state->xdg_surface) {
		state->xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base,
																	state->wl_surface_popup);
		xdg_surface_add_listener(state->xdg_surface, &xdg_surface_listener, state);
		state->xdg_toplevel = xdg_surface_get_toplevel(state->xdg_surface);
		xdg_toplevel_set_app_id(state->xdg_toplevel, "org.Diogenes.diowmenu");
		xdg_surface_set_window_geometry(state->xdg_surface, state->x_popup, state->y_popup,
																	state->width_popup,
																	state->height_popup);
		wl_surface_commit(state->wl_surface_popup);
	}
	panelClicked = true;
	///printf("------------------ Second time panel opened ----------------\n");
	return;
}

/* Panel enter, popup open/leave, wheel sctoll and click actions */
static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
	(void)wl_pointer;
	struct client_state *state = data;
	struct pointer_event *event = &state->pointer_event;

	if (event->event_mask & POINTER_EVENT_ENTER) {
		///fprintf(stderr, "Entered\n");
		zwlr_layer_surface_v1_set_size(state->layer_surface, 20, 20);
		wl_surface_commit(state->wl_surface);
	}

	if (!parentEntered && event->event_mask & POINTER_EVENT_LEAVE) {
		///fprintf(stderr, "Left\n");
		zwlr_layer_surface_v1_set_size(state->layer_surface, 1, 1);
		wl_surface_commit(state->wl_surface);
	}

	// Clicking on the panel button
	char *stateev = event->state == WL_POINTER_BUTTON_STATE_RELEASED ? "released" : "pressed";
	if (event->event_mask & POINTER_EVENT_BUTTON && strcmp(stateev, "pressed") == 0) {
		// Get the number if items (.desktop files) in items folder
		const char *HOME = getenv("HOME");
		const char *dirConfigItems	= "/.config/diowmenu/items";
		char dirConfigItemsBuff[strlen(HOME) + strlen(dirConfigItems) + 1];
		snprintf(dirConfigItemsBuff, sizeof(dirConfigItemsBuff), "%s%s", HOME, dirConfigItems);
		nrOfItems = count_items_in_directory(dirConfigItemsBuff);
		// Populate names, execs and icons but only if there are .desktop files in utems/
		if (nrOfItems > 0) {
			item_counter = 0;
			process_directory(dirConfigItemsBuff);
		}
		///Tprintf(stderr, "button %d %s\n", event->button, stateev);
		// Terminate the application
		if (event->button == 273) {
			state->closed = true;
			return;
		}
		/// toggle open/close popup when clicking on the panel
		if (panelClicked && !popupEntered) {
			///printf("----------------------- Popup Closing ----------------------\n");
			close_popup(state);
		}
		else if (xdg_serial == 0 && !panelClicked && !popupEntered) {
			///printf("----------------- First time panel opening -----------------\n");
			// opens popup the first time
			initial_popup_open(state);
		}
		else if (xdg_serial != 0 && !panelClicked && !popupEntered) {
			///printf("----------------- Second time panel opening ----------------\n");
			// open popup the second and all the next times
			show_popup(state);
		}
		// Clicking inside popup
		else if (popupEntered) {
			///printf("-------------------- Cliked inside popup --------------------\n");
			// Prevent clicking the edges (fixing crashes)
			if (state->y_motion < 7 || state->y_motion > 695) {
				return;
			}
			// Clicking reboot button
			///fprintf(stderr, "x=%d y=%d\n", state->x_motion, state->y_motion);
			if (state->x_motion > 525 && state->y_motion > 553 && state->y_motion < 612) {
				printf("Reboot clicked\n");
				const char *reboot = get_char_value_from_conf(config, "reboot_command");
				printf("reboot: %s\n", reboot);
				run_cmd((char *)reboot);
				free((void *)reboot);
				reboot = NULL;
				return;
			}
			// Clicking shutdown button
			if (state->x_motion > 525 && state->y_motion > 638 && state->y_motion < 699) {
				printf("Shutdown clicked\n");
				const char *poweroff = get_char_value_from_conf(config, "poweroff_command");
				run_cmd((char *)poweroff);
				free((void *)poweroff);
				poweroff = NULL;
				return;
			}
			///int currentSelectedLine = state->y_motion / 55;
			///printf("current Selected Line: %d\n", currentSelectedLine);
			// Run selected item
			run_cmd(execs[state->y_motion / 55]);
			/// after the selected window is raised close the popup window
			close_popup(state);
		}
	}
	if (event->event_mask & POINTER_EVENT_MOTION) {
		state->x_motion = wl_fixed_to_int(event->surface_x);
		state->y_motion = wl_fixed_to_int(event->surface_y);
		///fprintf(stderr, "x=%d\n", state->x_motion);
	}
	///fprintf(stderr, "y\n");
	memset(event, 0, sizeof(*event));
}

static const struct wl_pointer_listener wl_pointer_listener = {
	.enter = wl_pointer_enter,
	.leave = wl_pointer_leave,
	.motion = wl_pointer_motion,
	.button = wl_pointer_button,
	.axis = noop,
	.frame = wl_pointer_frame,
	.axis_source = noop,
	.axis_stop = noop,
	.axis_discrete = wl_pointer_axis_discrete,
};

/*************************************************************************************************/
/************************************** DRAWING  POPUP *******************************************/
/*************************************************************************************************/
static void wl_buffer_release_popup(void *data, struct wl_buffer *wl_buffer) {
	/* Sent by the compositor when it's no longer using this buffer */
	(void)data;
	wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener_popup = {
	.release = wl_buffer_release_popup,
};
			
static struct wl_buffer *draw_frame_popup(struct client_state *state) {
	int32_t width = state->width_popup;
	int32_t height = (state->height_popup + 21);
	int32_t stride = width * 4;
	int32_t size = stride * height;
	int32_t fd_popup = fd_popup = allocate_shm_file(size);
	if (fd_popup == -1) {
		return NULL;
	}
	uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_popup, 0);
	if (data == MAP_FAILED) {
		close(fd_popup);
		return NULL;
	}

	/// drawing titles
	struct wl_shm_pool *pool_popup = wl_shm_create_pool(state->wl_shm, fd_popup, size);
	struct wl_buffer *buffer_popup = wl_shm_pool_create_buffer(pool_popup, 0, width, height, stride,
																	WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool_popup);
	close(fd_popup);

	// cairo drawing
	cairo_surface_t *surface = cairo_image_surface_create_for_data((unsigned char *)data,
																		CAIRO_FORMAT_RGB24,
																		width,
																		height,
																		stride);
	cairo_t *cr = cairo_create(surface);
	cairo_paint(cr);

	// Grey color
	cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);

	// Draw frame around light blue
	cairo_set_source_rgba(cr, 0.0, 0.8, 0.8, 1.0);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_stroke(cr);

	// Darker grey color inner rectangle
	cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 1.0);
	cairo_rectangle(cr, 15, 15, width - 100, height - 30);
	cairo_fill(cr);

	// Draw inner frame light blue
	cairo_set_source_rgba(cr, 0.0, 0.3, 0.3, 1.0);
	cairo_set_line_width(cr, 5);
	cairo_rectangle(cr, 15, 15, width - 100, height - 30);
	cairo_stroke(cr);

	// Draw shutdown button
	// Draw big red circle inside
	int xpos = 558;
	int ypos = 670;
	cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
	cairo_arc(cr, xpos, ypos, 30, 0, 2 * M_PI);
	cairo_fill(cr);

	// Draw inner smaller circle
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_set_line_width(cr, 7);
	cairo_arc(cr, xpos, ypos, 17, 0, 2 * M_PI);
	cairo_stroke(cr);

	// Draw vertical thick red line
	cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
	cairo_set_line_width(cr, 13);
	cairo_move_to(cr, xpos, ypos - 20);
	cairo_line_to(cr, xpos, ypos);
	cairo_stroke(cr);

	// Draw vertical thin white line
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_set_line_width(cr, 7);
	cairo_move_to(cr, xpos, ypos - 20);
	cairo_line_to(cr, xpos, ypos);
	cairo_stroke(cr);

	// Draw reboot button
	// Draw big green circle inside
	cairo_set_source_rgb(cr, 0.0, 1.0, 0.0);
	cairo_arc(cr, xpos, ypos - 85, 30, 0, 2 * M_PI);
	cairo_fill(cr);

	// Draw inner smaller circle
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_set_line_width(cr, 7);
	cairo_arc(cr, xpos, ypos - 85, 17, 0, 2 * M_PI);
	cairo_stroke(cr);

	// Draw arrow
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_set_line_width(cr, 5);
	cairo_move_to(cr, xpos + 9, ypos - 84);
	cairo_line_to(cr, xpos + 10, ypos - 73);
	cairo_stroke(cr);

	// Draw arrow
	cairo_set_source_rgb(cr, 0.0, 1.0, 0.0);
	cairo_set_line_width(cr, 5);
	cairo_move_to(cr, xpos + 3, ypos - 85);
	cairo_line_to(cr, xpos + 10, ypos - 60);
	cairo_stroke(cr);

	// Draw arrow 2
	cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
	cairo_set_line_width(cr, 5);
	cairo_move_to(cr, xpos + 23, ypos - 77);
	cairo_line_to(cr, xpos + 10, ypos - 70);
	cairo_stroke(cr);

	// Highlighting reboot icon
	if (state->x_motion > 530 && state->y_motion > 553 && state->y_motion < 612) {
		// Draw inner frame light blue
		cairo_set_source_rgb(cr, 0.0, 0.9, 1.0);
		cairo_set_line_width(cr, 3);
		cairo_rectangle(cr, 522, 550, 70, 70);
		cairo_stroke(cr);
	}
	// Highlighling shutdown icon
	if (state->x_motion > 530 && state->y_motion > 638 && state->y_motion < 699) {
		// Draw inner frame light blue
		cairo_set_source_rgb(cr, 0.0, 0.9, 1.0);
		cairo_set_line_width(cr, 3);
		cairo_rectangle(cr, 522, 633, 70, 70);
		cairo_stroke(cr);
	}

	// If .desktop files were added to .config/diowmenu/items directory
	if (nrOfItems > 0) {
		// Creating the list of all apps
		int pos_y = 55;
		cairo_set_font_size(cr, 30);
		for (int i = 0; names[i] != NULL; i++) {
			cairo_move_to(cr, 77, pos_y);
			// Highlighting selected item in the list
			if (popupEntered && state->x_motion < 530 && state->y_motion / 55 == i) {
				cairo_set_source_rgb(cr, 0.0, 0.9, 1.0);
			}
			else {
				cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
			}
			// Trim the name to 20 characters
			if (strlen(names[i]) > 25) {
				names[i][21] = ' ';
				names[i][22] = '.';
				names[i][23] = '.';
				names[i][24] = '.';
				names[i][25] = '\0';
			}
			cairo_show_text(cr, names[i]);

			// Drawing icons
			const RsvgRectangle rect = {
				.x = 20,
				.y = pos_y - 33,
				.width = 45,
				.height = 45,
			};
			RsvgHandle *svgIcon = rsvg_handle_new_from_file(icons[i], NULL);
			rsvg_handle_render_document(svgIcon, cr, &rect, NULL);
			// Destroy cairo
			g_object_unref(svgIcon);

			pos_y = pos_y + 52;
		}
	}
	else {
		cairo_move_to(cr, 23, height / 2);
		cairo_set_font_size(cr, 20);
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
		cairo_show_text(cr, "No .desktop files added to .config/diowmenu/items");
	}

	// destroy cairo
	cairo_surface_flush(surface); // Flush changes to the surface
	cairo_surface_destroy(surface);
	cairo_destroy(cr);
	munmap(data, size);

	/// destroy the buffer
	wl_buffer_add_listener(buffer_popup, &wl_buffer_listener_popup, NULL);
	return buffer_popup;
}

static void wl_surface_frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)time;
	struct client_state *state = data;
	/* Destroy this callback */
	wl_callback_destroy(cb);
	cb = wl_display_sync(state->wl_display);
	if (popupEntered) {
		/// unsleep(1000000) = 1 sexond, reduce framerate, throtlle CPU usage
		usleep(150000);
		wl_callback_add_listener(cb, &wl_surface_frame_listener, state);
	}
	else {
		wl_callback_destroy(cb);
	}
	/* Submit a frame for this event */
	struct wl_buffer *buffer = draw_frame_popup(state);
	if (panelClicked || popupEntered) {
		/// fix: surface must not have a buffer attached ...
		wl_surface_attach(state->wl_surface_popup, buffer, 0, 0);
		wl_surface_damage_buffer(state->wl_surface_popup, 0, 0, INT32_MAX, INT32_MAX);
		wl_surface_commit(state->wl_surface_popup);
	}
}

static const struct wl_callback_listener wl_surface_frame_listener = {
	.done = wl_surface_frame_done,
};

/*************************************************************************************************/
/************************************** DRAWING WIDGET *******************************************/
/*************************************************************************************************/
static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
	/// Sent by the compositor when it's no longer using this buffer
	(void)data;
	wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
	.release = wl_buffer_release,
};

static struct wl_buffer *draw_frame(struct client_state *state) {
	/// this function runs in a loop
	int width = state->width;
	int height = state->height;
	int stride = width * 4;
	int size = stride * height;
	int fd = allocate_shm_file(size);

	uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	struct wl_shm_pool *pool = wl_shm_create_pool(state->wl_shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
																	WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	// cairo drawing
	cairo_surface_t *surface = cairo_image_surface_create_for_data((unsigned char *)data,
																		CAIRO_FORMAT_RGB24,
																		width,
																		height,
																		stride);
	cairo_t *cr = cairo_create(surface);
	cairo_paint(cr);

	// Grey color
	cairo_set_source_rgba(cr, 0.2, 0.2, 0.2, 1.0);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_fill(cr);

	// Draw frame around light blue
	cairo_set_source_rgba(cr, 0.0, 0.8, 0.8, 1.0);
	cairo_rectangle(cr, 0, 0, width, height);
	cairo_stroke(cr);

	// destroy cairo
	cairo_surface_flush(surface); // Flush changes to the surface
	cairo_surface_destroy(surface);
	cairo_destroy(cr);
	munmap(data, size);
	/// very important to release the buffer to prevent memory leak
	wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
	return buffer;
}

static void wl_seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
	(void)wl_seat;
	struct client_state *state = data;
	bool have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
	if (have_pointer && state->wl_pointer == NULL) {
		state->wl_pointer = wl_seat_get_pointer(state->wl_seat);
		wl_pointer_add_listener(state->wl_pointer, &wl_pointer_listener, state);
	}
	else if (!have_pointer && state->wl_pointer != NULL) {
		wl_pointer_release(state->wl_pointer);
		state->wl_pointer = NULL;
	}
}

static const struct wl_seat_listener wl_seat_listener = {
	.capabilities = wl_seat_capabilities,
	.name = noop,
};

// configuring popup, attaching buffer
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	xdg_serial = serial;
    struct client_state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    struct wl_buffer *buffer = draw_frame_popup(state);
    wl_surface_attach(state->wl_surface_popup, buffer, 0, 0);
    wl_surface_commit(state->wl_surface_popup);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
	(void)data;
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *wl_registry, uint32_t name,
													const char *interface, uint32_t version) {
	(void)version;
	struct client_state *state = data;
	if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->wl_shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
	}
	else if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->wl_compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, 4);
	}
	else if (strcmp(interface, wl_seat_interface.name) == 0) {
		state->wl_seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, 7);
		wl_seat_add_listener(state->wl_seat, &wl_seat_listener, state);
	}
	else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		/// Bind Layer Shell interface
		state->layer_shell = wl_registry_bind(wl_registry, name, &zwlr_layer_shell_v1_interface, 1);
	}
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base = wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
    }
}

static void registry_global_remove(void *data, struct wl_registry *wl_registry, uint32_t name) {
	(void)data;
	(void)name;
	(void)wl_registry;
	/* This space deliberately left blank */
}

static const struct wl_registry_listener wl_registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

/* configure zwlr_layer_surface_v1 */
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
											uint32_t serial, uint32_t width, uint32_t height) {
	(void)width;
	(void)height;
	struct client_state *state = data;
	zwlr_layer_surface_v1_ack_configure(surface, serial);
	struct wl_buffer *buffer = draw_frame(state);
	wl_surface_attach(state->wl_surface, buffer, 0, 0);
	wl_surface_commit(state->wl_surface);
}

static void zwlr_layer_surface_close(void *data, struct zwlr_layer_surface_v1 *surface) {
	(void)surface;
	struct client_state *state = data;
	state->closed = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = zwlr_layer_surface_close,
};

int main(void) {
	/// generate initial config
	create_configs();

	/// setting up the global config file path
	const char *HOME = getenv("HOME");
	const char *configPath = "/.config/diowmenu/diowmenu.conf";
	const char *iconCachePath = "/.config/diowmenu/icons.cache";
	config = malloc(sizeof(char) * strlen(HOME) + strlen(configPath) + 3);
	cacheFilePath = malloc(sizeof(char) * strlen(HOME) + strlen(iconCachePath) + 3);
	snprintf(config, strlen(HOME) + strlen(configPath) + 3, "%s%s", HOME, configPath);
	snprintf(cacheFilePath, strlen(HOME) + strlen(iconCachePath) + 3, "%s%s", HOME, iconCachePath);

	struct client_state state = { 0 };
	state.x_popup = get_int_value_from_conf(config, "posx");
	state.y_popup = get_int_value_from_conf(config, "posy");
	state.width_popup = 600;
	state.height_popup = 700;
	state.width = 20;
	state.height = 20;

	state.wl_display = wl_display_connect(NULL);
	state.wl_registry = wl_display_get_registry(state.wl_display);
	wl_registry_add_listener(state.wl_registry, &wl_registry_listener, &state);
	wl_display_roundtrip(state.wl_display);
	/// set cursor visible on surface
	state.wl_cursor_theme = wl_cursor_theme_load(NULL, 24, state.wl_shm);
	state.wl_cursor = wl_cursor_theme_get_cursor(state.wl_cursor_theme, "left_ptr");
	state.wl_cursor_image = state.wl_cursor->images[0];
	state.wl_cursor_buffer = wl_cursor_image_get_buffer(state.wl_cursor_image);
	state.wl_cursor_surface = wl_compositor_create_surface(state.wl_compositor);
	wl_surface_attach(state.wl_cursor_surface, state.wl_cursor_buffer, 0, 0);
	wl_surface_commit(state.wl_cursor_surface);
	/// end of cursor setup
	state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
	state.wl_surface_popup = wl_compositor_create_surface(state.wl_compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.wl_surface_popup);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_app_id(state.xdg_toplevel, "org.Diogenes.diowmenu");
	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell,
																state.wl_surface,
																NULL,
																ZWLR_LAYER_SHELL_V1_LAYER_TOP,
																"");
	zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener, &state);
	zwlr_layer_surface_v1_set_size(state.layer_surface, state.width, state.height);
	zwlr_layer_surface_v1_set_anchor(state.layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | \
																ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	/// committing the surface showing the panel
	wl_surface_commit(state.wl_surface);

	while (!state.closed && wl_display_dispatch(state.wl_display) != -1) {
		/* This space deliberately left blank */
	}

	/// free resources
	free(config);
	config = NULL;
	free((void *)cacheFilePath);
	cacheFilePath = NULL;
	if (state.wl_cursor_buffer) {
		wl_buffer_destroy(state.wl_cursor_buffer);
	}
	if (state.wl_cursor_surface) {
		wl_surface_destroy(state.wl_cursor_surface);
	}
	if (state.xdg_toplevel) {
		xdg_toplevel_destroy(state.xdg_toplevel);
	}
	if (state.xdg_surface) {
		xdg_surface_destroy(state.xdg_surface);
	}
	if (state.wl_surface) {
		wl_surface_destroy(state.wl_surface);
	}
	if (state.wl_seat) {
		wl_seat_destroy(state.wl_seat);
	}
	if (state.xdg_wm_base) {
		xdg_wm_base_destroy(state.xdg_wm_base);
	}
	if (state.wl_registry) {
		wl_registry_destroy(state.wl_registry);
	}
	if (state.wl_display) {
		wl_display_disconnect(state.wl_display);
	}
	fprintf(stderr, "Wayland client terminated!\n");

    return 0;
}
