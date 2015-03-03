/*
 * shell_seat.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include <cassert>
#include "shell_seat.hxx"
#include "grab_handlers.hxx"
#include "shell.h"

namespace page {

shell_seat::shell_seat(struct weston_seat *seat) :
seat_destroy_listener{this, &shell_seat::destroy_shell_seat},
keyboard_focus_listener{this, &shell_seat::handle_keyboard_focus},
pointer_focus_listener{this, &shell_seat::handle_pointer_focus},
caps_changed_listener{this, &shell_seat::shell_seat_caps_changed},
seat{nullptr},
focused_surface{nullptr},
popup_grab{0}
{

	this->seat = seat;
	wl_list_init(&this->popup_grab.surfaces_list);

	wl_signal_add(&seat->destroy_signal, &this->seat_destroy_listener.listener);
	wl_signal_add(&seat->updated_caps_signal, &this->caps_changed_listener.listener);
	shell_seat_caps_changed();
}

void shell_seat::destroy_shell_seat()
{
	shell_surface *shsurf, *prev = NULL;

	if (this->popup_grab.grab.interface == &popup_grab_interface) {
		weston_pointer_end_grab(this->popup_grab.grab.pointer);
		this->popup_grab.client = NULL;

		wl_list_for_each(shsurf, &this->popup_grab.surfaces_list, popup.grab_link) {
			shsurf->popup.shseat = NULL;
			if (prev) {
				wl_list_init(&prev->popup.grab_link);
			}
			prev = shsurf;
		}
		wl_list_init(&prev->popup.grab_link);
	}

	wl_list_remove(&this->seat_destroy_listener.listener.link);
	free(this);
}

void shell_seat::handle_keyboard_focus(struct weston_keyboard *keyboard)
{
	page::shell_seat *seat = get_shell_seat(keyboard->seat);

	if (seat->focused_surface) {
		shell_surface *shsurf = shell_surface::get_shell_surface(seat->focused_surface);
		if (shsurf)
			shsurf->shell_surface_lose_keyboard_focus();
	}

	seat->focused_surface = keyboard->focus;

	if (seat->focused_surface) {
		shell_surface *shsurf = shell_surface::get_shell_surface(seat->focused_surface);
		if (shsurf)
			shsurf->shell_surface_gain_keyboard_focus();
	}
}

void shell_seat::handle_pointer_focus(struct weston_pointer *pointer)
{
	struct weston_view *view = pointer->focus;
	struct weston_compositor *compositor;
	uint32_t serial;

	if (!view)
		return;

	compositor = view->surface->compositor;
	serial = wl_display_next_serial(compositor->wl_display);
	ping_handler(view->surface, serial);
}

void shell_seat::shell_seat_caps_changed()
{
	if (this->seat->keyboard &&
	    wl_list_empty(&this->keyboard_focus_listener.listener.link)) {
		wl_signal_add(&this->seat->keyboard->focus_signal,
			      &this->keyboard_focus_listener.listener);
	} else if (!this->seat->keyboard) {
		wl_list_init(&this->keyboard_focus_listener.listener.link);
	}

	if (this->seat->pointer &&
	    wl_list_empty(&this->pointer_focus_listener.listener.link)) {
		wl_signal_add(&this->seat->pointer->focus_signal,
			      &this->pointer_focus_listener.listener);
	} else if (!this->seat->pointer) {
		wl_list_init(&this->pointer_focus_listener.listener.link);
	}
}

shell_seat * shell_seat::get_shell_seat(weston_seat *seat)
{
	cxx_wl_listener<shell_seat> * listener;
	listener = reinterpret_cast<cxx_wl_listener<shell_seat> *>(wl_signal_get(&seat->destroy_signal, reinterpret_cast<wl_notify_func_t>(cxx_wl_listener<shell_seat>::call)));
	assert(listener != nullptr);
	return listener->data;
}

}
