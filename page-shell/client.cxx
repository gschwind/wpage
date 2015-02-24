/*
 * client.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "compositor.h"
#include "xdg-shell-server-protocol.h"

#include <cassert>

#include "protocols_implementation.hxx"
#include "client.hxx"
#include "surface.hxx"
#include "exception.hxx"
#include "grab_handlers.hxx"



namespace page {

const struct wl_shell_interface shell_client::shell_implementation = {
	shell_client::shell_get_shell_surface
};

const struct weston_shell_client shell_client::shell_client_impl = {
	shell_client::send_configure
};

/**
 * Create structure to handle a client.
 * In particular wl_shell or xdg_shell protocol.
 */
shell_client::shell_client(
		wl_client *client,
		desktop_shell *shell,
		api type,
		uint32_t id) :
resource{nullptr},
client{client},
shell{shell},
ping_timer{nullptr},
ping_serial{0},
unresponsive{0},
destroy_listener{this, &shell_client::handle_shell_client_destroy}
{

	if(type == API_SHELL) {
		this->resource = wl_resource_create(client, &wl_shell_interface, 1, id);
		if (this->resource == nullptr) {
			wl_client_post_no_memory(client);
			throw exception_t("no enough memory");
		}

		/** bind the implementation **/
		wl_resource_set_implementation(resource, &shell_implementation, this, nullptr);
		wl_client_add_destroy_listener(client, &this->destroy_listener.listener);
	} else if (type == API_XDG){
		this->resource = wl_resource_create(client, &xdg_shell_interface, 1, id);
		if (this->resource == nullptr) {
			wl_client_post_no_memory(client);
			throw exception_t("no enough memory");
		}

		/** bind the implementation **/
		wl_resource_set_implementation(resource, &xdg_implementation, this, nullptr);
		wl_client_add_destroy_listener(client, &this->destroy_listener.listener);
	}


}

shell_client::~shell_client() {
	if (ping_timer)
		wl_event_source_remove(ping_timer);
}

void shell_client::handle_shell_client_destroy()
{
	delete this;
}

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
void shell_client::shell_get_shell_surface(
		wl_client *client,
		wl_resource *resource,
		uint32_t id,
		wl_resource *surface_resource)
{
	auto surface =
		reinterpret_cast<weston_surface*>(wl_resource_get_user_data(surface_resource));
	auto sc = reinterpret_cast<page::shell_client*>(wl_resource_get_user_data(resource));
	auto shell = sc->shell;

	if (shell_surface::get_shell_surface(surface)) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "desktop_shell::get_shell_surface already requested");
		return;
	}

	auto shsurf = new shell_surface(sc, reinterpret_cast<void*>(shell), surface, &shell_client::shell_client_impl);
	if (!shsurf) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface->configure already set");
		return;
	}

	shsurf->resource =
		wl_resource_create(client,
				   &wl_shell_surface_interface, 1, id);
	wl_resource_set_implementation(shsurf->resource,
				       &shell_surface::shell_surface_implementation,
				       shsurf, shell_surface::shell_destroy_shell_surface);

}

void shell_client::shell_client_pong(uint32_t serial)
{
	if (this->ping_serial != serial)
		return;

	this->unresponsive = 0;
	end_busy_cursor(this->shell->compositor, this->client);

	if (this->ping_timer) {
		wl_event_source_remove(this->ping_timer);
		this->ping_timer = nullptr;
	}

}

void shell_client::send_configure(weston_surface *surface, int32_t width, int32_t height)
{
	page::shell_surface *shsurf = page::shell_surface::get_shell_surface(surface);
	assert(shsurf);
	if (shsurf->resource)
		wl_shell_surface_send_configure(shsurf->resource, shsurf->resize_edges, width, height);
}

}
