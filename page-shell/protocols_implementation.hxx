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

#include "desktop-shell-server-protocol.h"
#include "workspaces-server-protocol.h"
#include "xdg-shell-server-protocol.h"

/**
 * This is the list of interfaces protocols that have to be implemented
 * By the *window manager*
 **/

const struct desktop_shell_interface desktop_shell_implementation;
const struct screensaver_interface screensaver_implementation;
const struct wl_shell_interface shell_implementation;
const struct wl_shell_surface_interface shell_surface_implementation;
const struct workspace_manager_interface workspace_manager_implementation;
const struct xdg_shell_interface xdg_implementation;
const struct xdg_popup_interface xdg_popup_implementation;
const struct xdg_surface_interface xdg_surface_implementation;

void
desktop_shell_set_background(struct wl_client *client,
		struct wl_resource *resource, struct wl_resource *output_resource,
		struct wl_resource *surface_resource);

void
desktop_shell_set_panel(struct wl_client *client,
			struct wl_resource *resource,
			struct wl_resource *output_resource,
			struct wl_resource *surface_resource);

void
desktop_shell_set_lock_surface(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *surface_resource);
void
desktop_shell_unlock(struct wl_client *client,
		     struct wl_resource *resource);

void
desktop_shell_set_grab_surface(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *surface_resource);

void
desktop_shell_desktop_ready(struct wl_client *client,
			    struct wl_resource *resource);

void
desktop_shell_set_panel_position(struct wl_client *client,
				 struct wl_resource *resource,
				 uint32_t position);

void
screensaver_set_surface(struct wl_client *client,
			struct wl_resource *resource,
			struct wl_resource *surface_resource,
			struct wl_resource *output_resource);

void
shell_get_shell_surface(struct wl_client *client,
			struct wl_resource *resource,
			uint32_t id,
			struct wl_resource *surface_resource);

static void
shell_surface_pong(struct wl_client *client,
		   struct wl_resource *resource, uint32_t serial);

static void
shell_surface_move(struct wl_client *client, struct wl_resource *resource,
		   struct wl_resource *seat_resource, uint32_t serial);
static void
shell_surface_resize(struct wl_client *client, struct wl_resource *resource,
		     struct wl_resource *seat_resource, uint32_t serial,
		     uint32_t edges);

static void
shell_surface_set_toplevel(struct wl_client *client,
			   struct wl_resource *resource);

static void
shell_surface_set_transient(struct wl_client *client,
			    struct wl_resource *resource,
			    struct wl_resource *parent_resource,
			    int x, int y, uint32_t flags);

static void
shell_surface_set_fullscreen(struct wl_client *client,
			     struct wl_resource *resource,
			     uint32_t method,
			     uint32_t framerate,
			     struct wl_resource *output_resource);

static void
shell_surface_set_popup(struct wl_client *client,
			struct wl_resource *resource,
			struct wl_resource *seat_resource,
			uint32_t serial,
			struct wl_resource *parent_resource,
			int32_t x, int32_t y, uint32_t flags);

static void
shell_surface_set_maximized(struct wl_client *client,
                            struct wl_resource *resource,
                            struct wl_resource *output_resource);

static void
shell_surface_set_title(struct wl_client *client,
			struct wl_resource *resource, const char *title);

static void
shell_surface_set_class_(struct wl_client *client,
			struct wl_resource *resource, const char *class_);

static void
workspace_manager_move_surface(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *surface_resource,
			       uint32_t workspace);

static void
xdg_use_unstable_version(struct wl_client *client,
			 struct wl_resource *resource,
			 int32_t version);

static void
xdg_get_xdg_surface(struct wl_client *client,
		    struct wl_resource *resource,
		    uint32_t id,
			/* created by weston (wl_surface implementation of weston) */
		    struct wl_resource *surface_resource);

static void
xdg_get_xdg_popup(struct wl_client *client,
		  struct wl_resource *resource,
		  uint32_t id,
		  struct wl_resource *surface_resource,
		  struct wl_resource *parent_resource,
		  struct wl_resource *seat_resource,
		  uint32_t serial,
		  int32_t x, int32_t y, uint32_t flags);

static void
xdg_pong(struct wl_client *client,
	 struct wl_resource *resource, uint32_t serial);

static void
xdg_popup_destroy(struct wl_client *client,
		  struct wl_resource *resource);

static void
xdg_surface_destroy(struct wl_client *client,
		    struct wl_resource *resource);
static void
xdg_surface_set_parent(struct wl_client *client,
		       struct wl_resource *resource,
		       struct wl_resource *parent_resource);
static void
xdg_surface_set_title(struct wl_client *client,
			struct wl_resource *resource, const char *title);

static void
xdg_surface_set_app_id(struct wl_client *client,
		       struct wl_resource *resource,
		       const char *app_id);

static void
xdg_surface_show_window_menu(struct wl_client *client,
			     struct wl_resource *surface_resource,
			     struct wl_resource *seat_resource,
			     uint32_t serial,
			     int32_t x,
			     int32_t y);

static void
xdg_surface_move(struct wl_client *client, struct wl_resource *resource,
		 struct wl_resource *seat_resource, uint32_t serial);

static void
xdg_surface_resize(struct wl_client *client, struct wl_resource *resource,
		   struct wl_resource *seat_resource, uint32_t serial,
		   uint32_t edges);

static void
xdg_surface_ack_configure(struct wl_client *client,
			  struct wl_resource *resource,
			  uint32_t serial);

static void
xdg_surface_set_window_geometry(struct wl_client *client,
				struct wl_resource *resource,
				int32_t x,
				int32_t y,
				int32_t width,
				int32_t height);

static void
xdg_surface_set_maximized(struct wl_client *client,
			  struct wl_resource *resource);
static void
xdg_surface_unset_maximized(struct wl_client *client,
			    struct wl_resource *resource);

static void
xdg_surface_set_fullscreen(struct wl_client *client,
			   struct wl_resource *resource,
			   struct wl_resource *output_resource);
static void
xdg_surface_unset_fullscreen(struct wl_client *client,
			     struct wl_resource *resource);

static void
xdg_surface_set_minimized(struct wl_client *client,
			    struct wl_resource *resource);


static const struct desktop_shell_interface desktop_shell_implementation = {
	desktop_shell_set_background,
	desktop_shell_set_panel,
	desktop_shell_set_lock_surface,
	desktop_shell_unlock,
	desktop_shell_set_grab_surface,
	desktop_shell_desktop_ready,
	desktop_shell_set_panel_position
};

static const struct screensaver_interface screensaver_implementation = {
	screensaver_set_surface
};

static const struct wl_shell_interface shell_implementation = {
	shell_get_shell_surface
};

static const struct wl_shell_surface_interface shell_surface_implementation = {
	shell_surface_pong,
	shell_surface_move,
	shell_surface_resize,
	shell_surface_set_toplevel,
	shell_surface_set_transient,
	shell_surface_set_fullscreen,
	shell_surface_set_popup,
	shell_surface_set_maximized,
	shell_surface_set_title,
	shell_surface_set_class_
};

static const struct workspace_manager_interface workspace_manager_implementation = {
	workspace_manager_move_surface
};

static const struct xdg_shell_interface xdg_implementation = {
	xdg_use_unstable_version,
	xdg_get_xdg_surface,
	xdg_get_xdg_popup,
	xdg_pong
};

static const struct xdg_popup_interface xdg_popup_implementation = {
	xdg_popup_destroy,
};

static const struct xdg_surface_interface xdg_surface_implementation = {
	xdg_surface_destroy,
	xdg_surface_set_parent,
	xdg_surface_set_title,
	xdg_surface_set_app_id,
	xdg_surface_show_window_menu,
	xdg_surface_move,
	xdg_surface_resize,
	xdg_surface_ack_configure,
	xdg_surface_set_window_geometry,
	xdg_surface_set_maximized,
	xdg_surface_unset_maximized,
	xdg_surface_set_fullscreen,
	xdg_surface_unset_fullscreen,
	xdg_surface_set_minimized,
};



#endif /* PAGE_SHELL_PROTOCOLS_IMPLEMENTATION_HXX_ */
