/*
 * client.hxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */
#ifndef PAGE_SHELL_CLIENT_HXX_
#define PAGE_SHELL_CLIENT_HXX_

#include "compositor.h"

#include "desktop_shell.hxx"
#include "utils.hxx"
#include "grab_handlers.hxx"

struct shell_surface;

namespace page {

struct shell_client {

	static const struct wl_shell_interface shell_implementation;
	static const struct weston_shell_client shell_client_impl;

	wl_resource *resource;
	wl_client *client;
	desktop_shell *shell;
	cxx_wl_listener<shell_client> destroy_listener;
	wl_event_source *ping_timer;
	uint32_t ping_serial;
	int unresponsive;

	enum api {
		API_SHELL,
		API_XDG
	};

	shell_client(wl_client *client, desktop_shell *shell, api type, uint32_t id);
	~shell_client();

	void handle_shell_client_destroy();

	void shell_client_pong(uint32_t serial);


	/** send SHELL configure **/
	static void send_configure(weston_surface *surface, int32_t width, int32_t height);


	/**
	 * Implementation of server side shell API
	 */

	static void shell_get_shell_surface(wl_client *client, wl_resource *resource, uint32_t id, wl_resource *surface_resource);

};


}

#endif /* PAGE_SHELL_CLIENT_HXX_ */
