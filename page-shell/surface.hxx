/*
 * surface.hxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#ifndef PAGE_SHELL_SURFACE_HXX_
#define PAGE_SHELL_SURFACE_HXX_

#include "compositor.h"
#include "client.hxx"
#include "shell_seat.hxx"
#include "utils.hxx"

namespace page {

enum shell_surface_type {
	SHELL_SURFACE_NONE,
	SHELL_SURFACE_TOPLEVEL,
	SHELL_SURFACE_POPUP,
	SHELL_SURFACE_XWAYLAND
};

struct shell_surface {
	wl_resource *resource;
	wl_signal destroy_signal;
	shell_client *owner;

	weston_surface *surface;
	weston_view *view;
	int32_t last_width, last_height;
	cxx_wl_listener<shell_surface> surface_destroy_listener;
	cxx_wl_listener<shell_surface> resource_destroy_listener;

	weston_surface *parent;
	wl_list children_list; /* child surfaces of this one */
	wl_list children_link; /* sibling surfaces of this one */
	desktop_shell *shell;

	enum shell_surface_type type;
	char *title, *class_;
	int32_t saved_x, saved_y;
	int32_t saved_width, saved_height;bool saved_position_valid;bool saved_size_valid;bool saved_rotation_valid;
	int unresponsive, grabbed;
	uint32_t resize_edges;

	struct {
		weston_transform transform;
		weston_matrix rotation;
	} rotation;

	struct {
		wl_list grab_link;
		int32_t x, y;
		shell_seat *shseat;
		uint32_t serial;
	} popup;

	struct {
		int32_t x, y;
		uint32_t flags;
	} transient;

	struct {
		enum wl_shell_surface_fullscreen_method type;
		struct weston_transform transform; /* matrix from x, y */
		uint32_t framerate;
		struct weston_view *black_view;
	} fullscreen;

	weston_transform workspace_transform;

	weston_output *fullscreen_output;
	weston_output *output;
	wl_list link;

	const weston_shell_client *client;

	struct surface_state {
		bool maximized;bool fullscreen;bool relative;bool lowered;
	} state, next_state, requested_state; /* surface states */
	bool state_changed;bool state_requested;

	struct {
		int32_t x, y, width, height;
	} geometry, next_geometry;bool has_set_geometry, has_next_geometry;

	int focus_count;

	shell_surface(shell_client * owner, void * shell, weston_surface * surface, weston_shell_client const *client);
	~shell_surface();

	auto remove_popup_grab() -> void;
	auto set_surface_type() -> void;
	auto reset_surface_type() -> int;
	auto unset_fullscreen() -> void;
	auto unset_maximized() -> void;
	auto shell_surface_is_top_fullscreen() -> bool;
	auto set_full_output() -> void;
	auto shell_surface_update_layer() -> void;
	auto shell_surface_get_shell() -> desktop_shell *;
	auto shell_surface_set_parent(weston_surface * parent) -> void;
	auto shell_surface_calculate_layer_link() -> weston_layer_entry *;
	auto shell_surface_update_child_surface_layers() -> void;
	auto shell_surface_is_wl_shell_surface() -> bool;
	auto shell_surface_is_xdg_surface() -> bool;



	static auto get_shell_surface(struct weston_surface *surface) -> shell_surface *;
	static auto shell_surface_configure(struct weston_surface *es, int32_t sx, int32_t sy) -> void;
	static auto handle_resource_destroy(cxx_wl_listener<shell_surface> * listener, void *data) -> void;
	static auto shell_handle_surface_destroy(cxx_wl_listener<shell_surface> * listener, void *data) -> void;

};

}

#endif /* PAGE_SHELL_SURFACE_HXX_ */
