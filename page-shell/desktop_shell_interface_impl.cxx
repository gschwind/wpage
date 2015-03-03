/*
 * desktop_shell_interface_impl.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "compositor.h"
#include "desktop-shell-server-protocol.h"
#include "client.hxx"
#include "shell.h"

namespace page {

static void desktop_shell_set_background(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource, struct wl_resource *surface_resource);
static void desktop_shell_set_panel(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource, struct wl_resource *surface_resource);
static void desktop_shell_set_lock_surface(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource);
static void desktop_shell_unlock(struct wl_client *client, struct wl_resource *resource);
static void desktop_shell_set_grab_surface(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource);
static void desktop_shell_desktop_ready(struct wl_client *client, struct wl_resource *resource);
static void desktop_shell_set_panel_position(struct wl_client *client, struct wl_resource *resource, uint32_t position);

const struct desktop_shell_interface desktop_shell_implementation = {
	desktop_shell_set_background,
	desktop_shell_set_panel,
	desktop_shell_set_lock_surface,
	desktop_shell_unlock,
	desktop_shell_set_grab_surface,
	desktop_shell_desktop_ready,
	desktop_shell_set_panel_position
};



static void
configure_static_view(struct weston_view *ev, struct weston_layer *layer)
{
	struct weston_view *v, *next;

	wl_list_for_each_safe(v, next, &layer->view_list.link, layer_link.link) {
		if (v->output == ev->output && v != ev) {
			weston_view_unmap(v);
			v->surface->configure = NULL;
		}
	}

	weston_view_set_position(ev, ev->output->x, ev->output->y);

	if (wl_list_empty(&ev->layer_link.link)) {
		weston_layer_entry_insert(&layer->view_list, &ev->layer_link);
		weston_compositor_schedule_repaint(ev->surface->compositor);
	}
}

static void
background_configure(struct weston_surface *es, int32_t sx, int32_t sy)
{
	struct desktop_shell *shell = es->configure_private;
	struct weston_view *view;

	view = container_of(es->views.next, struct weston_view, surface_link);

	configure_static_view(view, &shell->background_layer);
}

void
desktop_shell_set_background(struct wl_client *client,
			     struct wl_resource *resource,
			     struct wl_resource *output_resource,
			     struct wl_resource *surface_resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct weston_view *view, *next;

	if (surface->configure) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface role already assigned");
		return;
	}

	wl_list_for_each_safe(view, next, &surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_view_create(surface);

	surface->configure = background_configure;
	surface->configure_private = shell;
	surface->output = wl_resource_get_user_data(output_resource);
	view->output = surface->output;
	desktop_shell_send_configure(resource, 0,
				     surface_resource,
				     surface->output->width,
				     surface->output->height);
}


static void
panel_configure(struct weston_surface *es, int32_t sx, int32_t sy)
{
	struct desktop_shell *shell = es->configure_private;
	struct weston_view *view;

	view = container_of(es->views.next, struct weston_view, surface_link);

	configure_static_view(view, &shell->panel_layer);
}


void
desktop_shell_set_panel(struct wl_client *client,
			struct wl_resource *resource,
			struct wl_resource *output_resource,
			struct wl_resource *surface_resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct weston_view *view, *next;

	if (surface->configure) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface role already assigned");
		return;
	}

	wl_list_for_each_safe(view, next, &surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_view_create(surface);

	surface->configure = panel_configure;
	surface->configure_private = shell;
	surface->output = wl_resource_get_user_data(output_resource);
	view->output = surface->output;
	desktop_shell_send_configure(resource, 0,
				     surface_resource,
				     surface->output->width,
				     surface->output->height);
}



static void
lock_surface_configure(struct weston_surface *surface, int32_t sx, int32_t sy)
{
	struct desktop_shell *shell = surface->configure_private;
	struct weston_view *view;

	view = container_of(surface->views.next, struct weston_view, surface_link);

	if (surface->width == 0)
		return;

	center_on_output(view, get_default_output(shell->compositor));

	if (!weston_surface_is_mapped(surface)) {
		weston_layer_entry_insert(&shell->lock_layer.view_list,
					  &view->layer_link);
		weston_view_update_transform(view);
		shell->shell_fade(FADE_IN);
	}
}

static void
handle_lock_surface_destroy(struct wl_listener *listener, void *data)
{
	struct desktop_shell *shell =
	    container_of(listener, struct desktop_shell, lock_surface_listener);

	weston_log("lock surface gone\n");
	shell->lock_surface = NULL;
}

static void
desktop_shell_set_lock_surface(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *surface_resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);

	shell->prepare_event_sent = false;

	if (!shell->locked)
		return;

	shell->lock_surface = surface;

	shell->lock_surface_listener.notify = handle_lock_surface_destroy;
	wl_signal_add(&surface->destroy_signal,
		      &shell->lock_surface_listener);

	weston_view_create(surface);
	surface->configure = lock_surface_configure;
	surface->configure_private = shell;
}

static void
desktop_shell_unlock(struct wl_client *client,
		     struct wl_resource *resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	shell->prepare_event_sent = false;

	if (shell->locked)
		shell->resume_desktop();
}

static void
desktop_shell_set_grab_surface(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *surface_resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	shell->grab_surface = wl_resource_get_user_data(surface_resource);
	weston_view_create(shell->grab_surface);
}

static void
desktop_shell_desktop_ready(struct wl_client *client,
			    struct wl_resource *resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	shell->shell_fade_startup();
}

static void
desktop_shell_set_panel_position(struct wl_client *client,
				 struct wl_resource *resource,
				 uint32_t position)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	if (position != DESKTOP_SHELL_PANEL_POSITION_TOP &&
	    position != DESKTOP_SHELL_PANEL_POSITION_BOTTOM &&
	    position != DESKTOP_SHELL_PANEL_POSITION_LEFT &&
	    position != DESKTOP_SHELL_PANEL_POSITION_RIGHT) {
		wl_resource_post_error(resource,
				       DESKTOP_SHELL_ERROR_INVALID_ARGUMENT,
				       "bad position argument");
		return;
	}

	shell->panel_position = position;
}




}
