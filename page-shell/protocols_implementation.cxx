/*
 * protocols_implementation.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "protocols_implementation.hxx"


#include <cassert>
#include "client.hxx"
#include "shell.h"


namespace page {

/**
 * Desktop shell interface
 **/

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

static bool
shell_surface_is_xdg_surface(shell_surface *shsurf)
{
	return shsurf->resource &&
		wl_resource_instance_of(shsurf->resource,
					&xdg_surface_interface,
					&page::xdg_surface_implementation);
}

/* xdg-popup implementation */




/* end of xdg-shell implementation */
/***********************************/







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


/**
 * Screensaver interface
 **/

static void screensaver_set_surface(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource, struct wl_resource *output_resource);

const struct screensaver_interface screensaver_implementation = {
	screensaver_set_surface
};

static void
screensaver_configure(struct weston_surface *surface, int32_t sx, int32_t sy)
{
	struct desktop_shell *shell = surface->configure_private;
	struct weston_view *view;
	struct weston_layer_entry *prev;

	if (surface->width == 0)
		return;

	/* XXX: starting weston-screensaver beforehand does not work */
	if (!shell->locked)
		return;

	view = container_of(surface->views.next, struct weston_view, surface_link);
	center_on_output(view, surface->output);

	if (wl_list_empty(&view->layer_link.link)) {
		prev = container_of(shell->lock_layer.view_list.link.prev,
				    struct weston_layer_entry, link);
		weston_layer_entry_insert(prev, &view->layer_link);
		weston_view_update_transform(view);
		wl_event_source_timer_update(shell->screensaver.timer,
					     shell->screensaver.duration);
		shell->shell_fade(FADE_IN);
	}
}

static void
screensaver_set_surface(struct wl_client *client,
			struct wl_resource *resource,
			struct wl_resource *surface_resource,
			struct wl_resource *output_resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct weston_output *output = wl_resource_get_user_data(output_resource);
	struct weston_view *view, *next;

	/* Make sure we only have one view */
	wl_list_for_each_safe(view, next, &surface->views, surface_link)
		weston_view_destroy(view);
	weston_view_create(surface);

	surface->configure = screensaver_configure;
	surface->configure_private = shell;
	surface->output = output;
}


/**
 * Shell interface
 **/

/**
 * Create a shell surface for an existing surface. This gives
 * the wl_surface the role of a shell surface. If the wl_surface
 * already has another role, it raises a protocol error.
 *
 * Only one shell surface can be associated with a given surface.
 *
 * @input client: client where surface_resource belong.
 * @input resource: wl_shell handler for this client. created when client bind wl_shell.
 * @input id: id of the new wl_shell_surface to create.
 * @input surface_resource: the reference wl_surface of wl_shell_surface
 **/
void shell_get_shell_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource);

static const struct wl_shell_interface shell_implementation = {
	shell_get_shell_surface
};

/**
 * Workspace manager interface
 **/

static void workspace_manager_move_surface(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource, uint32_t workspace);

const struct workspace_manager_interface workspace_manager_implementation = {
	workspace_manager_move_surface
};

static void workspace_manager_move_surface(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *surface_resource,
			       uint32_t workspace)
{
	desktop_shell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct weston_surface *main_surface;
	shell_surface *shell_surface;

	main_surface = weston_surface_get_main_surface(surface);
	shell_surface = shell_surface::get_shell_surface(main_surface);
	if (shell_surface == NULL)
		return;

	shell->move_surface_to_workspace(shell_surface, workspace);
}

/**
 * XDG interface (shell replacement)
 **/

static void xdg_use_unstable_version(struct wl_client *client, struct wl_resource *resource, int32_t version);
static void xdg_get_xdg_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, /* created by weston (wl_surface implementation of weston) */ struct wl_resource *surface_resource);
static void xdg_get_xdg_popup(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent_resource, struct wl_resource *seat_resource, uint32_t serial, int32_t x, int32_t y, uint32_t flags);
static void xdg_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial);

const struct xdg_shell_interface xdg_implementation = {
	xdg_use_unstable_version,
	xdg_get_xdg_surface,
	xdg_get_xdg_popup,
	xdg_pong
};

static void xdg_use_unstable_version(struct wl_client *client,
			 struct wl_resource *resource,
			 int32_t version)
{
	if (version > 1) {
		wl_resource_post_error(resource,
				       1,
				       "xdg-shell:: version not implemented yet.");
		return;
	}
}

static shell_surface *
create_xdg_surface(shell_client *owner, void *shell,
		   struct weston_surface *surface,
		   const struct weston_shell_client *client)
{
	shell_surface *shsurf;

	shsurf = new shell_surface(owner, shell, surface, client);
	if (!shsurf)
		return NULL;

	shsurf->type = SHELL_SURFACE_TOPLEVEL;

	return shsurf;
}

static shell_surface *
create_xdg_popup(shell_client *owner, void *shell,
		 struct weston_surface *surface,
		 const struct weston_shell_client *client,
		 struct weston_surface *parent,
		 page::shell_seat *seat,
		 uint32_t serial,
		 int32_t x, int32_t y)
{
	shell_surface *shsurf;

	shsurf = new shell_surface(owner, shell, surface, client);
	if (!shsurf)
		return NULL;

	shsurf->type = SHELL_SURFACE_POPUP;
	shsurf->popup.shseat = seat;
	shsurf->popup.serial = serial;
	shsurf->popup.x = x;
	shsurf->popup.y = y;
	shsurf->shell_surface_set_parent(parent);

	return shsurf;
}


/**
 * Implement xdg-shell::get_xdg_surface
 *
 */
static void
xdg_get_xdg_surface(struct wl_client *client,
		    struct wl_resource *resource,
		    uint32_t id,
			/* created by weston (wl_surface implementation of weston) */
		    struct wl_resource *surface_resource)
{
	auto surface = reinterpret_cast<struct weston_surface *>(wl_resource_get_user_data(surface_resource));
	auto sc = reinterpret_cast<page::shell_client *>(wl_resource_get_user_data(resource));
	struct desktop_shell *shell = sc->shell;
	shell_surface *shsurf;

	if (shell_surface::get_shell_surface(surface)) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "xdg_shell::get_xdg_surface already requested");
		return;
	}

	shsurf = create_xdg_surface(sc, shell, surface, &xdg_client);
	if (!shsurf) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface->configure already set");
		return;
	}

	shsurf->resource =
		wl_resource_create(client,
				   &xdg_surface_interface, 1, id);
	wl_resource_set_implementation(shsurf->resource,
				       &page::xdg_surface_implementation,
				       shsurf, shell_surface::shell_destroy_shell_surface);

	/** force size to 100x100 when xdg_surface is created **/
	xdg_client.send_configure(surface, 157, 277);
	//weston_surface_set_size(surface, 100, 100);

}

static void
xdg_get_xdg_popup(struct wl_client *client,
		  struct wl_resource *resource,
		  uint32_t id,
		  struct wl_resource *surface_resource,
		  struct wl_resource *parent_resource,
		  struct wl_resource *seat_resource,
		  uint32_t serial,
		  int32_t x, int32_t y, uint32_t flags)
{
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	page::shell_client *sc = reinterpret_cast<page::shell_client*>(wl_resource_get_user_data(resource));
	struct desktop_shell *shell = sc->shell;
	shell_surface *shsurf;
	struct weston_surface *parent;
	page::shell_seat *seat;

	if (shell_surface::get_shell_surface(surface)) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "xdg_shell::get_xdg_popup already requested");
		return;
	}

	if (!parent_resource) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "xdg_shell::get_xdg_popup requires a parent shell surface");
		return;
	}

	parent = wl_resource_get_user_data(parent_resource);
	seat = get_shell_seat(wl_resource_get_user_data(seat_resource));;

	shsurf = create_xdg_popup(sc, shell, surface, &xdg_popup_client,
				  parent, seat, serial, x, y);
	if (!shsurf) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface->configure already set");
		return;
	}

	shsurf->resource =
		wl_resource_create(client,
				   &xdg_popup_interface, 1, id);
	wl_resource_set_implementation(shsurf->resource,
				       &xdg_popup_implementation,
				       shsurf, shell_surface::shell_destroy_shell_surface);
}

static void
xdg_pong(struct wl_client *client,
	 struct wl_resource *resource, uint32_t serial)
{
	page::shell_client *sc = reinterpret_cast<page::shell_client*>(wl_resource_get_user_data(resource));

	sc->shell_client_pong(serial);
}


/**
 * XDG popup interface.
 **/

static void xdg_popup_destroy(struct wl_client *client, struct wl_resource *resource);

static const struct xdg_popup_interface xdg_popup_implementation = {
	xdg_popup_destroy,
};

static void
xdg_popup_destroy(struct wl_client *client,
		  struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void xdg_popup_send_configure(struct weston_surface *surface, int32_t width, int32_t height);

const struct weston_shell_client xdg_popup_client = {
	xdg_popup_send_configure
};

static void
xdg_popup_send_configure(struct weston_surface *surface,
			 int32_t width, int32_t height)
{
}

static void xdg_send_configure(struct weston_surface *surface, int32_t width, int32_t height);

static const struct weston_shell_client xdg_client = {
	xdg_send_configure
};

static void xdg_send_configure(struct weston_surface *surface,
		   int32_t width, int32_t height)
{
	shell_surface *shsurf = shell_surface::get_shell_surface(surface);
	uint32_t *s;
	struct wl_array states;
	uint32_t serial;

	assert(shsurf);

	if (!shsurf->resource)
		return;

	wl_array_init(&states);
	if (shsurf->requested_state.fullscreen) {
		s = wl_array_add(&states, sizeof *s);
		*s = XDG_SURFACE_STATE_FULLSCREEN;
	} else if (shsurf->requested_state.maximized) {
		s = wl_array_add(&states, sizeof *s);
		*s = XDG_SURFACE_STATE_MAXIMIZED;
	}
	if (shsurf->resize_edges != 0) {
		s = wl_array_add(&states, sizeof *s);
		*s = XDG_SURFACE_STATE_RESIZING;
	}
	if (shsurf->focus_count > 0) {
		s = wl_array_add(&states, sizeof *s);
		*s = XDG_SURFACE_STATE_ACTIVATED;
	}

	serial = wl_display_next_serial(shsurf->surface->compositor->wl_display);
	xdg_surface_send_configure(shsurf->resource, width, height, &states, serial);

	printf("xdg_send_configure %d (%d,%d)\n", serial, width, height);
	wl_array_release(&states);
}


}

