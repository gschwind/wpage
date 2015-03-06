/*
 * workspace.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "workspace.hxx"
#include "focus_state.hxx"
#include "shell.h"

workspace::workspace()
{
	weston_layer_init(&this->layer, nullptr);
	wl_list_init(&this->focus_list);
	//wl_list_init(&this->seat_destroyed_listener.link);
	//this->seat_destroyed_listener.notify = seat_destroyed;
	this->fsurf_front = nullptr;
	this->fsurf_back = nullptr;
	this->focus_animation = nullptr;
}

workspace::~workspace()
{
	focus_state *state, *next;

	wl_list_for_each_safe(state, next, &this->focus_list, link)
		focus_state_destroy(state);

	if (this->fsurf_front)
		focus_surface_destroy(this->fsurf_front);
	if (this->fsurf_back)
		focus_surface_destroy(this->fsurf_back);

}
