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

namespace page {

struct shell_client {
	wl_resource *resource;
	wl_client *client;
	desktop_shell *shell;
	wl_listener destroy_listener;
	wl_event_source *ping_timer;
	uint32_t ping_serial;
	int unresponsive;

	shell_client();

};

}

#endif /* PAGE_SHELL_CLIENT_HXX_ */
