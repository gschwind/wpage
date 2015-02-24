/*
 * protocols_implementation.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "protocols_implementation.hxx"


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

static void screensaver_set_surface(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource, struct wl_resource *output_resource);

const struct screensaver_interface screensaver_implementation = {
	screensaver_set_surface
};

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


static void workspace_manager_move_surface(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource, uint32_t workspace);

const struct workspace_manager_interface workspace_manager_implementation = {
	workspace_manager_move_surface
};


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

static void xdg_popup_destroy(struct wl_client *client, struct wl_resource *resource);

static const struct xdg_popup_interface xdg_popup_implementation = {
	xdg_popup_destroy,
};


static void xdg_surface_destroy(struct wl_client *client, struct wl_resource *resource);
static void xdg_surface_set_parent(struct wl_client *client, struct wl_resource *resource, struct wl_resource *parent_resource);
static void xdg_surface_set_title(struct wl_client *client, struct wl_resource *resource, const char *title);
static void xdg_surface_set_app_id(struct wl_client *client, struct wl_resource *resource, const char *app_id);
static void xdg_surface_show_window_menu(struct wl_client *client, struct wl_resource *surface_resource, struct wl_resource *seat_resource, uint32_t serial, int32_t x, int32_t y);
static void xdg_surface_move(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial);
static void xdg_surface_resize(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial, uint32_t edges);
static void xdg_surface_ack_configure(struct wl_client *client, struct wl_resource *resource, uint32_t serial);
static void xdg_surface_set_window_geometry(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height);
static void xdg_surface_set_maximized(struct wl_client *client, struct wl_resource *resource);
static void xdg_surface_unset_maximized(struct wl_client *client, struct wl_resource *resource);
static void xdg_surface_set_fullscreen(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output_resource);
static void xdg_surface_unset_fullscreen(struct wl_client *client, struct wl_resource *resource);
static void xdg_surface_set_minimized(struct wl_client *client, struct wl_resource *resource);

const struct wl_shell_surface_interface shell_surface_implementation = {
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


const struct desktop_shell_interface desktop_shell_implementation = {
	desktop_shell_set_background,
	desktop_shell_set_panel,
	desktop_shell_set_lock_surface,
	desktop_shell_unlock,
	desktop_shell_set_grab_surface,
	desktop_shell_desktop_ready,
	desktop_shell_set_panel_position
};


const struct workspace_manager_interface workspace_manager_implementation = {
	workspace_manager_move_surface
};

const struct xdg_shell_interface xdg_implementation = {
	xdg_use_unstable_version,
	xdg_get_xdg_surface,
	xdg_get_xdg_popup,
	xdg_pong
};

const struct xdg_popup_interface xdg_popup_implementation = {
	xdg_popup_destroy,
};

const struct xdg_surface_interface xdg_surface_implementation = {
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
