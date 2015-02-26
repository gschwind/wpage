/*
 * xdg_surface_interface_impl.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include <cstring>
#include <cassert>

#include "compositor.h"
#include "surface.hxx"
#include "shell.h"

/**
 * XDG surface interface implementation.
 **/

namespace page {

static void xdg_surface_destroy(struct wl_client *client, struct wl_resource *resource);
static void xdg_surface_set_parent(struct wl_client *client, struct wl_resource *resource, struct wl_resource *parent_resource);
static void xdg_surface_set_title(struct wl_client *client, struct wl_resource *resource, const char *title);
static void xdg_surface_set_app_id(struct wl_client *client, struct wl_resource *resource, const char *app_id);
static void xdg_surface_show_window_menu(struct wl_client *client, struct wl_resource *surface_resource, struct wl_resource *seat_resource, uint32_t serial, int32_t x, int32_t y);
static void xdg_surface_move(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial);
static void xdg_surface_resize(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, uint32_t edges);
static void xdg_surface_ack_configure(struct wl_client *client, struct wl_resource *resource, uint32_t serial);
static void xdg_surface_set_window_geometry(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height);
static void xdg_surface_set_maximized(struct wl_client *client, struct wl_resource *resource);
static void xdg_surface_unset_maximized(struct wl_client *client, struct wl_resource *resource);
static void xdg_surface_set_fullscreen(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource);
static void xdg_surface_unset_fullscreen(struct wl_client *client, struct wl_resource *resource);
static void xdg_surface_set_minimized(struct wl_client *client, struct wl_resource *resource);

const struct xdg_surface_interface xdg_surface_implementation = {
	xdg_surface_destroy,
	xdg_surface_set_parent,
	xdg_surface_set_title,
	xdg_surface_set_app_id,
	xdg_surface_show_window_menu,
	xdg_surface_move,
	xdg_surface_resize,
	xdg_surface_ack_configure,
	xdg_surface_set_window_geometry,
	xdg_surface_set_maximized,
	xdg_surface_unset_maximized,
	xdg_surface_set_fullscreen,
	xdg_surface_unset_fullscreen,
	xdg_surface_set_minimized,
};

static void
xdg_surface_destroy(struct wl_client *client,
		    struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
xdg_surface_set_parent(struct wl_client *client,
		       struct wl_resource *resource,
		       struct wl_resource *parent_resource)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);
	struct weston_surface *parent;

	if (parent_resource)
		parent = wl_resource_get_user_data(parent_resource);
	else
		parent = NULL;

	shsurf->shell_surface_set_parent(parent);
}

static void
xdg_surface_set_app_id(struct wl_client *client,
		       struct wl_resource *resource,
		       const char *app_id)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);

	free(shsurf->class_);
	shsurf->class_ = strdup(app_id);
}

static void
xdg_surface_show_window_menu(struct wl_client *client,
			     struct wl_resource *surface_resource,
			     struct wl_resource *seat_resource,
			     uint32_t serial,
			     int32_t x,
			     int32_t y)
{
	/* TODO */
}

static void
xdg_surface_set_title(struct wl_client *client,
			struct wl_resource *resource, const char *title)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);

	page::weston_shell_interface_impl.set_title(shsurf, title);
}

static void
xdg_surface_move(struct wl_client *client, struct wl_resource *resource,
		 struct wl_resource *seat_resource, uint32_t serial)
{
	shell_surface::common_surface_move(resource, seat_resource, serial);
}

static void
xdg_surface_resize(struct wl_client *client, struct wl_resource *resource,
		   struct wl_resource *seat_resource, uint32_t serial,
		   uint32_t edges)
{
	shell_surface::common_surface_resize(resource, seat_resource, serial, edges);
}

static void
xdg_surface_ack_configure(struct wl_client *client,
			  struct wl_resource *resource,
			  uint32_t serial)
{
	printf("xdg_surface_ack_configure %d\n", serial);
	shell_surface *shsurf = wl_resource_get_user_data(resource);

	if (shsurf->state_requested) {
		shsurf->next_state = shsurf->requested_state;
		shsurf->state_changed = true;
		shsurf->state_requested = false;
	}
}

static void
xdg_surface_set_window_geometry(struct wl_client *client,
				struct wl_resource *resource,
				int32_t x,
				int32_t y,
				int32_t width,
				int32_t height)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);

	page::weston_shell_interface_impl.set_window_geometry(shsurf, x, y, width, height);
}

static struct weston_output *
get_focused_output(struct weston_compositor *compositor)
{
	struct weston_seat *seat;
	struct weston_output *output = NULL;

	wl_list_for_each(seat, &compositor->seat_list, link) {
		/* Priority has touch focus, then pointer and
		 * then keyboard focus. We should probably have
		 * three for loops and check frist for touch,
		 * then for pointer, etc. but unless somebody has some
		 * objections, I think this is sufficient. */
		if (seat->touch && seat->touch->focus)
			output = seat->touch->focus->output;
		else if (seat->pointer && seat->pointer->focus)
			output = seat->pointer->focus->output;
		else if (seat->keyboard && seat->keyboard->focus)
			output = seat->keyboard->focus->output;

		if (output)
			break;
	}

	return output;
}

static void
xdg_surface_set_maximized(struct wl_client *client,
			  struct wl_resource *resource)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);
	struct weston_output *output;

	shsurf->state_requested = true;
	shsurf->requested_state.maximized = true;

	if (!weston_surface_is_mapped(shsurf->surface))
		output = get_focused_output(shsurf->surface->compositor);
	else
		output = shsurf->surface->output;

	shsurf->shell_surface_set_output(output);
	shsurf->send_configure_for_surface();
}

static void
xdg_surface_unset_maximized(struct wl_client *client,
			    struct wl_resource *resource)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);

	shsurf->state_requested = true;
	shsurf->requested_state.maximized = false;
	shsurf->send_configure_for_surface();
}

static void
xdg_surface_set_fullscreen(struct wl_client *client,
			   struct wl_resource *resource,
			   struct wl_resource *output_resource)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);
	struct weston_output *output;

	shsurf->state_requested = true;
	shsurf->requested_state.fullscreen = true;

	if (output_resource)
		output = wl_resource_get_user_data(output_resource);
	else
		output = NULL;

	/* handle clients launching in fullscreen */
	if (output == NULL && !weston_surface_is_mapped(shsurf->surface)) {
		/* Set the output to the one that has focus currently. */
		assert(shsurf->surface);
		output = get_focused_output(shsurf->surface->compositor);
	}

	shsurf->shell_surface_set_output(output);
	shsurf->fullscreen_output = shsurf->output;

	shsurf->send_configure_for_surface();
}

static void
xdg_surface_unset_fullscreen(struct wl_client *client,
			     struct wl_resource *resource)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);

	shsurf->state_requested = true;
	shsurf->requested_state.fullscreen = false;
	shsurf->send_configure_for_surface();
}

static void
set_minimized(struct weston_surface *surface, uint32_t is_true)
{
	shell_surface *shsurf;
	struct workspace *current_ws;
	struct weston_seat *seat;
	struct weston_surface *focus;
	struct weston_view *view;

	view = get_default_view(surface);
	if (!view)
		return;

	assert(weston_surface_get_main_surface(view->surface) == view->surface);

	shsurf = shell_surface::get_shell_surface(surface);
	current_ws = get_current_workspace(shsurf->shell);

	weston_layer_entry_remove(&view->layer_link);
	 /* hide or show, depending on the state */
	if (is_true) {
		weston_layer_entry_insert(&shsurf->shell->minimized_layer.view_list, &view->layer_link);

		shsurf->shell->drop_focus_state(current_ws, view->surface);
		wl_list_for_each(seat, &shsurf->shell->compositor->seat_list, link) {
			if (!seat->keyboard)
				continue;
			focus = weston_surface_get_main_surface(seat->keyboard->focus);
			if (focus == view->surface)
				weston_keyboard_set_focus(seat->keyboard, NULL);
		}
	}
	else {
		weston_layer_entry_insert(&current_ws->layer.view_list, &view->layer_link);

		wl_list_for_each(seat, &shsurf->shell->compositor->seat_list, link) {
			if (!seat->keyboard)
				continue;
			activate(shsurf->shell, view->surface, seat, true);
		}
	}

	shsurf->shell_surface_update_child_surface_layers();

	weston_view_damage_below(view);
}

static void
xdg_surface_set_minimized(struct wl_client *client,
			    struct wl_resource *resource)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);

	if (shsurf->type != SHELL_SURFACE_TOPLEVEL)
		return;

	 /* apply compositor's own minimization logic (hide) */
	set_minimized(shsurf->surface, 1);
}

}
