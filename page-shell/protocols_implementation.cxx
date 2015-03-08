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
#include "shell_seat.hxx"

namespace page {


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

/**
 * destroy - destroy xdg_shell
 *
 * Destroy this xdg_shell object.
 *
 * Destroying a bound xdg_shell object while there are surfaces
 * still alive with roles from this interface is illegal and will
 * result in a protocol error. Make sure to destroy all surfaces
 * before destroying this object.
 */
static void xdg_destroy(struct wl_client *client, struct wl_resource *resource);

/**
 * use_unstable_version - enable use of this unstable version
 * @version: (none)
 *
 * Negotiate the unstable version of the interface. This
 * mechanism is in place to ensure client and server agree on the
 * unstable versions of the protocol that they speak or exit
 * cleanly if they don't agree. This request will go away once the
 * xdg-shell protocol is stable.
 */
static void xdg_use_unstable_version(struct wl_client *client,
			     struct wl_resource *resource,
			     int32_t version);
/**
 * get_xdg_surface - create a shell surface from a surface
 * @id: (none)
 * @surface: (none)
 *
 * This creates an xdg_surface for the given surface and gives it
 * the xdg_surface role. See the documentation of xdg_surface for
 * more details.
 */
static void xdg_get_xdg_surface(struct wl_client *client,
			struct wl_resource *resource,
			uint32_t id,
			struct wl_resource *surface);
/**
 * get_xdg_popup - create a popup for a surface
 * @id: (none)
 * @surface: (none)
 * @parent: (none)
 * @seat: the wl_seat of the user event
 * @serial: the serial of the user event
 * @x: (none)
 * @y: (none)
 *
 * This creates an xdg_popup for the given surface and gives it
 * the xdg_popup role. See the documentation of xdg_popup for more
 * details.
 *
 * This request must be used in response to some sort of user
 * action like a button press, key press, or touch down event.
 */
static void xdg_get_xdg_popup(struct wl_client *client,
		      struct wl_resource *resource,
		      uint32_t id,
		      struct wl_resource *surface,
		      struct wl_resource *parent,
		      struct wl_resource *seat,
		      uint32_t serial,
		      int32_t x,
		      int32_t y);
/**
 * pong - respond to a ping event
 * @serial: serial of the ping event
 *
 * A client must respond to a ping event with a pong request or
 * the client may be deemed unresponsive.
 */
static void xdg_pong(struct wl_client *client,
	     struct wl_resource *resource,
	     uint32_t serial);

const struct xdg_shell_interface xdg_implementation = {
	xdg_destroy,
	xdg_use_unstable_version,
	xdg_get_xdg_surface,
	xdg_get_xdg_popup,
	xdg_pong
};

static void
xdg_destroy(struct wl_client *client,
		  struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

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
	//xdg_client.send_configure(surface, 157, 277);
	//weston_surface_set_size(surface, 100, 100);

}

/**
 * Create a popup window
 **/
static void
xdg_get_xdg_popup(struct wl_client *client,
		  struct wl_resource *resource,
		  uint32_t id,
		  struct wl_resource *surface_resource,
		  struct wl_resource *parent_resource,
		  struct wl_resource *seat_resource,
		  uint32_t serial,
		  int32_t x, int32_t y)
{
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct shell_client *sc = wl_resource_get_user_data(resource);
	struct desktop_shell *shell = sc->shell;
	struct shell_surface *shsurf;
	struct weston_surface *parent;
	struct shell_seat *seat;

	shsurf = shell_surface::get_shell_surface(surface);
	if (shsurf && shsurf->shell_surface_is_xdg_popup()) {
		wl_resource_post_error(resource, XDG_SHELL_ERROR_ROLE,
				       "This wl_surface is already an "
				       "xdg_popup");
		return;
	}

	if (weston_surface_set_role(surface, "xdg_popup",
				    resource, XDG_SHELL_ERROR_ROLE) < 0)
		return;

	if (!parent_resource) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "xdg_shell::get_xdg_popup requires a parent shell surface");
		return;
	}

	parent = wl_resource_get_user_data(parent_resource);
	seat = shell_seat::get_shell_seat(wl_resource_get_user_data(seat_resource));;

	shsurf = create_xdg_popup(sc, shell, surface, &xdg_popup_client,
				  parent, seat, serial, x, y);
	if (!shsurf) {
		wl_resource_post_no_memory(surface_resource);
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

const struct xdg_popup_interface xdg_popup_implementation = {
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

const struct weston_shell_client xdg_client = {
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

