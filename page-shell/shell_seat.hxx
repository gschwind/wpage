/*
 * shell_seat.hxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */
#ifndef PAGE_SHELL_SHELL_SEAT_HXX_
#define PAGE_SHELL_SHELL_SEAT_HXX_

#include "compositor.h"
#include "utils.hxx"

namespace page {

struct shell_seat {

	enum type : int32_t { POINTER, TOUCH };

	struct weston_seat *seat;
	cxx_wl_listener<shell_seat> seat_destroy_listener;
	struct weston_surface *focused_surface;

	cxx_wl_listener<shell_seat> caps_changed_listener;
	cxx_wl_listener<shell_seat, struct weston_pointer> pointer_focus_listener;
	cxx_wl_listener<shell_seat, struct weston_keyboard> keyboard_focus_listener;

	struct {
		struct weston_pointer_grab grab;
		struct weston_touch_grab touch_grab;
		struct wl_list surfaces_list;
		struct wl_client *client;
		int32_t initial_up;
		shell_seat::type type;
	} popup_grab;

	shell_seat(struct weston_seat *seat);
	void destroy_shell_seat();
	void handle_keyboard_focus(struct weston_keyboard *keyboard);
	void handle_pointer_focus(struct weston_pointer *pointer);
	void shell_seat_caps_changed();

	static shell_seat * get_shell_seat(weston_seat *seat);

};

}



#endif /* PAGE_SHELL_SHELL_SEAT_HXX_ */
