/*
 * exposay.hxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */
#ifndef PAGE_SHELL_EXPOSAY_HXX_
#define PAGE_SHELL_EXPOSAY_HXX_

enum exposay_target_state {
	EXPOSAY_TARGET_OVERVIEW, /* show all windows */
	EXPOSAY_TARGET_CANCEL, /* return to normal, same focus */
	EXPOSAY_TARGET_SWITCH, /* return to normal, switch focus */
};

enum exposay_layout_state {
	EXPOSAY_LAYOUT_INACTIVE = 0, /* normal desktop */
	EXPOSAY_LAYOUT_ANIMATE_TO_INACTIVE, /* in transition to normal */
	EXPOSAY_LAYOUT_OVERVIEW, /* show all windows */
	EXPOSAY_LAYOUT_ANIMATE_TO_OVERVIEW, /* in transition to all windows */
};

struct exposay_output {
	int num_surfaces;
	int grid_size;
	int surface_size;

	int hpadding_outer;
	int vpadding_outer;
	int padding_inner;
};

struct exposay {
	/* XXX: Make these exposay_surfaces. */
	struct weston_view *focus_prev;
	struct weston_view *focus_current;
	struct weston_view *clicked;
	struct workspace *workspace;
	struct weston_seat *seat;

	struct wl_list surface_list;

	struct weston_keyboard_grab grab_kbd;
	struct weston_pointer_grab grab_ptr;

	enum exposay_target_state state_target;
	enum exposay_layout_state state_cur;
	int in_flight; /* number of animations still running */

	int row_current;
	int column_current;
	struct exposay_output *cur_output;

	bool mod_pressed;
	bool mod_invalid;
};

void x_exposay_binding(struct weston_seat *seat, weston_keyboard_modifier modifier, void *data);


#endif /* PAGE_SHELL_EXPOSAY_HXX_ */
