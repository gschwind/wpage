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

#include "compositor.h"
#include "shell.h"
#include "exposay.hxx"
#include "grab_handlers.hxx"
#include "focus_state.hxx"
#include "surface.hxx"


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
	struct desktop_shell *shell = reinterpret_cast<desktop_shell*>(data);

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
	struct desktop_shell *shell = data;

	if (shell->startup_animation_type == ANIMATION_FADE)
		shell->shell_fade(FADE_IN);
	else if (shell->startup_animation_type == ANIMATION_NONE) {
		weston_surface_destroy(shell->fade.view->surface);
		shell->fade.view = NULL;
	}
}
