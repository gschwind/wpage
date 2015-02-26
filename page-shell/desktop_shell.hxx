/*
 * desktop_shell.hxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */
#ifndef PAGE_SHELL_DESKTOP_SHELL_HXX_
#define PAGE_SHELL_DESKTOP_SHELL_HXX_

#include "compositor.h"
#include "desktop-shell-server-protocol.h"

#include "exposay.hxx"
#include "focus_state.hxx"

enum animation_type {
	ANIMATION_NONE,

	ANIMATION_ZOOM,
	ANIMATION_FADE,
	ANIMATION_DIM_LAYER,
};

enum fade_type {
	FADE_IN,
	FADE_OUT
};


struct desktop_shell {
	struct weston_compositor *compositor;

	struct wl_listener idle_listener;
	struct wl_listener wake_listener;
	struct wl_listener destroy_listener;
	struct wl_listener show_input_panel_listener;
	struct wl_listener hide_input_panel_listener;
	struct wl_listener update_input_panel_listener;

	struct weston_layer fullscreen_layer;
	struct weston_layer panel_layer;
	struct weston_layer background_layer;
	struct weston_layer lock_layer;
	struct weston_layer input_panel_layer;

	struct wl_listener pointer_focus_listener;
	struct weston_surface *grab_surface;

	struct {
		struct wl_client *client;
		struct wl_resource *desktop_shell;
		struct wl_listener client_destroy_listener;

		unsigned deathcount;
		uint32_t deathstamp;
	} child;

	bool locked;
	bool showing_input_panels;
	bool prepare_event_sent;

	struct {
		struct weston_surface *surface;
		pixman_box32_t cursor_rectangle;
	} text_input;

	struct weston_surface *lock_surface;
	struct wl_listener lock_surface_listener;

	struct {
		struct wl_array array;
		unsigned int current;
		unsigned int num;

		struct wl_list client_list;

		struct weston_animation animation;
		struct wl_list anim_sticky_list;
		int anim_dir;
		uint32_t anim_timestamp;
		double anim_current;
		struct workspace *anim_from;
		struct workspace *anim_to;
	} workspaces;

	struct {
		char *path;
		int duration;
		struct wl_resource *binding;
		struct weston_process process;
		struct wl_event_source *timer;
	} screensaver;

	struct {
		struct wl_resource *binding;
		struct wl_list surfaces;
	} input_panel;

	struct {
		struct weston_view *view;
		struct weston_view_animation *animation;
		enum fade_type type;
		struct wl_event_source *startup_timer;
	} fade;

	struct exposay exposay;

	uint32_t binding_modifier;
	uint32_t exposay_modifier;
	enum animation_type win_animation_type;
	enum animation_type win_close_animation_type;
	enum animation_type startup_animation_type;
	enum animation_type focus_animation_type;

	struct weston_layer minimized_layer;

	struct wl_listener seat_create_listener;
	struct wl_listener output_create_listener;
	struct wl_listener output_move_listener;
	struct wl_list output_list;

	enum desktop_shell_panel_position panel_position;

	char *client;

	struct timespec startup_time;

	void get_output_work_area(struct weston_output *output, pixman_rectangle32_t *area);
	void get_output_panel_size(struct weston_output *output, int *width, int *height);
	void drop_focus_state(struct workspace *ws, struct weston_surface *surface);
	void move_surface_to_workspace(shell_surface *shsurf, uint32_t workspace);
	workspace * get_workspace(unsigned int index);

	void shell_fade(fade_type type);
	struct weston_view * shell_fade_create_surface();

	static void shell_fade_done(struct weston_view_animation *animation, void *data);
	static void handle_screensaver_sigchld(struct weston_process *proc, int status);

	void unlock();
	void lock();

	void launch_screensaver();
	void unfocus_all_seats();
	void resume_desktop();
	void terminate_screensaver();

	void restore_focus_state(struct workspace *ws);

};


#endif /* PAGE_SHELL_DESKTOP_SHELL_HXX_ */
