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

namespace page {

struct shell_seat {

	enum type : int32_t { POINTER, TOUCH };

	struct weston_seat *seat;
	struct wl_listener seat_destroy_listener;
	struct weston_surface *focused_surface;

	struct wl_listener caps_changed_listener;
	struct wl_listener pointer_focus_listener;
	struct wl_listener keyboard_focus_listener;

	struct {
		struct weston_pointer_grab grab;
		struct weston_touch_grab touch_grab;
		struct wl_list surfaces_list;
		struct wl_client *client;
		int32_t initial_up;
		shell_seat::type type;
	} popup_grab;
};

}



#endif /* PAGE_SHELL_SHELL_SEAT_HXX_ */
