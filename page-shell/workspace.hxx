/*
 * workspace.hxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */
#ifndef PAGE_SHELL_WORKSPACE_HXX_
#define PAGE_SHELL_WORKSPACE_HXX_

#include "compositor.h"

struct workspace {
	struct weston_layer layer;

	struct wl_list focus_list;
	//struct wl_listener seat_destroyed_listener;

	struct focus_surface *fsurf_front;
	struct focus_surface *fsurf_back;
	struct weston_view_animation *focus_animation;


	workspace();
	~workspace();

};



#endif /* PAGE_SHELL_WORKSPACE_HXX_ */
