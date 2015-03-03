/*
 * grab_handlers.hxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */
#ifndef PAGE_SHELL_GRAB_HANDLERS_HXX_
#define PAGE_SHELL_GRAB_HANDLERS_HXX_

#include <linux/input.h>
#include "compositor.h"
#include "surface.hxx"

namespace page {

struct shell_grab {
	struct weston_pointer_grab grab;
	shell_surface *shsurf;
	struct wl_listener shsurf_destroy_listener;
};

struct shell_touch_grab {
	struct weston_touch_grab grab;
	shell_surface *shsurf;
	struct wl_listener shsurf_destroy_listener;
	struct weston_touch *touch;
};

struct weston_move_grab {
	struct shell_grab base;
	wl_fixed_t dx, dy;
	int client_initiated;
};

struct weston_touch_move_grab {
	struct shell_touch_grab base;
	int active;
	wl_fixed_t dx, dy;
};

struct rotate_grab {
	struct shell_grab base;
	struct weston_matrix rotation;
	struct {
		float x;
		float y;
	} center;
};


struct weston_resize_grab {
	struct shell_grab base;
	uint32_t edges;
	int32_t width, height;
};

extern const struct weston_pointer_grab_interface move_grab_interface;
extern const struct weston_pointer_grab_interface popup_grab_interface;
extern const struct weston_pointer_grab_interface busy_cursor_grab_interface;
extern const struct weston_pointer_grab_interface rotate_grab_interface;
extern const struct weston_pointer_grab_interface resize_grab_interface;

extern const struct weston_touch_grab_interface touch_move_grab_interface;
extern const struct weston_touch_grab_interface touch_popup_grab_interface;

void end_busy_cursor(struct weston_compositor *compositor, struct wl_client *client);

void shell_grab_start(struct shell_grab *grab, const struct weston_pointer_grab_interface *interface, shell_surface *shsurf, struct weston_pointer *pointer, enum desktop_shell_cursor cursor);
void shell_grab_end(struct shell_grab *grab);

void popup_grab_end(struct weston_pointer *pointer);
void touch_popup_grab_end(struct weston_touch *touch);

void shell_touch_grab_start(struct shell_touch_grab *grab, const struct weston_touch_grab_interface *interface, shell_surface *shsurf, struct weston_touch *touch);
void shell_touch_grab_end(struct shell_touch_grab *grab);

void shell_surface_send_popup_done(shell_surface *shsurf);

}

#endif /* PAGE_SHELL_GRAB_HANDLERS_HXX_ */
