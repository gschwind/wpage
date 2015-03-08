/*
 * focus_state.hxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */
#ifndef PAGE_SHELL_FOCUS_STATE_HXX_
#define PAGE_SHELL_FOCUS_STATE_HXX_

namespace page {
	struct workspace;
}

struct focus_surface {
	struct weston_surface *surface;
	struct weston_view *view;
	struct weston_transform workspace_transform;
};

struct shell_output {
	struct desktop_shell  *shell;
	struct weston_output  *output;
	struct wl_listener    destroy_listener;
	struct wl_list        link;
};


struct focus_state {
	struct weston_seat *seat;
	page::workspace *ws;
	struct weston_surface *keyboard_focus;
	struct wl_list link;
	struct wl_listener seat_destroy_listener;
	struct wl_listener surface_destroy_listener;


	void focus_state_set_focus(struct weston_surface *surface)
	{
		if (this->keyboard_focus) {
			wl_list_remove(&this->surface_destroy_listener.link);
			wl_list_init(&this->surface_destroy_listener.link);
		}

		this->keyboard_focus = surface;
		if (surface)
			wl_signal_add(&surface->destroy_signal,
				      &this->surface_destroy_listener);
	}


};



#endif /* PAGE_SHELL_FOCUS_STATE_HXX_ */
