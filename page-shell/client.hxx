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

namespace page {

struct shell_client {
	struct wl_resource *resource;
	struct wl_client *client;
	struct desktop_shell *shell;
	struct wl_listener destroy_listener;
	struct wl_event_source *ping_timer;
	uint32_t ping_serial;
	int unresponsive;

	shell_client();

};

}

#endif /* PAGE_SHELL_CLIENT_HXX_ */
