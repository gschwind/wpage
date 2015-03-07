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
#include "utils.hxx"

#define DEFAULT_NUM_WORKSPACES 1
#define DEFAULT_WORKSPACE_CHANGE_ANIMATION_LENGTH 200

typedef void (*shell_for_each_layer_func_t)(struct desktop_shell *,
					    struct weston_layer *, void *);

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

using namespace page;

struct desktop_shell {
	struct weston_compositor *compositor;

	cxx_wl_listener<desktop_shell> idle_listener;
	cxx_wl_listener<desktop_shell> wake_listener;
	cxx_wl_listener<desktop_shell> destroy_listener;
	wl_listener show_input_panel_listener;
	wl_listener hide_input_panel_listener;
	wl_listener update_input_panel_listener;

	struct weston_layer fullscreen_layer;
	struct weston_layer panel_layer;
	struct weston_layer background_layer;
	struct weston_layer background_real_layer;
	struct weston_layer lock_layer;
	struct weston_layer input_panel_layer;

	cxx_wl_listener<desktop_shell> pointer_focus_listener;
	struct weston_surface *grab_surface;

	struct {
		struct wl_client *client;
		struct wl_resource *desktop_shell;
		cxx_wl_listener<::desktop_shell> client_destroy_listener;

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
		/* array of workspaces */
		std::vector<workspace*> array;
		/* current workspace id */
		unsigned int current;
		/* size of array */
		unsigned int num;

		/* list of resources that handle server-to-client workspace protocol */
		std::list<wl_resource*> client_list;

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

	cxx_wl_listener<desktop_shell, struct weston_seat> seat_create_listener;
	cxx_wl_listener<desktop_shell, struct weston_output> output_create_listener;
	cxx_wl_listener<desktop_shell, void> output_move_listener;
	struct wl_list output_list;

	enum desktop_shell_panel_position panel_position;

	char *client;

	struct timespec startup_time;

	struct weston_buffer_reference background_tex;

	desktop_shell(struct weston_compositor *ec, int *argc, char *argv[]);
	~desktop_shell();

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

	void shell_fade_startup();
	static void do_shell_fade_startup(void *data);

	void shell_configuration();
	void shell_destroy();
	void idle_handler();
	void wake_handler();

	static void bind_shell(wl_client *client, desktop_shell * shell, uint32_t version, uint32_t id);
	static void bind_xdg_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id);
	static void bind_desktop_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id);
	static void bind_screensaver(struct wl_client *client, void *data, uint32_t version, uint32_t id);
	static void bind_workspace_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id);

	static void launch_desktop_shell_process(void *data);
	static int screensaver_timeout(void *data);

	void handle_seat_created(struct weston_seat * data);

	void shell_fade_init();
	static int fade_startup_timeout(desktop_shell *);
	void shell_add_bindings(struct weston_compositor *ec);

	static void unbind_desktop_shell(struct wl_resource *resource);
	static void unbind_screensaver(struct wl_resource *resource);
	static void unbind_resource(struct wl_resource *resource);

	static void zoom_axis_binding(struct weston_seat *seat, uint32_t time, uint32_t axis, wl_fixed_t value, void *data);
	static void zoom_key_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data);
	static void terminate_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data);
	static void rotate_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data);

	static void activate_binding(struct weston_seat *seat, struct desktop_shell *shell, struct weston_surface *focus);
	static void click_to_activate_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data);
	static void touch_to_activate_binding(struct weston_seat *seat, uint32_t time, void *data);


	static void move_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data);
	static void maximize_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data);
	static void fullscreen_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data);
	static void touch_move_binding(struct weston_seat *seat, uint32_t time, void *data);
	static void resize_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data);

//	static void debug_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data);
//	static void force_kill_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data);
//	static void workspace_up_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data);
//	static void workspace_down_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data);
//	static void workspace_f_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data);
//	static void workspace_move_surface_up_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data);
//	static void workspace_move_surface_down_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data);

	void desktop_shell_client_destroy();

	bool check_desktop_shell_crash_too_early();
	void respawn_desktop_shell_process();

	static void surface_opacity_binding(struct weston_seat *seat, uint32_t time, uint32_t axis,
				wl_fixed_t value, void *data);

	void handle_output_create(struct weston_output *data);

	void setup_output_destroy_handler(struct weston_compositor *ec);
	void create_shell_output(struct weston_output *output);

	void shell_for_each_layer(shell_for_each_layer_func_t func, void *data);
	void handle_output_move(void *data);

	void map(shell_surface *shsurf, int32_t sx, int32_t sy);
	void configure(struct weston_surface *surface, float x, float y);
	void set_maximized_position(shell_surface *shsurf);
	void broadcast_current_workspace_state();
	void reverse_workspace_change_animation(unsigned int index, workspace *from, workspace *to);

};


#endif /* PAGE_SHELL_DESKTOP_SHELL_HXX_ */
