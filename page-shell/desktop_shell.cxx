/*
 * desktop_shell.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "desktop_shell.hxx"

#include <cassert>
#include <signal.h>
#include <cstring>

#include "config.h"
#include "compositor.h"
#include "shell.h"
#include "exposay.hxx"
#include "grab_handlers.hxx"
#include "focus_state.hxx"
#include "surface.hxx"
#include "exception.hxx"

void desktop_shell::get_output_work_area(struct weston_output *output,
		pixman_rectangle32_t *area) {
	int32_t panel_width = 0, panel_height = 0;

	area->x = 0;
	area->y = 0;

	this->get_output_panel_size(output, &panel_width, &panel_height);

	switch (this->panel_position) {
	case DESKTOP_SHELL_PANEL_POSITION_TOP:
	default:
		area->y = panel_height;
	case DESKTOP_SHELL_PANEL_POSITION_BOTTOM:
		area->width = output->width;
		area->height = output->height - panel_height;
		break;
	case DESKTOP_SHELL_PANEL_POSITION_LEFT:
		area->x = panel_width;
	case DESKTOP_SHELL_PANEL_POSITION_RIGHT:
		area->width = output->width - panel_width;
		area->height = output->height;
		break;
	}
}

void desktop_shell::get_output_panel_size(struct weston_output *output,
		int *width, int *height) {
	struct weston_view *view;

	*width = 0;
	*height = 0;

	if (!output)
		return;

	wl_list_for_each(view, &this->panel_layer.view_list.link, layer_link.link)
	{
		float x, y;

		if (view->surface->output != output)
			continue;

		switch (this->panel_position) {
		case DESKTOP_SHELL_PANEL_POSITION_TOP:
		case DESKTOP_SHELL_PANEL_POSITION_BOTTOM:

			weston_view_to_global_float(view, view->surface->width, 0, &x, &y);

			*width = (int) x;
			*height = view->surface->height + (int) y;
			return;

		case DESKTOP_SHELL_PANEL_POSITION_LEFT:
		case DESKTOP_SHELL_PANEL_POSITION_RIGHT:
			weston_view_to_global_float(view, 0, view->surface->height, &x, &y);

			*width = view->surface->width + (int) x;
			*height = (int) y;
			return;

		default:
			/* we've already set width and height to
			 * fallback values. */
			break;
		}
	}

	/* the correct view wasn't found */
}

void desktop_shell::drop_focus_state(struct workspace *ws,
		struct weston_surface *surface) {
	struct focus_state *state;

	wl_list_for_each(state, &ws->focus_list, link)
		if (state->keyboard_focus == surface)
			state->focus_state_set_focus(NULL);
}

void desktop_shell::move_surface_to_workspace(shell_surface *shsurf,
		uint32_t workspace) {
	struct workspace *from;
	struct workspace *to;
	weston_seat *seat;
	weston_surface *focus;
	weston_view *view;

	if (workspace == this->workspaces.current)
		return;

	view = get_default_view(shsurf->surface);
	if (!view)
		return;

	assert(weston_surface_get_main_surface(view->surface) == view->surface);

	if (workspace >= this->workspaces.num)
		workspace = this->workspaces.num - 1;

	from = get_current_workspace(this);
	to = this->get_workspace(workspace);

	weston_layer_entry_remove(&view->layer_link);
	weston_layer_entry_insert(&to->layer.view_list, &view->layer_link);

	shsurf->shell_surface_update_child_surface_layers();

	this->drop_focus_state(from, view->surface);
	wl_list_for_each(seat, &this->compositor->seat_list, link)
	{
		if (!seat->keyboard)
			continue;

		focus = weston_surface_get_main_surface(seat->keyboard->focus);
		if (focus == view->surface)
			weston_keyboard_set_focus(seat->keyboard, NULL);
	}

	weston_view_damage_below(view);
}

workspace * desktop_shell::get_workspace(unsigned int index) {
	workspace **pws =
			reinterpret_cast<workspace **>(this->workspaces.array.data);
	assert(index < this->workspaces.num);
	pws += index;
	return *pws;
}

void desktop_shell::shell_fade(enum fade_type type)
{
	float tint;

	switch (type) {
	case FADE_IN:
		tint = 0.0;
		break;
	case FADE_OUT:
		tint = 1.0;
		break;
	default:
		weston_log("this: invalid fade type\n");
		return;
	}

	this->fade.type = type;

	if (this->fade.view == NULL) {
		this->fade.view = shell_fade_create_surface();
		if (!this->fade.view)
			return;

		this->fade.view->alpha = 1.0 - tint;
		weston_view_update_transform(this->fade.view);
	}

	if (this->fade.view->output == NULL) {
		/* If the black view gets a NULL output, we lost the
		 * last output and we'll just cancel the fade.  This
		 * happens when you close the last window under the
		 * X11 or Wayland backends. */
		this->locked = false;
		weston_surface_destroy(this->fade.view->surface);
		this->fade.view = NULL;
	} else if (this->fade.animation) {
		weston_fade_update(this->fade.animation, tint);
	} else {
		this->fade.animation =
			weston_fade_run(this->fade.view,
					1.0 - tint, tint, 300.0,
					shell_fade_done, this);
	}
}

void desktop_shell::shell_fade_done(struct weston_view_animation *animation, void *data)
{
	auto shell = reinterpret_cast<desktop_shell*>(data);

	shell->fade.animation = NULL;

	switch (shell->fade.type) {
	case FADE_IN:
		weston_surface_destroy(shell->fade.view->surface);
		shell->fade.view = NULL;
		break;
	case FADE_OUT:
		shell->lock();
		break;
	default:
		break;
	}
}


struct weston_view *
desktop_shell::shell_fade_create_surface()
{
	struct weston_compositor *compositor = this->compositor;
	struct weston_surface *surface;
	struct weston_view *view;

	surface = weston_surface_create(compositor);
	if (!surface)
		return NULL;

	view = weston_view_create(surface);
	if (!view) {
		weston_surface_destroy(surface);
		return NULL;
	}

	weston_surface_set_size(surface, 8192, 8192);
	weston_view_set_position(view, 0, 0);
	weston_surface_set_color(surface, 0.0, 0.0, 0.0, 1.0);
	weston_layer_entry_insert(&compositor->fade_layer.view_list,
				  &view->layer_link);
	pixman_region32_init(&surface->input);

	return view;
}

void desktop_shell::lock()
{
	struct workspace *ws = get_current_workspace(this);

	if (this->locked) {
		weston_compositor_sleep(this->compositor);
		return;
	}

	this->locked = true;

	/* Hide all surfaces by removing the fullscreen, panel and
	 * toplevel layers.  This way nothing else can show or receive
	 * input events while we are locked. */

	wl_list_remove(&this->panel_layer.link);
	wl_list_remove(&this->fullscreen_layer.link);
	if (this->showing_input_panels)
		wl_list_remove(&this->input_panel_layer.link);
	wl_list_remove(&ws->layer.link);
	wl_list_insert(&this->compositor->cursor_layer.link,
		       &this->lock_layer.link);

	launch_screensaver();

	/* Remove the keyboard focus on all seats. This will be
	 * restored to the workspace's saved state via
	 * restore_focus_state when the compositor is unlocked */
	unfocus_all_seats();

	/* TODO: disable bindings that should not work while locked. */

	/* All this must be undone in resume_desktop(). */
}

void desktop_shell::unlock()
{
	if (!this->locked || this->lock_surface) {
		shell_fade(FADE_IN);
		return;
	}

	/* If desktop-shell client has gone away, unlock immediately. */
	if (!this->child.desktop_shell) {
		resume_desktop();
		return;
	}

	if (this->prepare_event_sent)
		return;

	desktop_shell_send_prepare_lock_surface(this->child.desktop_shell);
	this->prepare_event_sent = true;
}

void
desktop_shell::launch_screensaver()
{
	if (this->screensaver.binding)
		return;

	if (!this->screensaver.path) {
		weston_compositor_sleep(this->compositor);
		return;
	}

	if (this->screensaver.process.pid != 0) {
		weston_log("old screensaver still running\n");
		return;
	}

	weston_client_launch(this->compositor,
			   &this->screensaver.process,
			   this->screensaver.path,
			   handle_screensaver_sigchld);
}


void desktop_shell::unfocus_all_seats()
{
	struct weston_seat *seat, *next;

	wl_list_for_each_safe(seat, next, &this->compositor->seat_list, link) {
		if (seat->keyboard == NULL)
			continue;

		weston_keyboard_set_focus(seat->keyboard, NULL);
	}
}

void desktop_shell::resume_desktop()
{
	struct workspace *ws = get_current_workspace(this);

	terminate_screensaver();

	wl_list_remove(&this->lock_layer.link);
	if (this->showing_input_panels) {
		wl_list_insert(&this->compositor->cursor_layer.link,
			       &this->input_panel_layer.link);
		wl_list_insert(&this->input_panel_layer.link,
			       &this->fullscreen_layer.link);
	} else {
		wl_list_insert(&this->compositor->cursor_layer.link,
			       &this->fullscreen_layer.link);
	}
	wl_list_insert(&this->fullscreen_layer.link,
		       &this->panel_layer.link);
	wl_list_insert(&this->panel_layer.link,
		       &ws->layer.link),

	restore_focus_state(get_current_workspace(this));

	this->locked = false;
	shell_fade(FADE_IN);
	weston_compositor_damage_all(this->compositor);
}

void desktop_shell::handle_screensaver_sigchld(struct weston_process *proc, int status)
{
	struct desktop_shell *shell =
		container_of(proc, struct desktop_shell, screensaver.process);

	proc->pid = 0;

	if (shell->locked)
		weston_compositor_sleep(shell->compositor);
}


void desktop_shell::terminate_screensaver()
{
	if (this->screensaver.process.pid == 0)
		return;

	/* Disarm the screensaver timer, otherwise it may fire when the
	 * compositor is not in the idle state. In that case, the screen will
	 * be locked, but the wake_signal won't fire on user input, making the
	 * system unresponsive. */
	wl_event_source_timer_update(this->screensaver.timer, 0);

	kill(this->screensaver.process.pid, SIGTERM);
}

void desktop_shell::restore_focus_state(struct workspace *ws)
{
	struct focus_state *state, *next;
	struct weston_surface *surface;
	struct wl_list pending_seat_list;
	struct weston_seat *seat, *next_seat;

	/* Temporarily steal the list of seats so that we can keep
	 * track of the seats we've already processed */
	wl_list_init(&pending_seat_list);
	wl_list_insert_list(&pending_seat_list, &this->compositor->seat_list);
	wl_list_init(&this->compositor->seat_list);

	wl_list_for_each_safe(state, next, &ws->focus_list, link) {
		wl_list_remove(&state->seat->link);
		wl_list_insert(&this->compositor->seat_list,
			       &state->seat->link);

		if (state->seat->keyboard == NULL)
			continue;

		surface = state->keyboard_focus;

		weston_keyboard_set_focus(state->seat->keyboard, surface);
	}

	/* For any remaining seats that we don't have a focus state
	 * for we'll reset the keyboard focus to NULL */
	wl_list_for_each_safe(seat, next_seat, &pending_seat_list, link) {
		wl_list_insert(&this->compositor->seat_list, &seat->link);

		if (seat->keyboard == NULL)
			continue;

		weston_keyboard_set_focus(seat->keyboard, NULL);
	}
}

void desktop_shell::shell_fade_startup()
{
	struct wl_event_loop *loop;

	if (!this->fade.startup_timer)
		return;

	wl_event_source_remove(this->fade.startup_timer);
	this->fade.startup_timer = NULL;

	loop = wl_display_get_event_loop(this->compositor->wl_display);
	wl_event_loop_add_idle(loop, do_shell_fade_startup, this);
}

static void
desktop_shell::do_shell_fade_startup(void *data)
{
	struct desktop_shell *shell = reinterpret_cast<desktop_shell*>(data);

	if (shell->startup_animation_type == ANIMATION_FADE)
		shell->shell_fade(FADE_IN);
	else if (shell->startup_animation_type == ANIMATION_NONE) {
		weston_surface_destroy(shell->fade.view->surface);
		shell->fade.view = NULL;
	}
}


static enum weston_keyboard_modifier
get_modifier(char *modifier)
{
	if (!modifier)
		return MODIFIER_SUPER;

	if (!strcmp("ctrl", modifier))
		return MODIFIER_CTRL;
	else if (!strcmp("alt", modifier))
		return MODIFIER_ALT;
	else if (!strcmp("super", modifier))
		return MODIFIER_SUPER;
	else
		return MODIFIER_SUPER;
}

static enum animation_type
get_animation_type(char *animation)
{
	if (!animation)
		return ANIMATION_NONE;

	if (!strcmp("zoom", animation))
		return ANIMATION_ZOOM;
	else if (!strcmp("fade", animation))
		return ANIMATION_FADE;
	else if (!strcmp("dim-layer", animation))
		return ANIMATION_DIM_LAYER;
	else
		return ANIMATION_NONE;
}


void desktop_shell::shell_configuration()
{
	struct weston_config_section *section;
	int duration;
	char *s, *client;
	int ret;

	section = weston_config_get_section(this->compositor->config,
					    "screensaver", NULL, NULL);
	weston_config_section_get_string(section,
					 "path", &this->screensaver.path, NULL);
	weston_config_section_get_int(section, "duration", &duration, 60);
	this->screensaver.duration = duration * 1000;

	section = weston_config_get_section(this->compositor->config,
					    "shell", NULL, NULL);
	ret = asprintf(&client, "%s/%s", weston_config_get_libexec_dir(),
		       WESTON_SHELL_CLIENT);
	if (ret < 0)
		client = NULL;
	weston_config_section_get_string(section,
					 "client", &s, client);
	free(client);
	this->client = s;
	weston_config_section_get_string(section,
					 "binding-modifier", &s, "super");
	this->binding_modifier = get_modifier(s);
	free(s);

	weston_config_section_get_string(section,
					 "exposay-modifier", &s, "none");
	if (strcmp(s, "none") == 0)
		this->exposay_modifier = 0;
	else
		this->exposay_modifier = get_modifier(s);
	free(s);

	weston_config_section_get_string(section, "animation", &s, "none");
	this->win_animation_type = get_animation_type(s);
	free(s);
	weston_config_section_get_string(section, "close-animation", &s, "fade");
	this->win_close_animation_type = get_animation_type(s);
	free(s);
	weston_config_section_get_string(section,
					 "startup-animation", &s, "fade");
	this->startup_animation_type = get_animation_type(s);
	free(s);
	if (this->startup_animation_type == ANIMATION_ZOOM)
		this->startup_animation_type = ANIMATION_NONE;
	weston_config_section_get_string(section, "focus-animation", &s, "none");
	this->focus_animation_type = get_animation_type(s);
	free(s);
	weston_config_section_get_uint(section, "num-workspaces",
				       &this->workspaces.num,
				       DEFAULT_NUM_WORKSPACES);
}

void desktop_shell::shell_destroy()
{
	/* self destroy */
	delete this;
}

desktop_shell::~desktop_shell()
{
	struct workspace **ws;
	struct shell_output *shell_output, *tmp;

	/* Force state to unlocked so we don't try to fade */
	this->locked = false;

	if (this->child.client) {
		/* disable respawn */
		wl_list_remove(&this->child.client_destroy_listener.listener.link);
		wl_client_destroy(this->child.client);
	}

	wl_list_remove(&this->idle_listener.listener.link);
	wl_list_remove(&this->wake_listener.listener.link);

	input_panel_destroy(this);

	wl_list_for_each_safe(shell_output, tmp, &this->output_list, link) {
		wl_list_remove(&shell_output->destroy_listener.link);
		wl_list_remove(&shell_output->link);
		free(shell_output);
	}

	wl_list_remove(&this->output_create_listener.listener.link);
	wl_list_remove(&this->output_move_listener.listener.link);

	wl_array_for_each(ws, &this->workspaces.array)
		workspace_destroy(*ws);
	wl_array_release(&this->workspaces.array);

	free(this->screensaver.path);
	free(this->client);
}


desktop_shell::desktop_shell(struct weston_compositor *ec, int *argc, char *argv[]) :
destroy_listener{this, &desktop_shell::shell_destroy},
wake_listener{this, &desktop_shell::wake_handler},
idle_listener{this, &desktop_shell::idle_handler},
seat_create_listener{this, &desktop_shell::handle_seat_created},
compositor{nullptr},
show_input_panel_listener{0},
hide_input_panel_listener{0},
update_input_panel_listener{0},
fullscreen_layer{0},
panel_layer{0},
background_layer{0},
lock_layer{0},
input_panel_layer{0},
grab_surface{nullptr},
child{0},
locked{false},
showing_input_panels{false},
prepare_event_sent{false},
text_input{0},
lock_surface{nullptr},
lock_surface_listener{0},
workspaces{0},
screensaver{0},
input_panel{0},
fade{0},
exposay{0},
binding_modifier{0},
exposay_modifier{0},
win_animation_type{ANIMATION_NONE},
win_close_animation_type{ANIMATION_NONE},
startup_animation_type{ANIMATION_NONE},
focus_animation_type{ANIMATION_NONE},
minimized_layer{0},
output_list{0},
panel_position{DESKTOP_SHELL_PANEL_POSITION_TOP},
client{nullptr},
startup_time{0}
{
	struct weston_seat *seat;
	struct workspace **pws;
	unsigned int i;
	struct wl_event_loop *loop;

	this->compositor = ec;

	wl_signal_add(&ec->destroy_signal, &this->destroy_listener.listener);
	wl_signal_add(&ec->idle_signal, &this->idle_listener.listener);
	wl_signal_add(&ec->wake_signal, &this->wake_listener.listener);

	ec->shell_interface = page::weston_shell_interface_impl;
	ec->shell_interface.shell = this;

	weston_layer_init(&this->fullscreen_layer, &ec->cursor_layer.link);
	weston_layer_init(&this->panel_layer, &this->fullscreen_layer.link);
	weston_layer_init(&this->background_layer, &this->panel_layer.link);
	weston_layer_init(&this->lock_layer, NULL);
	weston_layer_init(&this->input_panel_layer, NULL);

	wl_array_init(&this->workspaces.array);
	wl_list_init(&this->workspaces.client_list);

	if (input_panel_setup(this) < 0)
		throw exception_t("cannot create panel");

	this->shell_configuration();

	this->exposay.state_cur = EXPOSAY_LAYOUT_INACTIVE;
	this->exposay.state_target = EXPOSAY_TARGET_CANCEL;

	for (i = 0; i < this->workspaces.num; i++) {
		pws = wl_array_add(&this->workspaces.array, sizeof *pws);
		if (pws == NULL)
			throw exception_t("cannot create workspace");

		*pws = workspace_create();
		if (*pws == NULL)
			throw exception_t("cannot create workspace");
	}
	activate_workspace(this, 0);

	weston_layer_init(&this->minimized_layer, NULL);

	wl_list_init(&this->workspaces.anim_sticky_list);
	wl_list_init(&this->workspaces.animation.link);
	this->workspaces.animation.frame = animate_workspace_change_frame;

	/**
	 * create a global interface of type shell
	 * wl_shell_interface is the description of shell_interface defined in libwayland.
	 **/
	if (wl_global_create(ec->wl_display, &wl_shell_interface, 1,
				  this, bind_shell) == NULL)
		throw exception_t("cannot create global shell interface");

	/**
	 * create a global interface of type xdg-shell
	 * xdg_shell_interface is the description of xdg-shell-insterface defined in _weston_ only.
	 **/
	if (wl_global_create(ec->wl_display, &xdg_shell_interface, 1,
				  this, bind_xdg_shell) == NULL)
		throw exception_t("cannot create global xdg shell interface");

	/**
	 * create a global interface of type desktop_shell_interface
	 * desktop_shell_interface is an extension provided by _weston_ only and is intend to be used by client
	 * to dynamically change setting of weston.
	 *
	 * xdg_shell_interface is the description of xdg-shell-interface defined in _weston_ only.
	 **/
	if (wl_global_create(ec->wl_display,
			     &desktop_shell_interface, 3,
			     this, bind_desktop_shell) == NULL)
		throw exception_t("cannot create global desktop shell interface");

	if (wl_global_create(ec->wl_display, &screensaver_interface, 1,
			     this, bind_screensaver) == NULL)
		throw exception_t("cannot create global screensaver interface");

	if (wl_global_create(ec->wl_display, &workspace_manager_interface, 1,
			     this, bind_workspace_manager) == NULL)
		throw exception_t("cannot create global workspace interface");

	this->child.deathstamp = weston_compositor_get_time();

	this->panel_position = DESKTOP_SHELL_PANEL_POSITION_TOP;

	this->setup_output_destroy_handler(ec);

	loop = wl_display_get_event_loop(ec->wl_display);
	wl_event_loop_add_idle(loop, launch_desktop_shell_process, this);

	this->screensaver.timer =
		wl_event_loop_add_timer(loop, screensaver_timeout, this);

	wl_list_for_each(seat, &ec->seat_list, link)
		handle_seat_created(seat);
	wl_signal_add(&ec->seat_created_signal, &this->seat_create_listener.listener);

	screenshooter_create(ec);

	shell_add_bindings(ec);

	shell_fade_init();

	clock_gettime(CLOCK_MONOTONIC, &this->startup_time);
}

void desktop_shell::idle_handler()
{
	struct weston_seat *seat;

	wl_list_for_each(seat, &this->compositor->seat_list, link) {
		if (seat->pointer)
			page::popup_grab_end(seat->pointer);
		if (seat->touch)
			page::touch_popup_grab_end(seat->touch);
	}

	this->shell_fade(FADE_OUT);
	/* lock() is called from shell_fade_done() */
}

void desktop_shell::wake_handler()
{
	this->unlock();
}

/**
 * This function is called when a client request to use shell-interface.
 *
 * Client do this by bind the corresponding wl_global
 **/
void desktop_shell::bind_shell(wl_client *client, desktop_shell * shell, uint32_t version, uint32_t id)
{
	/**
	 * reate a new object, this object will be handled by wl_resource
	 * And it will be destroyed on destroy of this resources
	 **/
	new page::shell_client{client, shell, page::shell_client::API_SHELL, id};
}

void desktop_shell::bind_xdg_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	auto shell = reinterpret_cast<struct desktop_shell *>(data);

	auto sc = new page::shell_client(client, shell, page::shell_client::API_XDG, id);
	if (sc)
		wl_resource_set_dispatcher(sc->resource,
					   xdg_shell_unversioned_dispatch,
					   NULL, sc, NULL);
}

void desktop_shell::bind_desktop_shell(struct wl_client *client,
		   void *data, uint32_t version, uint32_t id)
{
	auto shell = reinterpret_cast<struct desktop_shell *>(data);
	struct wl_resource *resource;

	resource = wl_resource_create(client, &desktop_shell_interface,
				      MIN(version, 3), id);

	if (client == shell->child.client) {
		wl_resource_set_implementation(resource,
					       &page::desktop_shell_implementation,
					       shell, unbind_desktop_shell);
		shell->child.desktop_shell = resource;

		if (version < 2)
			shell->shell_fade_startup();

		return;
	}

	wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
			       "permission to bind desktop_shell denied");
}

static void
desktop_shell::bind_screensaver(struct wl_client *client,
		 void *data, uint32_t version, uint32_t id)
{
	struct desktop_shell *shell = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &screensaver_interface, 1, id);

	if (shell->screensaver.binding == NULL) {
		wl_resource_set_implementation(resource,
					       &page::screensaver_implementation,
					       shell, unbind_screensaver);
		shell->screensaver.binding = resource;
		return;
	}

	wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
			       "interface object already bound");
}

static void
desktop_shell::bind_workspace_manager(struct wl_client *client,
		       void *data, uint32_t version, uint32_t id)
{
	desktop_shell *shell = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client,
				      &workspace_manager_interface, 1, id);

	if (resource == NULL) {
		weston_log("couldn't add workspace manager object");
		return;
	}

	wl_resource_set_implementation(resource,
				       &page::workspace_manager_implementation,
				       shell, unbind_resource);
	wl_list_insert(&shell->workspaces.client_list,
		       wl_resource_get_link(resource));

	workspace_manager_send_state(resource,
				     shell->workspaces.current,
				     shell->workspaces.num);
}

static void
desktop_shell::launch_desktop_shell_process(void *data)
{
	struct desktop_shell *shell = data;

	shell->child.client = weston_client_start(shell->compositor,
						  shell->client);

	if (!shell->child.client) {
		weston_log("not able to start %s\n", shell->client);
		return;
	}

	shell->child.client_destroy_listener = cxx_wl_listener<::desktop_shell>{shell,
		&desktop_shell::desktop_shell_client_destroy};
	wl_client_add_destroy_listener(shell->child.client,
				       &shell->child.client_destroy_listener.listener);
}

static int
desktop_shell::screensaver_timeout(void *data)
{
	struct desktop_shell *shell = data;
	shell->shell_fade(FADE_OUT);
	return 1;
}

void desktop_shell::handle_seat_created(struct weston_seat*data)
{
	new shell_seat{data};
}

void desktop_shell::shell_fade_init()
{
	/* Make compositor output all black, and wait for the desktop-shell
	 * client to signal it is ready, then fade in. The timer triggers a
	 * fade-in, in case the desktop-shell client takes too long.
	 */

	struct wl_event_loop *loop;

	if (this->fade.view != NULL) {
		weston_log("%s: warning: fade surface already exists\n",
			   __func__);
		return;
	}

	this->fade.view = this->shell_fade_create_surface();
	if (!this->fade.view)
		return;

	weston_view_update_transform(this->fade.view);
	weston_surface_damage(this->fade.view->surface);

	loop = wl_display_get_event_loop(this->compositor->wl_display);
	this->fade.startup_timer =
		wl_event_loop_add_timer(loop, reinterpret_cast<wl_event_loop_timer_func_t>(fade_startup_timeout), this);
	wl_event_source_timer_update(this->fade.startup_timer, 15000);
}

void desktop_shell::shell_add_bindings(struct weston_compositor *ec)
{
	uint32_t mod;
	int i, num_workspace_bindings;

	/* fixed bindings */
	weston_compositor_add_key_binding(ec, KEY_BACKSPACE,
				          MODIFIER_CTRL | MODIFIER_ALT,
				          terminate_binding, ec);
	weston_compositor_add_button_binding(ec, BTN_LEFT, 0,
					     click_to_activate_binding,
					     this);
	weston_compositor_add_button_binding(ec, BTN_RIGHT, 0,
					     click_to_activate_binding,
					     this);
	weston_compositor_add_touch_binding(ec, 0,
					    touch_to_activate_binding,
					    this);
	weston_compositor_add_axis_binding(ec, WL_POINTER_AXIS_VERTICAL_SCROLL,
				           MODIFIER_SUPER | MODIFIER_ALT,
				           surface_opacity_binding, NULL);
	weston_compositor_add_axis_binding(ec, WL_POINTER_AXIS_VERTICAL_SCROLL,
					   MODIFIER_SUPER, zoom_axis_binding,
					   NULL);

	/* configurable bindings */
	mod = this->binding_modifier;
	weston_compositor_add_key_binding(ec, KEY_PAGEUP, mod,
					  zoom_key_binding, NULL);
	weston_compositor_add_key_binding(ec, KEY_PAGEDOWN, mod,
					  zoom_key_binding, NULL);
	weston_compositor_add_key_binding(ec, KEY_M, mod | MODIFIER_SHIFT,
					  maximize_binding, NULL);
	weston_compositor_add_key_binding(ec, KEY_F, mod | MODIFIER_SHIFT,
					  fullscreen_binding, NULL);
	weston_compositor_add_button_binding(ec, BTN_LEFT, mod, move_binding,
					     this);
	weston_compositor_add_touch_binding(ec, mod, touch_move_binding, this);
	weston_compositor_add_button_binding(ec, BTN_MIDDLE, mod,
					     resize_binding, this);
	weston_compositor_add_button_binding(ec, BTN_LEFT,
					     mod | MODIFIER_SHIFT,
					     resize_binding, this);

	if (ec->capabilities & WESTON_CAP_ROTATION_ANY)
		weston_compositor_add_button_binding(ec, BTN_RIGHT, mod,
						     rotate_binding, NULL);

	//weston_compositor_add_key_binding(ec, KEY_TAB, mod, switcher_binding,
	//				  this);
	//weston_compositor_add_key_binding(ec, KEY_F9, mod, backlight_binding,
	//				  ec);
//	weston_compositor_add_key_binding(ec, KEY_BRIGHTNESSDOWN, 0,
//				          backlight_binding, ec);
//	weston_compositor_add_key_binding(ec, KEY_F10, mod, backlight_binding,
//					  ec);
//	weston_compositor_add_key_binding(ec, KEY_BRIGHTNESSUP, 0,
//				          backlight_binding, ec);
//	weston_compositor_add_key_binding(ec, KEY_K, mod,
//				          force_kill_binding, this);
//	weston_compositor_add_key_binding(ec, KEY_UP, mod,
//					  workspace_up_binding, this);
//	weston_compositor_add_key_binding(ec, KEY_DOWN, mod,
//					  workspace_down_binding, this);
//	weston_compositor_add_key_binding(ec, KEY_UP, mod | MODIFIER_SHIFT,
//					  workspace_move_surface_up_binding,
//					  this);
//	weston_compositor_add_key_binding(ec, KEY_DOWN, mod | MODIFIER_SHIFT,
//					  workspace_move_surface_down_binding,
//					  this);

	if (this->exposay_modifier)
		weston_compositor_add_modifier_binding(ec, static_cast<uint32_t>(this->exposay_modifier),
						       x_exposay_binding, this);

	/* Add bindings for mod+F[1-6] for workspace 1 to 6. */
	if (this->workspaces.num > 1) {
		num_workspace_bindings = this->workspaces.num;
		if (num_workspace_bindings > 6)
			num_workspace_bindings = 6;
//		for (i = 0; i < num_workspace_bindings; i++)
//			weston_compositor_add_key_binding(ec, KEY_F1 + i, mod,
//							  workspace_f_binding,
//							  this);
	}

	/* Debug bindings */
//	weston_compositor_add_key_binding(ec, KEY_SPACE, mod | MODIFIER_SHIFT,
//					  debug_binding, this);
}

void
desktop_shell::unbind_desktop_shell(struct wl_resource *resource)
{
	auto shell = reinterpret_cast<struct desktop_shell *>(wl_resource_get_user_data(resource));

	if (shell->locked)
		shell->resume_desktop();

	shell->child.desktop_shell = nullptr;
	shell->prepare_event_sent = false;
}


void desktop_shell::unbind_screensaver(struct wl_resource *resource)
{
	auto shell = reinterpret_cast<struct desktop_shell *>(wl_resource_get_user_data(resource));
	shell->screensaver.binding = nullptr;
}

void desktop_shell::unbind_resource(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

int desktop_shell::fade_startup_timeout(desktop_shell * shell)
{
	shell->shell_fade_startup();
	return 0;
}


static void
do_zoom(struct weston_seat *seat, uint32_t time, uint32_t key, uint32_t axis,
	wl_fixed_t value)
{
	struct weston_seat *ws = (struct weston_seat *) seat;
	struct weston_compositor *compositor = ws->compositor;
	struct weston_output *output;
	float increment;

	wl_list_for_each(output, &compositor->output_list, link) {
		if (pixman_region32_contains_point(&output->region,
						   wl_fixed_to_double(seat->pointer->x),
						   wl_fixed_to_double(seat->pointer->y),
						   NULL)) {
			if (key == KEY_PAGEUP)
				increment = output->zoom.increment;
			else if (key == KEY_PAGEDOWN)
				increment = -output->zoom.increment;
			else if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
				/* For every pixel zoom 20th of a step */
				increment = output->zoom.increment *
					    -wl_fixed_to_double(value) / 20.0;
			else
				increment = 0;

			output->zoom.level += increment;

			if (output->zoom.level < 0.0)
				output->zoom.level = 0.0;
			else if (output->zoom.level > output->zoom.max_level)
				output->zoom.level = output->zoom.max_level;
			else if (!output->zoom.active) {
				weston_output_activate_zoom(output);
			}

			output->zoom.spring_z.target = output->zoom.level;

			weston_output_update_zoom(output);
		}
	}
}



static void
desktop_shell::zoom_axis_binding(struct weston_seat *seat, uint32_t time, uint32_t axis,
		  wl_fixed_t value, void *data)
{
	do_zoom(seat, time, 0, axis, value);
}

static void
desktop_shell::zoom_key_binding(struct weston_seat *seat, uint32_t time, uint32_t key,
		 void *data)
{
	do_zoom(seat, time, key, 0, 0);
}

static void
desktop_shell::terminate_binding(struct weston_seat *seat, uint32_t time, uint32_t key,
		  void *data)
{
	struct weston_compositor *compositor = data;

	wl_display_terminate(compositor->wl_display);
}

static void
desktop_shell::rotate_binding(struct weston_seat *seat, uint32_t time, uint32_t button,
	       void *data)
{
	struct weston_surface *focus;
	struct weston_surface *base_surface;
	shell_surface *surface;

	if (seat->pointer->focus == NULL)
		return;

	focus = seat->pointer->focus->surface;

	base_surface = weston_surface_get_main_surface(focus);
	if (base_surface == NULL)
		return;

	surface = shell_surface::get_shell_surface(base_surface);
	if (surface == NULL || surface->state.fullscreen ||
	    surface->state.maximized)
		return;

	surface->surface_rotate(seat);
}

static shell_surface_type
get_shell_surface_type(struct weston_surface *surface)
{
	shell_surface *shsurf;

	shsurf = shell_surface::get_shell_surface(surface);
	if (!shsurf)
		return SHELL_SURFACE_NONE;
	return shsurf->type;
}

void
desktop_shell::activate_binding(struct weston_seat *seat,
		 struct desktop_shell *shell,
		 struct weston_surface *focus)
{
	struct weston_surface *main_surface;

	if (!focus)
		return;

	if (is_black_surface(focus, &main_surface))
		focus = main_surface;

	main_surface = weston_surface_get_main_surface(focus);
	if (get_shell_surface_type(main_surface) == SHELL_SURFACE_NONE)
		return;

	activate(shell, focus, seat, true);
}

void
desktop_shell::click_to_activate_binding(struct weston_seat *seat, uint32_t time, uint32_t button,
			  void *data)
{
	if (seat->pointer->grab != &seat->pointer->default_grab)
		return;
	if (seat->pointer->focus == NULL)
		return;

	activate_binding(seat, data, seat->pointer->focus->surface);
}

void
desktop_shell::touch_to_activate_binding(struct weston_seat *seat, uint32_t time, void *data)
{
	if (seat->touch->grab != &seat->touch->default_grab)
		return;
	if (seat->touch->focus == NULL)
		return;

	activate_binding(seat, data, seat->touch->focus->surface);
}

void
desktop_shell::desktop_shell_client_destroy()
{
	wl_list_remove(&this->child.client_destroy_listener.listener.link);
	this->child.client = nullptr;
	/*
	 * unbind_desktop_shell() will reset shell->child.desktop_shell
	 * before the respawned process has a chance to create a new
	 * desktop_shell object, because we are being called from the
	 * wl_client destructor which destroys all wl_resources before
	 * returning.
	 */

	if (!check_desktop_shell_crash_too_early())
		respawn_desktop_shell_process();

	this->shell_fade_startup();
}

bool
desktop_shell::check_desktop_shell_crash_too_early()
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return false;

	/*
	 * If the shell helper client dies before the session has been
	 * up for roughly 30 seconds, better just make Weston shut down,
	 * because the user likely has no way to interact with the desktop
	 * anyway.
	 */
	if (now.tv_sec - this->startup_time.tv_sec < 30) {
		weston_log("Error: %s apparently cannot run at all.\n",
			   this->client);
		weston_log_continue(STAMP_SPACE "Quitting...");
		wl_display_terminate(this->compositor->wl_display);

		return true;
	}

	return false;
}

void desktop_shell::respawn_desktop_shell_process()
{
	uint32_t time;

	/* if desktop-shell dies more than 5 times in 30 seconds, give up */
	time = weston_compositor_get_time();
	if (time - this->child.deathstamp > 30000) {
		this->child.deathstamp = time;
		this->child.deathcount = 0;
	}

	this->child.deathcount++;
	if (this->child.deathcount > 5) {
		weston_log("%s disconnected, giving up.\n", this->client);
		return;
	}

	weston_log("%s disconnected, respawning...\n", this->client);
	launch_desktop_shell_process(this);
}


void
desktop_shell::surface_opacity_binding(struct weston_seat *seat, uint32_t time, uint32_t axis,
			wl_fixed_t value, void *data)
{
	float step = 0.005;
	shell_surface *shsurf;
	struct weston_surface *focus = seat->pointer->focus->surface;
	struct weston_surface *surface;

	/* XXX: broken for windows containing sub-surfaces */
	surface = weston_surface_get_main_surface(focus);
	if (surface == NULL)
		return;

	shsurf = shell_surface::get_shell_surface(surface);
	if (!shsurf)
		return;

	shsurf->view->alpha -= wl_fixed_to_double(value) * step;

	if (shsurf->view->alpha > 1.0)
		shsurf->view->alpha = 1.0;
	if (shsurf->view->alpha < step)
		shsurf->view->alpha = step;

	weston_view_geometry_dirty(shsurf->view);
	weston_surface_damage(surface);
}



static void
desktop_shell::move_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data)
{
	struct weston_surface *focus;
	struct weston_surface *surface;
	shell_surface *shsurf;

	if (seat->pointer->focus == NULL)
		return;

	focus = seat->pointer->focus->surface;

	surface = weston_surface_get_main_surface(focus);
	if (surface == NULL)
		return;

	shsurf = shell_surface::get_shell_surface(surface);
	if (shsurf == NULL || shsurf->state.fullscreen ||
	    shsurf->state.maximized)
		return;

	shsurf->surface_move((struct weston_seat *) seat, 0);
}

static void
desktop_shell::maximize_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data)
{
	struct weston_surface *focus = seat->keyboard->focus;
	struct weston_surface *surface;
	shell_surface *shsurf;

	surface = weston_surface_get_main_surface(focus);
	if (surface == NULL)
		return;

	shsurf = shell_surface::get_shell_surface(surface);
	if (shsurf == NULL)
		return;

	if (!shsurf->shell_surface_is_xdg_surface())
		return;

	shsurf->state_requested = true;
	shsurf->requested_state.maximized = !shsurf->state.maximized;
	shsurf->send_configure_for_surface();
}

static void
desktop_shell::fullscreen_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data)
{
	struct weston_surface *focus = seat->keyboard->focus;
	struct weston_surface *surface;
	shell_surface *shsurf;

	surface = weston_surface_get_main_surface(focus);
	if (surface == NULL)
		return;

	shsurf = shell_surface::get_shell_surface(surface);
	if (shsurf == NULL)
		return;

	if (!shsurf->shell_surface_is_xdg_surface())
		return;

	shsurf->state_requested = true;
	shsurf->requested_state.fullscreen = !shsurf->state.fullscreen;
	shsurf->fullscreen_output = shsurf->output;
	shsurf->send_configure_for_surface();
}

static void
desktop_shell::touch_move_binding(struct weston_seat *seat, uint32_t time, void *data)
{
	struct weston_surface *focus;
	struct weston_surface *surface;
	shell_surface *shsurf;

	if (seat->touch->focus == NULL)
		return;

	focus = seat->touch->focus->surface;
	surface = weston_surface_get_main_surface(focus);
	if (surface == NULL)
		return;

	shsurf = shell_surface::get_shell_surface(surface);
	if (shsurf == NULL || shsurf->state.fullscreen ||
	    shsurf->state.maximized)
		return;

	shsurf->surface_touch_move((struct weston_seat *) seat);
}

static void
desktop_shell::resize_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data)
{
	struct weston_surface *focus;
	struct weston_surface *surface;
	uint32_t edges = 0;
	int32_t x, y;
	shell_surface *shsurf;

	if (seat->pointer->focus == NULL)
		return;

	focus = seat->pointer->focus->surface;

	surface = weston_surface_get_main_surface(focus);
	if (surface == NULL)
		return;

	shsurf = shell_surface::get_shell_surface(surface);
	if (shsurf == NULL || shsurf->state.fullscreen ||
	    shsurf->state.maximized)
		return;

	weston_view_from_global(shsurf->view,
				wl_fixed_to_int(seat->pointer->grab_x),
				wl_fixed_to_int(seat->pointer->grab_y),
				&x, &y);

	if (x < shsurf->surface->width / 3)
		edges |= WL_SHELL_SURFACE_RESIZE_LEFT;
	else if (x < 2 * shsurf->surface->width / 3)
		edges |= 0;
	else
		edges |= WL_SHELL_SURFACE_RESIZE_RIGHT;

	if (y < shsurf->surface->height / 3)
		edges |= WL_SHELL_SURFACE_RESIZE_TOP;
	else if (y < 2 * shsurf->surface->height / 3)
		edges |= 0;
	else
		edges |= WL_SHELL_SURFACE_RESIZE_BOTTOM;

	shsurf->surface_resize((struct weston_seat *) seat, edges);
}

void desktop_shell::handle_output_create(struct weston_output *output)
{
	this->create_shell_output(output);
}

void desktop_shell::setup_output_destroy_handler(struct weston_compositor *ec)
{
	struct weston_output *output;

	wl_list_init(&this->output_list);
	wl_list_for_each(output, &ec->output_list, link)
		this->create_shell_output(output);

	this->output_create_listener= cxx_wl_listener<desktop_shell, struct weston_output>{this, &desktop_shell::handle_output_create};
	wl_signal_add(&ec->output_created_signal,
				&this->output_create_listener.listener);

	this->output_move_listener = cxx_wl_listener<desktop_shell, void>(this, &desktop_shell::handle_output_move);
	wl_signal_add(&ec->output_moved_signal, &this->output_move_listener.listener);
}

static void
shell_reposition_view_on_output_destroy(struct weston_view *view)
{
	struct weston_output *output, *first_output;
	struct weston_compositor *ec = view->surface->compositor;
	shell_surface *shsurf;
	float x, y;
	int visible;

	x = view->geometry.x;
	y = view->geometry.y;

	/* At this point the destroyed output is not in the list anymore.
	 * If the view is still visible somewhere, we leave where it is,
	 * otherwise, move it to the first output. */
	visible = 0;
	wl_list_for_each(output, &ec->output_list, link) {
		if (pixman_region32_contains_point(&output->region,
						   x, y, NULL)) {
			visible = 1;
			break;
		}
	}

	if (!visible) {
		first_output = container_of(ec->output_list.next,
					    struct weston_output, link);

		x = first_output->x + first_output->width / 4;
		y = first_output->y + first_output->height / 4;

		weston_view_set_position(view, x, y);
	} else {
		weston_view_geometry_dirty(view);
	}


	shsurf = shell_surface::get_shell_surface(view->surface);

	if (shsurf) {
		shsurf->saved_position_valid = false;
		shsurf->next_state.maximized = false;
		shsurf->next_state.fullscreen = false;
		shsurf->state_changed = true;
	}
}


static void
shell_output_destroy_move_layer(struct desktop_shell *shell,
				struct weston_layer *layer,
				void *data)
{
	struct weston_output *output = data;
	struct weston_view *view;

	wl_list_for_each(view, &layer->view_list.link, layer_link.link) {
		if (view->output != output)
			continue;

		shell_reposition_view_on_output_destroy(view);
	}
}


static void
handle_output_destroy(struct wl_listener *listener, void *data)
{
	struct shell_output *output_listener =
		container_of(listener, struct shell_output, destroy_listener);
	struct weston_output *output = output_listener->output;
	struct desktop_shell *shell = output_listener->shell;

	shell->shell_for_each_layer(shell_output_destroy_move_layer, output);

	wl_list_remove(&output_listener->destroy_listener.link);
	wl_list_remove(&output_listener->link);
	free(output_listener);
}

void desktop_shell::create_shell_output(struct weston_output *output)
{
	struct shell_output *shell_output;

	shell_output = zalloc(sizeof *shell_output);
	if (shell_output == NULL)
		return;

	shell_output->output = output;
	shell_output->shell = this;
	shell_output->destroy_listener.notify = handle_output_destroy;
	wl_signal_add(&output->destroy_signal,
		      &shell_output->destroy_listener);
	wl_list_insert(this->output_list.prev, &shell_output->link);
}

static void
handle_output_move_layer(struct desktop_shell *shell,
			 struct weston_layer *layer, void *data)
{
	struct weston_output *output = data;
	struct weston_view *view;
	float x, y;

	wl_list_for_each(view, &layer->view_list.link, layer_link.link) {
		if (view->output != output)
			continue;

		x = view->geometry.x + output->move_x;
		y = view->geometry.y + output->move_y;
		weston_view_set_position(view, x, y);
	}
}

void desktop_shell::handle_output_move(void *data)
{
	this->shell_for_each_layer(handle_output_move_layer, data);
}

void
desktop_shell::shell_for_each_layer(shell_for_each_layer_func_t func, void *data)
{
	struct workspace **ws;

	func(this, &this->fullscreen_layer, data);
	func(this, &this->panel_layer, data);
	func(this, &this->background_layer, data);
	func(this, &this->lock_layer, data);
	func(this, &this->input_panel_layer, data);

	wl_array_for_each(ws, &this->workspaces.array)
		func(this, &(*ws)->layer, data);
}


void desktop_shell::map(shell_surface *shsurf, int32_t sx, int32_t sy)
{
	struct weston_compositor *compositor = this->compositor;
	struct weston_seat *seat;

	/* initial positioning, see also configure() */
	switch (shsurf->type) {
	case SHELL_SURFACE_TOPLEVEL:
		if (shsurf->state.fullscreen) {
			center_on_output(shsurf->view, shsurf->fullscreen_output);
			shsurf->shell_map_fullscreen();
		} else if (shsurf->state.maximized) {
			this->set_maximized_position(shsurf);
		} else if (!shsurf->state.relative) {

			weston_matrix_init(&(shsurf->rotation.transform.matrix));
			//weston_matrix_scale(&(shsurf->rotation.transform.matrix),0.5,0.5,0.5);
			wl_list_insert(&shsurf->view->geometry.transformation_list, &(shsurf->rotation.transform.link));

			weston_view_set_initial_position(shsurf->view, this);
		}
		break;
	case SHELL_SURFACE_POPUP:
		shsurf->shell_map_popup();
		break;
	case SHELL_SURFACE_NONE:
		weston_view_set_position(shsurf->view,
					 shsurf->view->geometry.x + sx,
					 shsurf->view->geometry.y + sy);
		break;
	case SHELL_SURFACE_XWAYLAND:
	default:
		;
	}

	/* Surface stacking order, see also activate(). */
	shsurf->shell_surface_update_layer();

	if (shsurf->type != SHELL_SURFACE_NONE) {
		weston_view_update_transform(shsurf->view);
		if (shsurf->state.maximized) {
			shsurf->surface->output = shsurf->output;
			shsurf->view->output = shsurf->output;
		}
	}

	switch (shsurf->type) {
	/* XXX: xwayland's using the same fields for transient type */
	case SHELL_SURFACE_XWAYLAND:
		if (shsurf->transient.flags ==
				WL_SHELL_SURFACE_TRANSIENT_INACTIVE)
			break;
	case SHELL_SURFACE_TOPLEVEL:
		if (shsurf->state.relative &&
		    shsurf->transient.flags == WL_SHELL_SURFACE_TRANSIENT_INACTIVE)
			break;
		if (this->locked)
			break;
		wl_list_for_each(seat, &compositor->seat_list, link)
			page::activate(this, shsurf->surface, seat, true);
		break;
	case SHELL_SURFACE_POPUP:
	case SHELL_SURFACE_NONE:
	default:
		break;
	}

	if (shsurf->type == SHELL_SURFACE_TOPLEVEL &&
	    !shsurf->state.maximized && !shsurf->state.fullscreen)
	{
		switch (this->win_animation_type) {
		case ANIMATION_FADE:
			weston_fade_run(shsurf->view, 0.0, 1.0, 300.0, NULL, NULL);
			break;
		case ANIMATION_ZOOM:
			weston_zoom_run(shsurf->view, 0.5, 1.0, NULL, NULL);
			break;
		case ANIMATION_NONE:
		default:
			break;
		}
	}
}

void desktop_shell::configure(struct weston_surface *surface, float x, float y)
{
	shell_surface *shsurf;
	struct weston_view *view;

	shsurf = shell_surface::get_shell_surface(surface);

	assert(shsurf);

	if (shsurf->state.fullscreen)
		shsurf->shell_configure_fullscreen();
	else if (shsurf->state.maximized) {
		this->set_maximized_position(shsurf);
	} else {
		weston_view_set_position(shsurf->view, x, y);
	}

	/* XXX: would a fullscreen surface need the same handling? */
	if (surface->output) {
		wl_list_for_each(view, &surface->views, surface_link)
			weston_view_update_transform(view);

		if (shsurf->state.maximized)
			surface->output = shsurf->output;
	}
}


void desktop_shell::set_maximized_position(shell_surface *shsurf)
{
	int32_t surf_x, surf_y;
	pixman_rectangle32_t area;
	pixman_box32_t *e;

	this->get_output_work_area(shsurf->output, &area);
	surface_subsurfaces_boundingbox(shsurf->surface,
					&surf_x, &surf_y, NULL, NULL);
	e = pixman_region32_extents(&shsurf->output->region);

	weston_view_set_position(shsurf->view,
				 e->x1 + area.x - surf_x,
				 e->y1 + area.y - surf_y);
}

//
//static void
//desktop_shell::debug_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data)
//{
//	struct debug_binding_grab *grab;
//
//	grab = calloc(1, sizeof *grab);
//	if (!grab)
//		return;
//
//	grab->seat = (struct weston_seat *) seat;
//	grab->key[0] = key;
//	grab->grab.interface = &debug_binding_keyboard_grab;
//	weston_keyboard_start_grab(seat->keyboard, &grab->grab);
//}
//
//static void
//desktop_shell::force_kill_binding(struct weston_seat *seat, uint32_t time, uint32_t key,
//		   void *data)
//{
//	struct weston_surface *focus_surface;
//	struct wl_client *client;
//	struct desktop_shell *shell = data;
//	struct weston_compositor *compositor = shell->compositor;
//	pid_t pid;
//
//	focus_surface = seat->keyboard->focus;
//	if (!focus_surface)
//		return;
//
//	wl_signal_emit(&compositor->kill_signal, focus_surface);
//
//	client = wl_resource_get_client(focus_surface->resource);
//	wl_client_get_credentials(client, &pid, NULL, NULL);
//
//	/* Skip clients that we launched ourselves (the credentials of
//	 * the socketpair is ours) */
//	if (pid == getpid())
//		return;
//
//	kill(pid, SIGKILL);
//}
//
//static void
//desktop_shell::workspace_up_binding(struct weston_seat *seat, uint32_t time,
//		     uint32_t key, void *data)
//{
//	struct desktop_shell *shell = data;
//	unsigned int new_index = shell->workspaces.current;
//
//	if (shell->locked)
//		return;
//	if (new_index != 0)
//		new_index--;
//
//	change_workspace(shell, new_index);
//}
//
//static void
//desktop_shell::workspace_down_binding(struct weston_seat *seat, uint32_t time,
//		       uint32_t key, void *data)
//{
//	struct desktop_shell *shell = data;
//	unsigned int new_index = shell->workspaces.current;
//
//	if (shell->locked)
//		return;
//	if (new_index < shell->workspaces.num - 1)
//		new_index++;
//
//	change_workspace(shell, new_index);
//}
//
//static void
//desktop_shell::workspace_f_binding(struct weston_seat *seat, uint32_t time,
//		    uint32_t key, void *data)
//{
//	struct desktop_shell *shell = data;
//	unsigned int new_index;
//
//	if (shell->locked)
//		return;
//	new_index = key - KEY_F1;
//	if (new_index >= shell->workspaces.num)
//		new_index = shell->workspaces.num - 1;
//
//	change_workspace(shell, new_index);
//}
//
//static void
//desktop_shell::workspace_move_surface_up_binding(struct weston_seat *seat, uint32_t time,
//				  uint32_t key, void *data)
//{
//	struct desktop_shell *shell = data;
//	unsigned int new_index = shell->workspaces.current;
//
//	if (shell->locked)
//		return;
//
//	if (new_index != 0)
//		new_index--;
//
//	take_surface_to_workspace_by_seat(shell, seat, new_index);
//}
//
//static void
//desktop_shell::workspace_move_surface_down_binding(struct weston_seat *seat, uint32_t time,
//				    uint32_t key, void *data)
//{
//	struct desktop_shell *shell = data;
//	unsigned int new_index = shell->workspaces.current;
//
//	if (shell->locked)
//		return;
//
//	if (new_index < shell->workspaces.num - 1)
//		new_index++;
//
//	take_surface_to_workspace_by_seat(shell, seat, new_index);
//}
//
//




