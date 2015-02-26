/*
 * shell_surface_interface_impl.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "protocols_implementation.hxx"
#include "surface.hxx"
#include "client.hxx"

#include <cstring>

namespace page {

/**
 * Shell surface interface.
 **/

static void shell_surface_pong(wl_client *client, wl_resource *resource, uint32_t serial);
static void shell_surface_move(wl_client *client, wl_resource *resource, wl_resource *seat_resource, uint32_t serial);
static void shell_surface_resize(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, uint32_t edges);
static void shell_surface_set_toplevel(struct wl_client *client, struct wl_resource *resource);
static void shell_surface_set_transient(struct wl_client *client, struct wl_resource *resource, struct wl_resource *parent_resource, int x, int y, uint32_t flags);
static void shell_surface_set_fullscreen(struct wl_client *client, struct wl_resource *resource, uint32_t method, uint32_t framerate, struct wl_resource *output_resource);
static void shell_surface_set_popup(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, struct wl_resource *parent_resource, int32_t x, int32_t y, uint32_t flags);
static void shell_surface_set_maximized(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource);
static void shell_surface_set_title(struct wl_client *client, struct wl_resource *resource, const char *title);
static void shell_surface_set_class(struct wl_client *client, struct wl_resource *resource, const char *class_name);

const struct wl_shell_surface_interface shell_surface_implementation = {
	shell_surface_pong,
	shell_surface_move,
	shell_surface_resize,
	shell_surface_set_toplevel,
	shell_surface_set_transient,
	shell_surface_set_fullscreen,
	shell_surface_set_popup,
	shell_surface_set_maximized,
	shell_surface_set_title,
	shell_surface_set_class
};

void shell_surface_pong(wl_client *client, wl_resource *resource, uint32_t serial)
{
	auto shsurf = reinterpret_cast<shell_surface*>(wl_resource_get_user_data(resource));
	shsurf->owner->shell_client_pong(serial);
}

void shell_surface_move(wl_client *client, wl_resource *resource, wl_resource *seat_resource, uint32_t serial)
{
	shell_surface::common_surface_move(resource, seat_resource, serial);
}

void shell_surface_resize(struct wl_client *client, struct wl_resource *resource,
		     struct wl_resource *seat_resource, uint32_t serial,
		     uint32_t edges)
{
	shell_surface::common_surface_resize(resource, seat_resource, serial, edges);
}


void shell_surface_set_toplevel(struct wl_client *client,
			   struct wl_resource *resource)
{
	shell_surface *surface = wl_resource_get_user_data(resource);

	weston_shell_interface_impl.set_toplevel(surface);
}

void shell_surface_set_transient(struct wl_client *client,
			    struct wl_resource *resource,
			    struct wl_resource *parent_resource,
			    int x, int y, uint32_t flags)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);
	struct weston_surface *parent =
		wl_resource_get_user_data(parent_resource);

	weston_shell_interface_impl.set_transient(shsurf, parent, x, y, flags);
}

void shell_surface_set_fullscreen(struct wl_client *client,
			     struct wl_resource *resource,
			     uint32_t method,
			     uint32_t framerate,
			     struct wl_resource *output_resource)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);
	struct weston_output *output;

	if (output_resource)
		output = wl_resource_get_user_data(output_resource);
	else
		output = NULL;

	shsurf->shell_surface_set_parent(nullptr);

	shsurf->surface_clear_next_states();
	shsurf->next_state.fullscreen = true;
	shsurf->state_changed = true;
	weston_shell_interface_impl.set_fullscreen(shsurf, method, framerate, output);
}

void shell_surface_set_popup(struct wl_client *client,
			struct wl_resource *resource,
			struct wl_resource *seat_resource,
			uint32_t serial,
			struct wl_resource *parent_resource,
			int32_t x, int32_t y, uint32_t flags)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);
	struct weston_surface *parent =
		wl_resource_get_user_data(parent_resource);

	shsurf->shell_surface_set_parent(parent);

	shsurf->surface_clear_next_states();
	shsurf->set_popup(
	          parent,
	          wl_resource_get_user_data(seat_resource),
	          serial, x, y);
}

void shell_surface_set_maximized(struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *output_resource)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);
	struct weston_output *output;

	shsurf->surface_clear_next_states();
	shsurf->next_state.maximized = true;
	shsurf->state_changed = true;

	shsurf->type = SHELL_SURFACE_TOPLEVEL;
	shsurf->shell_surface_set_parent(nullptr);

	if (output_resource)
		output = wl_resource_get_user_data(output_resource);
	else
		output = NULL;

	shsurf->shell_surface_set_output(output);

	shsurf->send_configure_for_surface();
}

void shell_surface_set_title(struct wl_client *client,
			struct wl_resource *resource, const char *title)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);

	page::weston_shell_interface_impl.set_title(shsurf, title);
}

void shell_surface_set_class(struct wl_client *client,
			struct wl_resource *resource, const char *class_)
{
	shell_surface *shsurf = wl_resource_get_user_data(resource);

	free(shsurf->class_);
	shsurf->class_ = strdup(class_);
}

}
