/*
 * protocols_implementation.hxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */
#ifndef PAGE_SHELL_PROTOCOLS_IMPLEMENTATION_HXX_
#define PAGE_SHELL_PROTOCOLS_IMPLEMENTATION_HXX_

#include "compositor.h"

#include "desktop-shell-server-protocol.h"
#include "workspaces-server-protocol.h"
#include "xdg-shell-server-protocol.h"

/**
 * This is the list of interfaces protocols that have to be implemented
 * By the *window manager*
 **/


namespace page {
extern const struct desktop_shell_interface desktop_shell_implementation;
extern const struct screensaver_interface screensaver_implementation;
extern const struct workspace_manager_interface workspace_manager_implementation;
extern const struct xdg_shell_interface xdg_implementation;
extern const struct xdg_popup_interface xdg_popup_implementation;
extern const struct xdg_surface_interface xdg_surface_implementation;
extern const struct wl_shell_surface_interface shell_surface_implementation;
extern const struct weston_shell_interface weston_shell_interface_impl;
extern const struct weston_shell_client xdg_client;
extern const struct weston_shell_client xdg_popup_client;
extern const struct screensaver_interface screensaver_implementation;
}

#endif /* PAGE_SHELL_PROTOCOLS_IMPLEMENTATION_HXX_ */
