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
#include "protocols_implementation.hxx"

#include "client.hxx"
#include "shell_seat.hxx"
#include "utils.hxx"

namespace page {
struct shell_client;
}

using namespace page;

enum shell_surface_type {
	SHELL_SURFACE_NONE,
	SHELL_SURFACE_TOPLEVEL,
	SHELL_SURFACE_POPUP,
	SHELL_SURFACE_XWAYLAND
};

/**
 * This shell surface is used for both shell_surface and xdg_surface
 * the diference is in resources interfaces.
 **/
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
	int32_t saved_width, saved_height;
	bool saved_position_valid;
	bool saved_size_valid;
	bool saved_rotation_valid;
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

	struct _fullscree_t {
		wl_shell_surface_fullscreen_method type;
		weston_transform transform; /* matrix from x, y */
		uint32_t framerate;
		weston_view *black_view;

		_fullscree_t() :
			type{WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT},
			framerate{0},
			black_view{nullptr}
		{
			wl_list_init(&transform.link);
			weston_matrix_init(&transform.matrix);
		}

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

	auto surface_move(weston_seat *seat, int client_initiated) -> int;
	auto surface_rotate(struct weston_seat *seat) -> void;
	auto surface_touch_move(weston_seat *seat) -> int;
	auto surface_resize(struct weston_seat *seat, uint32_t edges) -> int;

	static auto get_shell_surface(struct weston_surface *surface) -> shell_surface *;
	static auto shell_surface_configure(struct weston_surface *es, int32_t sx, int32_t sy) -> void;
	static void shell_destroy_shell_surface(wl_resource *resource);

	void handle_resource_destroy();
	void shell_handle_surface_destroy();

	void send_configure_for_surface();
	void shell_surface_state_changed();


	/**
	 * Implementation of server side shell_surface API
	 **/

	static auto shell_surface_pong(wl_client *client, wl_resource *resource, uint32_t serial) -> void;
	static auto shell_surface_move(wl_client *client, wl_resource *resource, wl_resource *seat_resource, uint32_t serial) -> void;
	static auto shell_surface_resize(wl_client *client, wl_resource *resource, wl_resource *seat_resource, uint32_t serial, uint32_t edges) -> void;
	static auto shell_surface_set_toplevel(wl_client *client, wl_resource *resource) -> void;
	static auto	shell_surface_set_transient(wl_client *client, wl_resource *resource, wl_resource *parent_resource, int x, int y, uint32_t flags) -> void;
	static auto shell_surface_set_fullscreen(wl_client *client, wl_resource *resource, uint32_t method, uint32_t framerate, wl_resource *output_resource) -> void;
	static auto shell_surface_set_popup(wl_client *client, wl_resource *resource, wl_resource *seat_resource, uint32_t serial, struct wl_resource *parent_resource, int32_t x, int32_t y, uint32_t flags) -> void;
	static auto shell_surface_set_maximized(wl_client *client, wl_resource *resource, wl_resource *output_resource) -> void;
	static auto shell_surface_set_title(wl_client *client, wl_resource *resource, const char *title) -> void;
	static auto shell_surface_set_class(wl_client *client, wl_resource *resource, const char *class_name) -> void;

	/* commun implementation */
	static void common_surface_move(wl_resource *resource, wl_resource *seat_resource, uint32_t serial);
	static void common_surface_resize(struct wl_resource *resource,
			      struct wl_resource *seat_resource, uint32_t serial,
			      uint32_t edges);

	void surface_clear_next_states();
	void shell_surface_set_output(struct weston_output *output);
	void set_popup(struct weston_surface *parent, struct weston_seat *seat, uint32_t serial, int32_t x, int32_t y);
	void set_fullscreen(uint32_t method, uint32_t framerate, struct weston_output *output);


};


#endif /* PAGE_SHELL_SURFACE_HXX_ */
