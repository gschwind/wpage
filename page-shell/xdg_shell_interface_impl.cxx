/*
 * xdg_shell_interface_impl.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "compositor.h"
#include "surface.hxx"
#include "workspace.hxx"
#include "notebook.hxx"


namespace page {


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

	auto n = filter_class<notebook_t>(shell->workspaces.array[0]->tree_t::get_all_children());
	n[0]->add_client(shsurf, true);

	shell->update_default_layer();
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

}

