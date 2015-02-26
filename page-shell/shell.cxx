/*
 * Copyright © 2010-2012 Intel Corporation
 * Copyright © 2011-2012 Collabora, Ltd.
 * Copyright © 2013 Raspberry Pi Foundation
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <assert.h>
#include <signal.h>
#include <math.h>
#include <sys/types.h>

#include "shell.h"
#include "desktop-shell-server-protocol.h"
#include "workspaces-server-protocol.h"
#include "../shared/config-parser.h"
#include "xdg-shell-server-protocol.h"

#include "protocols_implementation.hxx"

#include "surface.hxx"
#include "shell_seat.hxx"
#include "desktop_shell.hxx"
#include "focus_state.hxx"


#define DEFAULT_NUM_WORKSPACES 1
#define DEFAULT_WORKSPACE_CHANGE_ANIMATION_LENGTH 200

#ifndef static_assert
#define static_assert(cond, msg)
#endif


/*
 * Surface stacking and ordering.
 *
 * This is handled using several linked lists of surfaces, organised into
 * ‘layers’. The layers are ordered, and each of the surfaces in one layer are
 * above all of the surfaces in the layer below. The set of layers is static and
 * in the following order (top-most first):
 *  • Lock layer (only ever displayed on its own)
 *  • Cursor layer
 *  • Input panel layer
 *  • Fullscreen layer
 *  • Panel layer
 *  • Workspace layers
 *  • Background layer
 *
 * The list of layers may be manipulated to remove whole layers of surfaces from
 * display. For example, when locking the screen, all layers except the lock
 * layer are removed.
 *
 * A surface’s layer is modified on configuring the surface, in
 * set_surface_type() (which is only called when the surface’s type change is
 * _committed_). If a surface’s type changes (e.g. when making a window
 * fullscreen) its layer changes too.
 *
 * In order to allow popup and transient surfaces to be correctly stacked above
 * their parent surfaces, each surface tracks both its parent surface, and a
 * linked list of its children. When a surface’s layer is updated, so are the
 * layers of its children. Note that child surfaces are *not* the same as
 * subsurfaces — child/parent surfaces are purely for maintaining stacking
 * order.
 *
 * The children_link list of siblings of a surface (i.e. those surfaces which
 * have the same parent) only contains weston_surfaces which have a
 * shell_surface. Stacking is not implemented for non-shell_surface
 * weston_surfaces. This means that the following implication does *not* hold:
 *     (shsurf->parent != NULL) ⇒ !wl_list_is_empty(shsurf->children_link)
 */

static void
background_configure(struct weston_surface *es, int32_t sx, int32_t sy);

static desktop_shell *
shell_surface_get_shell(shell_surface *shsurf);

static void
shell_fade_startup(struct desktop_shell *shell);

static bool
shell_surface_is_xdg_popup(shell_surface *shsurf);

static void
panel_configure(struct weston_surface *es, int32_t sx, int32_t sy);

static void
destroy_shell_grab_shsurf(struct wl_listener *listener, void *data)
{
	struct shell_grab *grab;

	grab = container_of(listener, struct shell_grab,
			    shsurf_destroy_listener);

	grab->shsurf = NULL;
}

weston_view *
get_default_view(weston_surface *surface)
{
	shell_surface *shsurf;
	weston_view *view;

	if (!surface || wl_list_empty(&surface->views))
		return NULL;

	shsurf = shell_surface::get_shell_surface(surface);
	if (shsurf)
		return shsurf->view;

	wl_list_for_each(view, &surface->views, surface_link)
		if (weston_view_is_mapped(view))
			return view;

	return container_of(surface->views.next, weston_view, surface_link);
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

static void
shell_configuration(struct desktop_shell *shell)
{
	struct weston_config_section *section;
	int duration;
	char *s, *client;
	int ret;

	section = weston_config_get_section(shell->compositor->config,
					    "screensaver", NULL, NULL);
	weston_config_section_get_string(section,
					 "path", &shell->screensaver.path, NULL);
	weston_config_section_get_int(section, "duration", &duration, 60);
	shell->screensaver.duration = duration * 1000;

	section = weston_config_get_section(shell->compositor->config,
					    "shell", NULL, NULL);
	ret = asprintf(&client, "%s/%s", weston_config_get_libexec_dir(),
		       WESTON_SHELL_CLIENT);
	if (ret < 0)
		client = NULL;
	weston_config_section_get_string(section,
					 "client", &s, client);
	free(client);
	shell->client = s;
	weston_config_section_get_string(section,
					 "binding-modifier", &s, "super");
	shell->binding_modifier = get_modifier(s);
	free(s);

	weston_config_section_get_string(section,
					 "exposay-modifier", &s, "none");
	if (strcmp(s, "none") == 0)
		shell->exposay_modifier = 0;
	else
		shell->exposay_modifier = get_modifier(s);
	free(s);

	weston_config_section_get_string(section, "animation", &s, "none");
	shell->win_animation_type = get_animation_type(s);
	free(s);
	weston_config_section_get_string(section, "close-animation", &s, "fade");
	shell->win_close_animation_type = get_animation_type(s);
	free(s);
	weston_config_section_get_string(section,
					 "startup-animation", &s, "fade");
	shell->startup_animation_type = get_animation_type(s);
	free(s);
	if (shell->startup_animation_type == ANIMATION_ZOOM)
		shell->startup_animation_type = ANIMATION_NONE;
	weston_config_section_get_string(section, "focus-animation", &s, "none");
	shell->focus_animation_type = get_animation_type(s);
	free(s);
	weston_config_section_get_uint(section, "num-workspaces",
				       &shell->workspaces.num,
				       DEFAULT_NUM_WORKSPACES);
}

struct weston_output *
get_default_output(struct weston_compositor *compositor)
{
	return container_of(compositor->output_list.next,
			    struct weston_output, link);
}


/* no-op func for checking focus surface */
static void
focus_surface_configure(struct weston_surface *es, int32_t sx, int32_t sy)
{
}

static struct focus_surface *
get_focus_surface(struct weston_surface *surface)
{
	if (surface->configure == focus_surface_configure)
		return reinterpret_cast<struct focus_surface *>(surface->configure_private);
	else
		return nullptr;
}

static bool
is_focus_surface (struct weston_surface *es)
{
	return (es->configure == focus_surface_configure);
}

static bool
is_focus_view (struct weston_view *view)
{
	return is_focus_surface (view->surface);
}

static struct focus_surface *
create_focus_surface(struct weston_compositor *ec,
		     struct weston_output *output)
{
	struct focus_surface *fsurf = NULL;
	struct weston_surface *surface = NULL;

	fsurf = reinterpret_cast<struct focus_surface *>(malloc(sizeof *fsurf));
	if (!fsurf)
		return NULL;

	fsurf->surface = weston_surface_create(ec);
	surface = fsurf->surface;
	if (surface == NULL) {
		free(fsurf);
		return NULL;
	}

	surface->configure = focus_surface_configure;
	surface->output = output;
	surface->configure_private = fsurf;

	fsurf->view = weston_view_create(surface);
	if (fsurf->view == NULL) {
		weston_surface_destroy(surface);
		free(fsurf);
		return NULL;
	}
	fsurf->view->output = output;

	weston_surface_set_size(surface, output->width, output->height);
	weston_view_set_position(fsurf->view, output->x, output->y);
	weston_surface_set_color(surface, 0.0, 0.0, 0.0, 1.0);
	pixman_region32_fini(&surface->opaque);
	pixman_region32_init_rect(&surface->opaque, output->x, output->y,
				  output->width, output->height);
	pixman_region32_fini(&surface->input);
	pixman_region32_init(&surface->input);

	wl_list_init(&fsurf->workspace_transform.link);

	return fsurf;
}

static void
focus_surface_destroy(struct focus_surface *fsurf)
{
	weston_surface_destroy(fsurf->surface);
	free(fsurf);
}

static void
focus_animation_done(struct weston_view_animation *animation, void *data)
{
	struct workspace *ws = reinterpret_cast<struct workspace *>(data);
	ws->focus_animation = nullptr;
}

static void
focus_state_destroy(struct focus_state *state)
{
	wl_list_remove(&state->seat_destroy_listener.link);
	wl_list_remove(&state->surface_destroy_listener.link);
	free(state);
}

static void
focus_state_seat_destroy(struct wl_listener *listener, void *data)
{
	struct focus_state *state = container_of(listener,
						 struct focus_state,
						 seat_destroy_listener);

	wl_list_remove(&state->link);
	focus_state_destroy(state);
}

static void
focus_state_surface_destroy(struct wl_listener *listener, void *data)
{
	struct focus_state *state = container_of(listener,
						 struct focus_state,
						 surface_destroy_listener);
	struct desktop_shell *shell;
	struct weston_surface *main_surface, *next;
	struct weston_view *view;

	main_surface = weston_surface_get_main_surface(state->keyboard_focus);

	next = NULL;
	wl_list_for_each(view,
			 &state->ws->layer.view_list.link, layer_link.link) {
		if (view->surface == main_surface)
			continue;
		if (is_focus_view(view))
			continue;

		next = view->surface;
		break;
	}

	/* if the focus was a sub-surface, activate its main surface */
	if (main_surface != state->keyboard_focus)
		next = main_surface;

	shell = reinterpret_cast<desktop_shell*>(state->seat->compositor->shell_interface.shell);
	if (next) {
		state->keyboard_focus = NULL;
		activate(shell, next, state->seat, true);
	} else {
		if (shell->focus_animation_type == ANIMATION_DIM_LAYER) {
			if (state->ws->focus_animation)
				weston_view_animation_destroy(state->ws->focus_animation);

			state->ws->focus_animation = weston_fade_run(
				state->ws->fsurf_front->view,
				state->ws->fsurf_front->view->alpha, 0.0, 300,
				focus_animation_done, state->ws);
		}

		wl_list_remove(&state->link);
		focus_state_destroy(state);
	}
}

static struct focus_state *
focus_state_create(struct weston_seat *seat, struct workspace *ws)
{
	struct focus_state *state;

	state = reinterpret_cast<struct focus_state *>(malloc(sizeof *state));
	if (state == NULL)
		return NULL;

	state->keyboard_focus = NULL;
	state->ws = ws;
	state->seat = seat;
	wl_list_insert(&ws->focus_list, &state->link);

	state->seat_destroy_listener.notify = focus_state_seat_destroy;
	state->surface_destroy_listener.notify = focus_state_surface_destroy;
	wl_signal_add(&seat->destroy_signal,
		      &state->seat_destroy_listener);
	wl_list_init(&state->surface_destroy_listener.link);

	return state;
}

static struct focus_state *
ensure_focus_state(struct desktop_shell *shell, struct weston_seat *seat)
{
	struct workspace *ws = get_current_workspace(shell);
	struct focus_state *state;

	wl_list_for_each(state, &ws->focus_list, link)
		if (state->seat == seat)
			break;

	if (&state->link == &ws->focus_list)
		state = focus_state_create(seat, ws);

	return state;
}


static void
replace_focus_state(desktop_shell *shell, struct workspace *ws,
		    struct weston_seat *seat)
{
	struct focus_state *state;

	wl_list_for_each(state, &ws->focus_list, link) {
		if (state->seat == seat) {
			state->focus_state_set_focus(seat->keyboard->focus);
			return;
		}
	}
}

static void
animate_focus_change(struct desktop_shell *shell, struct workspace *ws,
		     struct weston_view *from, struct weston_view *to)
{
	struct weston_output *output;
	bool focus_surface_created = false;

	/* FIXME: Only support dim animation using two layers */
	if (from == to || shell->focus_animation_type != ANIMATION_DIM_LAYER)
		return;

	output = get_default_output(shell->compositor);
	if (ws->fsurf_front == NULL && (from || to)) {
		ws->fsurf_front = create_focus_surface(shell->compositor, output);
		if (ws->fsurf_front == NULL)
			return;
		ws->fsurf_front->view->alpha = 0.0;

		ws->fsurf_back = create_focus_surface(shell->compositor, output);
		if (ws->fsurf_back == NULL) {
			focus_surface_destroy(ws->fsurf_front);
			return;
		}
		ws->fsurf_back->view->alpha = 0.0;

		focus_surface_created = true;
	} else {
		weston_layer_entry_remove(&ws->fsurf_front->view->layer_link);
		weston_layer_entry_remove(&ws->fsurf_back->view->layer_link);
	}

	if (ws->focus_animation) {
		weston_view_animation_destroy(ws->focus_animation);
		ws->focus_animation = NULL;
	}

	if (to)
		weston_layer_entry_insert(&to->layer_link,
					  &ws->fsurf_front->view->layer_link);
	else if (from)
		weston_layer_entry_insert(&ws->layer.view_list,
					  &ws->fsurf_front->view->layer_link);

	if (focus_surface_created) {
		ws->focus_animation = weston_fade_run(
			ws->fsurf_front->view,
			ws->fsurf_front->view->alpha, 0.4, 300,
			focus_animation_done, ws);
	} else if (from) {
		weston_layer_entry_insert(&from->layer_link,
					  &ws->fsurf_back->view->layer_link);
		ws->focus_animation = weston_stable_fade_run(
			ws->fsurf_front->view, 0.0,
			ws->fsurf_back->view, 0.4,
			focus_animation_done, ws);
	} else if (to) {
		weston_layer_entry_insert(&ws->layer.view_list,
					  &ws->fsurf_back->view->layer_link);
		ws->focus_animation = weston_stable_fade_run(
			ws->fsurf_front->view, 0.0,
			ws->fsurf_back->view, 0.4,
			focus_animation_done, ws);
	}
}

static void
workspace_destroy(struct workspace *ws)
{
	struct focus_state *state, *next;

	wl_list_for_each_safe(state, next, &ws->focus_list, link)
		focus_state_destroy(state);

	if (ws->fsurf_front)
		focus_surface_destroy(ws->fsurf_front);
	if (ws->fsurf_back)
		focus_surface_destroy(ws->fsurf_back);

	free(ws);
}

static void
seat_destroyed(struct wl_listener *listener, void *data)
{
	 weston_seat *seat = data;
	 focus_state *state, *next;
	 workspace *ws = container_of(listener,
					    struct workspace,
					    seat_destroyed_listener);

	wl_list_for_each_safe(state, next, &ws->focus_list, link)
		if (state->seat == seat)
			wl_list_remove(&state->link);
}

static  workspace *
workspace_create(void)
{
	 workspace *ws = malloc(sizeof *ws);
	if (ws == NULL)
		return NULL;

	weston_layer_init(&ws->layer, NULL);

	wl_list_init(&ws->focus_list);
	wl_list_init(&ws->seat_destroyed_listener.link);
	ws->seat_destroyed_listener.notify = seat_destroyed;
	ws->fsurf_front = NULL;
	ws->fsurf_back = NULL;
	ws->focus_animation = NULL;

	return ws;
}

static int
workspace_is_empty( workspace *ws)
{
	return wl_list_empty(&ws->layer.view_list.link);
}

struct workspace *
get_current_workspace(desktop_shell *shell)
{
	return shell->get_workspace(shell->workspaces.current);
}

static void
activate_workspace( desktop_shell *shell, unsigned int index)
{
	struct workspace *ws;

	ws = shell->get_workspace(index);
	wl_list_insert(&shell->panel_layer.link, &ws->layer.link);

	shell->workspaces.current = index;
}

static unsigned int
get_output_height( weston_output *output)
{
	return abs(output->region.extents.y1 - output->region.extents.y2);
}

static void
view_translate( workspace *ws,  weston_view *view, double d)
{
	struct weston_transform *transform;

	if (is_focus_view(view)) {
		struct focus_surface *fsurf = get_focus_surface(view->surface);
		transform = &fsurf->workspace_transform;
	} else {
		shell_surface *shsurf = shell_surface::get_shell_surface(view->surface);
		transform = &shsurf->workspace_transform;
	}

	if (wl_list_empty(&transform->link))
		wl_list_insert(view->geometry.transformation_list.prev,
			       &transform->link);

	weston_matrix_init(&transform->matrix);
	weston_matrix_translate(&transform->matrix,
				0.0, d, 0.0);
	weston_view_geometry_dirty(view);
}

static void
workspace_translate_out(struct workspace *ws, double fraction)
{
	struct weston_view *view;
	unsigned int height;
	double d;

	wl_list_for_each(view, &ws->layer.view_list.link, layer_link.link) {
		height = get_output_height(view->surface->output);
		d = height * fraction;

		view_translate(ws, view, d);
	}
}

static void
workspace_translate_in(struct workspace *ws, double fraction)
{
	struct weston_view *view;
	unsigned int height;
	double d;

	wl_list_for_each(view, &ws->layer.view_list.link, layer_link.link) {
		height = get_output_height(view->surface->output);

		if (fraction > 0)
			d = -(height - height * fraction);
		else
			d = height + height * fraction;

		view_translate(ws, view, d);
	}
}

static void
broadcast_current_workspace_state( desktop_shell *shell)
{
	struct wl_resource *resource;

	wl_resource_for_each(resource, &shell->workspaces.client_list)
		workspace_manager_send_state(resource,
					     shell->workspaces.current,
					     shell->workspaces.num);
}

static void
reverse_workspace_change_animation( desktop_shell *shell,
				   unsigned int index,
				    workspace *from,
				    workspace *to)
{
	shell->workspaces.current = index;

	shell->workspaces.anim_to = to;
	shell->workspaces.anim_from = from;
	shell->workspaces.anim_dir = -1 * shell->workspaces.anim_dir;
	shell->workspaces.anim_timestamp = 0;

	weston_compositor_schedule_repaint(shell->compositor);
}

static void
workspace_deactivate_transforms( workspace *ws)
{
	struct weston_view *view;
	struct weston_transform *transform;

	wl_list_for_each(view, &ws->layer.view_list.link, layer_link.link) {
		if (is_focus_view(view)) {
			struct focus_surface *fsurf = get_focus_surface(view->surface);
			transform = &fsurf->workspace_transform;
		} else {
			shell_surface *shsurf = shell_surface::get_shell_surface(view->surface);
			transform = &shsurf->workspace_transform;
		}

		if (!wl_list_empty(&transform->link)) {
			wl_list_remove(&transform->link);
			wl_list_init(&transform->link);
		}
		weston_view_geometry_dirty(view);
	}
}

static void
finish_workspace_change_animation( desktop_shell *shell,
				   workspace *from,
				   workspace *to)
{
	 weston_view *view;

	weston_compositor_schedule_repaint(shell->compositor);

	/* Views that extend past the bottom of the output are still
	 * visible after the workspace animation ends but before its layer
	 * is hidden. In that case, we need to damage below those views so
	 * that the screen is properly repainted. */
	wl_list_for_each(view, &from->layer.view_list.link, layer_link.link)
		weston_view_damage_below(view);

	wl_list_remove(&shell->workspaces.animation.link);
	workspace_deactivate_transforms(from);
	workspace_deactivate_transforms(to);
	shell->workspaces.anim_to = NULL;

	wl_list_remove(&shell->workspaces.anim_from->layer.link);
}

static void
animate_workspace_change_frame( weston_animation *animation,
			        weston_output *output, uint32_t msecs)
{
	 desktop_shell *shell =
		container_of(animation, struct desktop_shell,
			     workspaces.animation);
	 workspace *from = shell->workspaces.anim_from;
	 workspace *to = shell->workspaces.anim_to;
	uint32_t t;
	double x, y;

	if (workspace_is_empty(from) && workspace_is_empty(to)) {
		finish_workspace_change_animation(shell, from, to);
		return;
	}

	if (shell->workspaces.anim_timestamp == 0) {
		if (shell->workspaces.anim_current == 0.0)
			shell->workspaces.anim_timestamp = msecs;
		else
			shell->workspaces.anim_timestamp =
				msecs -
				/* Invers of movement function 'y' below. */
				(asin(1.0 - shell->workspaces.anim_current) *
				 DEFAULT_WORKSPACE_CHANGE_ANIMATION_LENGTH *
				 M_2_PI);
	}

	t = msecs - shell->workspaces.anim_timestamp;

	/*
	 * x = [0, π/2]
	 * y(x) = sin(x)
	 */
	x = t * (1.0/DEFAULT_WORKSPACE_CHANGE_ANIMATION_LENGTH) * M_PI_2;
	y = sin(x);

	if (t < DEFAULT_WORKSPACE_CHANGE_ANIMATION_LENGTH) {
		weston_compositor_schedule_repaint(shell->compositor);

		workspace_translate_out(from, shell->workspaces.anim_dir * y);
		workspace_translate_in(to, shell->workspaces.anim_dir * y);
		shell->workspaces.anim_current = y;

		weston_compositor_schedule_repaint(shell->compositor);
	}
	else
		finish_workspace_change_animation(shell, from, to);
}

static void
animate_workspace_change( desktop_shell *shell,
			 unsigned int index,
			  workspace *from,
			  workspace *to)
{
	struct weston_output *output;

	int dir;

	if (index > shell->workspaces.current)
		dir = -1;
	else
		dir = 1;

	shell->workspaces.current = index;

	shell->workspaces.anim_dir = dir;
	shell->workspaces.anim_from = from;
	shell->workspaces.anim_to = to;
	shell->workspaces.anim_current = 0.0;
	shell->workspaces.anim_timestamp = 0;

	output = container_of(shell->compositor->output_list.next,
			       weston_output, link);
	wl_list_insert(&output->animation_list,
		       &shell->workspaces.animation.link);

	wl_list_insert(from->layer.link.prev, &to->layer.link);

	workspace_translate_in(to, 0);

	shell->restore_focus_state(to);

	weston_compositor_schedule_repaint(shell->compositor);
}

static void
update_workspace( desktop_shell *shell, unsigned int index,
		  workspace *from,  workspace *to)
{
	shell->workspaces.current = index;
	wl_list_insert(&from->layer.link, &to->layer.link);
	wl_list_remove(&from->layer.link);
}

static void
change_workspace( desktop_shell *shell, unsigned int index)
{
	struct workspace *from;
	struct workspace *to;
	struct focus_state *state;

	if (index == shell->workspaces.current)
		return;

	/* Don't change workspace when there is any fullscreen surfaces. */
	if (!wl_list_empty(&shell->fullscreen_layer.view_list.link))
		return;

	from = get_current_workspace(shell);
	to = shell->get_workspace(index);

	if (shell->workspaces.anim_from == to &&
	    shell->workspaces.anim_to == from) {
		shell->restore_focus_state(to);
		reverse_workspace_change_animation(shell, index, from, to);
		broadcast_current_workspace_state(shell);
		return;
	}

	if (shell->workspaces.anim_to != NULL)
		finish_workspace_change_animation(shell,
						  shell->workspaces.anim_from,
						  shell->workspaces.anim_to);

	shell->restore_focus_state(to);

	if (shell->focus_animation_type != ANIMATION_NONE) {
		wl_list_for_each(state, &from->focus_list, link)
			if (state->keyboard_focus)
				animate_focus_change(shell, from,
						     get_default_view(state->keyboard_focus), NULL);

		wl_list_for_each(state, &to->focus_list, link)
			if (state->keyboard_focus)
				animate_focus_change(shell, to,
						     NULL, get_default_view(state->keyboard_focus));
	}

	if (workspace_is_empty(to) && workspace_is_empty(from))
		update_workspace(shell, index, from, to);
	else
		animate_workspace_change(shell, index, from, to);

	broadcast_current_workspace_state(shell);
}

static bool
workspace_has_only( workspace *ws,  weston_surface *surface)
{
	 wl_list *list = &ws->layer.view_list.link;
	 wl_list *e;

	if (wl_list_empty(list))
		return false;

	e = list->next;

	if (e->next != list)
		return false;

	return container_of(e,  weston_view, layer_link.link)->surface == surface;
}


static void
take_surface_to_workspace_by_seat(struct desktop_shell *shell,
				  struct weston_seat *seat,
				  unsigned int index)
{
	struct weston_surface *surface;
	struct weston_view *view;
	shell_surface *shsurf;
	struct workspace *from;
	struct workspace *to;
	struct focus_state *state;

	surface = weston_surface_get_main_surface(seat->keyboard->focus);
	view = get_default_view(surface);
	if (view == NULL ||
	    index == shell->workspaces.current ||
	    is_focus_view(view))
		return;

	from = get_current_workspace(shell);
	to = shell->get_workspace(index);

	weston_layer_entry_remove(&view->layer_link);
	weston_layer_entry_insert(&to->layer.view_list, &view->layer_link);

	shsurf = shell_surface::get_shell_surface(surface);
	if (shsurf != NULL)
		shsurf->shell_surface_update_child_surface_layers();

	replace_focus_state(shell, to, seat);
	shell->drop_focus_state(from, surface);

	if (shell->workspaces.anim_from == to &&
	    shell->workspaces.anim_to == from) {
		wl_list_remove(&to->layer.link);
		wl_list_insert(from->layer.link.prev, &to->layer.link);

		reverse_workspace_change_animation(shell, index, from, to);
		broadcast_current_workspace_state(shell);

		return;
	}

	if (shell->workspaces.anim_to != NULL)
		finish_workspace_change_animation(shell,
						  shell->workspaces.anim_from,
						  shell->workspaces.anim_to);

	if (workspace_is_empty(from) &&
	    workspace_has_only(to, surface))
		update_workspace(shell, index, from, to);
	else {
		if (shsurf != NULL &&
		    wl_list_empty(&shsurf->workspace_transform.link))
			wl_list_insert(&shell->workspaces.anim_sticky_list,
				       &shsurf->workspace_transform.link);

		animate_workspace_change(shell, index, from, to);
	}

	broadcast_current_workspace_state(shell);

	state = ensure_focus_state(shell, seat);
	if (state != NULL)
		state->focus_state_set_focus(surface);
}

static void
unbind_resource(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}

static void
bind_workspace_manager(struct wl_client *client,
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
constrain_position(struct weston_move_grab *move, int *cx, int *cy)
{
	shell_surface *shsurf = move->base.shsurf;
	struct weston_pointer *pointer = move->base.grab.pointer;
	int x, y, panel_width, panel_height, bottom;
	const int safety = 50;

	x = wl_fixed_to_int(pointer->x + move->dx);
	y = wl_fixed_to_int(pointer->y + move->dy);

	if (shsurf->shell->panel_position == DESKTOP_SHELL_PANEL_POSITION_TOP) {
		shsurf->shell->get_output_panel_size(shsurf->surface->output,
				      &panel_width, &panel_height);

		bottom = y + shsurf->geometry.height;
		if (bottom - panel_height < safety)
			y = panel_height + safety -
				shsurf->geometry.height;

		if (move->client_initiated &&
		    y + shsurf->geometry.y < panel_height)
			y = panel_height - shsurf->geometry.y;
	}

	*cx = x;
	*cy = y;
}

/*
 * Returns the bounding box of a surface and all its sub-surfaces,
 * in the surface coordinates system. */
void
surface_subsurfaces_boundingbox(struct weston_surface *surface, int32_t *x,
				int32_t *y, int32_t *w, int32_t *h) {
	pixman_region32_t region;
	pixman_box32_t *box;
	struct weston_subsurface *subsurface;

	pixman_region32_init_rect(&region, 0, 0,
	                          surface->width,
	                          surface->height);

	wl_list_for_each(subsurface, &surface->subsurface_list, parent_link) {
		pixman_region32_union_rect(&region, &region,
		                           subsurface->position.x,
		                           subsurface->position.y,
		                           subsurface->surface->width,
		                           subsurface->surface->height);
	}

	box = pixman_region32_extents(&region);
	if (x)
		*x = box->x1;
	if (y)
		*y = box->y1;
	if (w)
		*w = box->x2 - box->x1;
	if (h)
		*h = box->y2 - box->y1;

	pixman_region32_fini(&region);
}







static void
set_busy_cursor(shell_surface *shsurf, struct weston_pointer *pointer)
{
	struct shell_grab *grab;

	if (pointer->grab->interface == &busy_cursor_grab_interface)
		return;

	grab = malloc(sizeof *grab);
	if (!grab)
		return;

	shell_grab_start(grab, &busy_cursor_grab_interface, shsurf, pointer,
			 DESKTOP_SHELL_CURSOR_BUSY);
	/* Mark the shsurf as ungrabbed so that button binding is able
	 * to move it. */
	shsurf->grabbed = 0;
}

static int
xdg_ping_timeout_handler(void *data)
{
	page::shell_client *sc = reinterpret_cast<page::shell_client*>(data);
	struct weston_seat *seat;
	shell_surface *shsurf;

	/* Client is not responding */
	sc->unresponsive = 1;
	wl_list_for_each(seat, &sc->shell->compositor->seat_list, link) {
		if (seat->pointer == NULL || seat->pointer->focus == NULL)
			continue;
		if (seat->pointer->focus->surface->resource == NULL)
			continue;
		
		shsurf = shell_surface::get_shell_surface(seat->pointer->focus->surface);
		if (shsurf &&
		    wl_resource_get_client(shsurf->resource) == sc->client)
			set_busy_cursor(shsurf, seat->pointer);
	}

	return 1;
}

static void
handle_xdg_ping(shell_surface *shsurf, uint32_t serial)
{
	struct weston_compositor *compositor = shsurf->shell->compositor;
	page::shell_client *sc = reinterpret_cast<page::shell_client*>(shsurf->owner);
	struct wl_event_loop *loop;
	static const int ping_timeout = 200;

	if (sc->unresponsive) {
		xdg_ping_timeout_handler(sc);
		return;
	}

	sc->ping_serial = serial;
	loop = wl_display_get_event_loop(compositor->wl_display);
	if (sc->ping_timer == NULL)
		sc->ping_timer =
			wl_event_loop_add_timer(loop,
						xdg_ping_timeout_handler, sc);
	if (sc->ping_timer == NULL)
		return;

	wl_event_source_timer_update(sc->ping_timer, ping_timeout);

	if (shsurf->shell_surface_is_xdg_surface() ||
	    shell_surface_is_xdg_popup(shsurf))
		xdg_shell_send_ping(sc->resource, serial);
	else if (shsurf->shell_surface_is_wl_shell_surface())
		wl_shell_surface_send_ping(shsurf->resource, serial);
}

static void
ping_handler(struct weston_surface *surface, uint32_t serial)
{
	shell_surface *shsurf = shell_surface::get_shell_surface(surface);

	if (!shsurf)
		return;
	if (!shsurf->resource)
		return;
	if (shsurf->surface == shsurf->shell->grab_surface)
		return;

	handle_xdg_ping(shsurf, serial);
}

static void
handle_pointer_focus(struct wl_listener *listener, void *data)
{
	struct weston_pointer *pointer = data;
	struct weston_view *view = pointer->focus;
	struct weston_compositor *compositor;
	uint32_t serial;

	if (!view)
		return;

	compositor = view->surface->compositor;
	serial = wl_display_next_serial(compositor->wl_display);
	ping_handler(view->surface, serial);
}

static void
shell_surface_lose_keyboard_focus(shell_surface *shsurf)
{
	if (--shsurf->focus_count == 0)
		shsurf->shell_surface_state_changed();
}

static void
shell_surface_gain_keyboard_focus(shell_surface *shsurf)
{
	if (shsurf->focus_count++ == 0)
		shsurf->shell_surface_state_changed();
}

static void
handle_keyboard_focus(struct wl_listener *listener, void *data)
{
	struct weston_keyboard *keyboard = data;
	page::shell_seat *seat = get_shell_seat(keyboard->seat);

	if (seat->focused_surface) {
		shell_surface *shsurf = shell_surface::get_shell_surface(seat->focused_surface);
		if (shsurf)
			shell_surface_lose_keyboard_focus(shsurf);
	}

	seat->focused_surface = keyboard->focus;

	if (seat->focused_surface) {
		shell_surface *shsurf = shell_surface::get_shell_surface(seat->focused_surface);
		if (shsurf)
			shell_surface_gain_keyboard_focus(shsurf);
	}
}


void
restore_output_mode(struct weston_output *output)
{
	if (output->original_mode ||
	    (int32_t)output->current_scale != output->original_scale)
		weston_output_switch_mode(output,
					  output->native_mode,
					  output->native_scale,
					  WESTON_MODE_SWITCH_RESTORE_NATIVE);
}

static void
restore_all_output_modes(struct weston_compositor *compositor)
{
	struct weston_output *output;

	wl_list_for_each(output, &compositor->output_list, link)
		restore_output_mode(output);
}





static void
black_surface_configure(struct weston_surface *es, int32_t sx, int32_t sy);

static struct weston_view *
create_black_surface(struct weston_compositor *ec,
		     struct weston_surface *fs_surface,
		     float x, float y, int w, int h)
{
	struct weston_surface *surface = NULL;
	struct weston_view *view;

	surface = weston_surface_create(ec);
	if (surface == NULL) {
		weston_log("no memory\n");
		return NULL;
	}
	view = weston_view_create(surface);
	if (surface == NULL) {
		weston_log("no memory\n");
		weston_surface_destroy(surface);
		return NULL;
	}

	surface->configure = black_surface_configure;
	surface->configure_private = fs_surface;
	weston_surface_set_color(surface, 0.0, 0.0, 0.0, 1);
	pixman_region32_fini(&surface->opaque);
	pixman_region32_init_rect(&surface->opaque, 0, 0, w, h);
	pixman_region32_fini(&surface->input);
	pixman_region32_init_rect(&surface->input, 0, 0, w, h);

	weston_surface_set_size(surface, w, h);
	weston_view_set_position(view, x, y);

	return view;
}

static void
shell_ensure_fullscreen_black_view(shell_surface *shsurf)
{
	struct weston_output *output = shsurf->fullscreen_output;

	assert(shsurf->state.fullscreen);

	if (!shsurf->fullscreen.black_view)
		shsurf->fullscreen.black_view =
			create_black_surface(shsurf->surface->compositor,
			                     shsurf->surface,
			                     output->x, output->y,
			                     output->width,
			                     output->height);

	weston_view_geometry_dirty(shsurf->fullscreen.black_view);
	weston_layer_entry_remove(&shsurf->fullscreen.black_view->layer_link);
	weston_layer_entry_insert(&shsurf->view->layer_link,
				  &shsurf->fullscreen.black_view->layer_link);
	weston_view_geometry_dirty(shsurf->fullscreen.black_view);
	weston_surface_damage(shsurf->surface);

	shsurf->state.lowered = false;
}

/* Create black surface and append it to the associated fullscreen surface.
 * Handle size dismatch and positioning according to the method. */
static void
shell_configure_fullscreen(shell_surface *shsurf)
{
	struct weston_output *output = shsurf->fullscreen_output;
	struct weston_surface *surface = shsurf->surface;
	struct weston_matrix *matrix;
	float scale, output_aspect, surface_aspect, x, y;
	int32_t surf_x, surf_y, surf_width, surf_height;

	if (shsurf->fullscreen.type != WL_SHELL_SURFACE_FULLSCREEN_METHOD_DRIVER)
		restore_output_mode(output);

	/* Reverse the effect of lower_fullscreen_layer() */
	weston_layer_entry_remove(&shsurf->view->layer_link);
	weston_layer_entry_insert(&shsurf->shell->fullscreen_layer.view_list, &shsurf->view->layer_link);

	shell_ensure_fullscreen_black_view(shsurf);

	surface_subsurfaces_boundingbox(shsurf->surface, &surf_x, &surf_y,
	                                &surf_width, &surf_height);

	switch (shsurf->fullscreen.type) {
	case WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT:
		if (surface->buffer_ref.buffer)
			center_on_output(shsurf->view, shsurf->fullscreen_output);
		break;
	case WL_SHELL_SURFACE_FULLSCREEN_METHOD_SCALE:
		/* 1:1 mapping between surface and output dimensions */
		if (output->width == surf_width &&
			output->height == surf_height) {
			weston_view_set_position(shsurf->view,
						 output->x - surf_x,
						 output->y - surf_y);
			break;
		}

		matrix = &shsurf->fullscreen.transform.matrix;
		weston_matrix_init(matrix);

		output_aspect = (float) output->width /
			(float) output->height;
		/* XXX: Use surf_width and surf_height here? */
		surface_aspect = (float) surface->width /
			(float) surface->height;
		if (output_aspect < surface_aspect)
			scale = (float) output->width /
				(float) surf_width;
		else
			scale = (float) output->height /
				(float) surf_height;

		weston_matrix_scale(matrix, scale, scale, 1);
		wl_list_remove(&shsurf->fullscreen.transform.link);
		wl_list_insert(&shsurf->view->geometry.transformation_list,
			       &shsurf->fullscreen.transform.link);
		x = output->x + (output->width - surf_width * scale) / 2 - surf_x;
		y = output->y + (output->height - surf_height * scale) / 2 - surf_y;
		weston_view_set_position(shsurf->view, x, y);

		break;
	case WL_SHELL_SURFACE_FULLSCREEN_METHOD_DRIVER:
		if (shsurf->shell_surface_is_top_fullscreen()) {
			struct weston_mode mode = {0,
				surf_width * surface->buffer_viewport.buffer.scale,
				surf_height * surface->buffer_viewport.buffer.scale,
				shsurf->fullscreen.framerate};

			if (weston_output_switch_mode(output, &mode, surface->buffer_viewport.buffer.scale,
					WESTON_MODE_SWITCH_SET_TEMPORARY) == 0) {
				weston_view_set_position(shsurf->view,
							 output->x - surf_x,
							 output->y - surf_y);
				shsurf->fullscreen.black_view->surface->width = output->width;
				shsurf->fullscreen.black_view->surface->height = output->height;
				weston_view_set_position(shsurf->fullscreen.black_view,
							 output->x - surf_x,
							 output->y - surf_y);
				break;
			} else {
				restore_output_mode(output);
				center_on_output(shsurf->view, output);
			}
		}
		break;
	case WL_SHELL_SURFACE_FULLSCREEN_METHOD_FILL:
		center_on_output(shsurf->view, output);
		break;
	default:
		break;
	}
}

static void
shell_map_fullscreen(shell_surface *shsurf)
{
	shell_configure_fullscreen(shsurf);
}



static void
destroy_shell_seat(struct wl_listener *listener, void *data)
{
	page::shell_seat *shseat =
		container_of(listener,
			     page::shell_seat, seat_destroy_listener);
	shell_surface *shsurf, *prev = NULL;

	if (shseat->popup_grab.grab.interface == &popup_grab_interface) {
		weston_pointer_end_grab(shseat->popup_grab.grab.pointer);
		shseat->popup_grab.client = NULL;

		wl_list_for_each(shsurf, &shseat->popup_grab.surfaces_list, popup.grab_link) {
			shsurf->popup.shseat = NULL;
			if (prev) {
				wl_list_init(&prev->popup.grab_link);
			}
			prev = shsurf;
		}
		wl_list_init(&prev->popup.grab_link);
	}

	wl_list_remove(&shseat->seat_destroy_listener.link);
	free(shseat);
}

static void
shell_seat_caps_changed(struct wl_listener *listener, void *data)
{
	page::shell_seat *seat;

	seat = container_of(listener, page::shell_seat, caps_changed_listener);

	if (seat->seat->keyboard &&
	    wl_list_empty(&seat->keyboard_focus_listener.link)) {
		wl_signal_add(&seat->seat->keyboard->focus_signal,
			      &seat->keyboard_focus_listener);
	} else if (!seat->seat->keyboard) {
		wl_list_init(&seat->keyboard_focus_listener.link);
	}

	if (seat->seat->pointer &&
	    wl_list_empty(&seat->pointer_focus_listener.link)) {
		wl_signal_add(&seat->seat->pointer->focus_signal,
			      &seat->pointer_focus_listener);
	} else if (!seat->seat->pointer) {
		wl_list_init(&seat->pointer_focus_listener.link);
	}
}

static page::shell_seat *
create_shell_seat(struct weston_seat *seat)
{
	page::shell_seat *shseat;

	shseat = calloc(1, sizeof *shseat);
	if (!shseat) {
		weston_log("no memory to allocate shell seat\n");
		return NULL;
	}

	shseat->seat = seat;
	wl_list_init(&shseat->popup_grab.surfaces_list);

	shseat->seat_destroy_listener.notify = destroy_shell_seat;
	wl_signal_add(&seat->destroy_signal,
	              &shseat->seat_destroy_listener);

	shseat->keyboard_focus_listener.notify = handle_keyboard_focus;
	wl_list_init(&shseat->keyboard_focus_listener.link);

	shseat->pointer_focus_listener.notify = handle_pointer_focus;
	wl_list_init(&shseat->pointer_focus_listener.link);

	shseat->caps_changed_listener.notify = shell_seat_caps_changed;
	wl_signal_add(&seat->updated_caps_signal,
		      &shseat->caps_changed_listener);
	shell_seat_caps_changed(&shseat->caps_changed_listener, NULL);

	return shseat;
}

page::shell_seat *
get_shell_seat(weston_seat *seat)
{
	struct wl_listener *listener;

	listener = wl_signal_get(&seat->destroy_signal, destroy_shell_seat);
	assert(listener != NULL);

	return container_of(listener,
			    page::shell_seat, seat_destroy_listener);
}


static void
touch_popup_grab_down(struct weston_touch_grab *grab, uint32_t time,
		      int touch_id, wl_fixed_t sx, wl_fixed_t sy)
{
	struct wl_resource *resource;
	page::shell_seat *shseat =
	    container_of(grab, page::shell_seat, popup_grab.touch_grab);
	struct wl_display *display = shseat->seat->compositor->wl_display;
	uint32_t serial;
	struct wl_list *resource_list;

	resource_list = &grab->touch->focus_resource_list;
	if (!wl_list_empty(resource_list)) {
		serial = wl_display_get_serial(display);
		wl_resource_for_each(resource, resource_list) {
			wl_touch_send_down(resource, serial, time,
				grab->touch->focus->surface->resource,
				touch_id, sx, sy);
		}
	}
}

static void
touch_popup_grab_up(struct weston_touch_grab *grab, uint32_t time, int touch_id)
{
	struct wl_resource *resource;
	page::shell_seat *shseat =
	    container_of(grab, page::shell_seat, popup_grab.touch_grab);
	struct wl_display *display = shseat->seat->compositor->wl_display;
	uint32_t serial;
	struct wl_list *resource_list;

	resource_list = &grab->touch->focus_resource_list;
	if (!wl_list_empty(resource_list)) {
		serial = wl_display_get_serial(display);
		wl_resource_for_each(resource, resource_list) {
			wl_touch_send_up(resource, serial, time, touch_id);
		}
	}
}

static void
touch_popup_grab_motion(struct weston_touch_grab *grab, uint32_t time,
			int touch_id, wl_fixed_t sx, wl_fixed_t sy)
{
	struct wl_resource *resource;
	struct wl_list *resource_list;

	resource_list = &grab->touch->focus_resource_list;
	if (!wl_list_empty(resource_list)) {
		wl_resource_for_each(resource, resource_list) {
			wl_touch_send_motion(resource, time, touch_id, sx, sy);
		}
	}
}

static void
touch_popup_grab_frame(struct weston_touch_grab *grab)
{
}

static void
touch_popup_grab_cancel(struct weston_touch_grab *grab)
{
	touch_popup_grab_end(grab->touch);
}

static const struct weston_touch_grab_interface touch_popup_grab_interface = {
	touch_popup_grab_down,
	touch_popup_grab_up,
	touch_popup_grab_motion,
	touch_popup_grab_frame,
	touch_popup_grab_cancel,
};

static void
shell_surface_send_popup_done(shell_surface *shsurf)
{
	if (shsurf->shell_surface_is_wl_shell_surface())
		wl_shell_surface_send_popup_done(shsurf->resource);
	else if (shell_surface_is_xdg_popup(shsurf))
		xdg_popup_send_popup_done(shsurf->resource,
					  shsurf->popup.serial);
}

static void
popup_grab_end(struct weston_pointer *pointer)
{
	struct weston_pointer_grab *grab = pointer->grab;
	page::shell_seat *shseat =
	    container_of(grab, page::shell_seat, popup_grab.grab);
	shell_surface *shsurf;
	shell_surface *prev = NULL;

	if (pointer->grab->interface == &popup_grab_interface) {
		weston_pointer_end_grab(grab->pointer);
		shseat->popup_grab.client = NULL;
		shseat->popup_grab.grab.interface = NULL;
		assert(!wl_list_empty(&shseat->popup_grab.surfaces_list));
		/* Send the popup_done event to all the popups open */
		wl_list_for_each(shsurf, &shseat->popup_grab.surfaces_list, popup.grab_link) {
			shell_surface_send_popup_done(shsurf);
			shsurf->popup.shseat = NULL;
			if (prev) {
				wl_list_init(&prev->popup.grab_link);
			}
			prev = shsurf;
		}
		wl_list_init(&prev->popup.grab_link);
		wl_list_init(&shseat->popup_grab.surfaces_list);
	}
}

static void
touch_popup_grab_end(struct weston_touch *touch)
{
	struct weston_touch_grab *grab = touch->grab;
	page::shell_seat *shseat =
	    container_of(grab, page::shell_seat, popup_grab.touch_grab);
	shell_surface *shsurf;
	shell_surface *prev = NULL;

	if (touch->grab->interface == &touch_popup_grab_interface) {
		weston_touch_end_grab(grab->touch);
		shseat->popup_grab.client = NULL;
		shseat->popup_grab.touch_grab.interface = NULL;
		assert(!wl_list_empty(&shseat->popup_grab.surfaces_list));
		/* Send the popup_done event to all the popups open */
		wl_list_for_each(shsurf, &shseat->popup_grab.surfaces_list, popup.grab_link) {
			shell_surface_send_popup_done(shsurf);
			shsurf->popup.shseat = NULL;
			if (prev) {
				wl_list_init(&prev->popup.grab_link);
			}
			prev = shsurf;
		}
		wl_list_init(&prev->popup.grab_link);
		wl_list_init(&shseat->popup_grab.surfaces_list);
	}
}

static void
add_popup_grab(shell_surface *shsurf, page::shell_seat *shseat, int32_t type)
{
	struct weston_seat *seat = shseat->seat;

	if (wl_list_empty(&shseat->popup_grab.surfaces_list)) {
		shseat->popup_grab.type = type;
		shseat->popup_grab.client = wl_resource_get_client(shsurf->resource);

		if (type == page::shell_seat::POINTER) {
			shseat->popup_grab.grab.interface = &popup_grab_interface;
			/* We must make sure here that this popup was opened after
			 * a mouse press, and not just by moving around with other
			 * popups already open. */
			if (shseat->seat->pointer->button_count > 0)
				shseat->popup_grab.initial_up = 0;
		} else if (type == page::shell_seat::TOUCH) {
			shseat->popup_grab.touch_grab.interface = &touch_popup_grab_interface;
		}

		wl_list_insert(&shseat->popup_grab.surfaces_list, &shsurf->popup.grab_link);

		if (type == page::shell_seat::POINTER)
			weston_pointer_start_grab(seat->pointer, &shseat->popup_grab.grab);
		else if (type == page::shell_seat::TOUCH)
			weston_touch_start_grab(seat->touch, &shseat->popup_grab.touch_grab);
	} else {
		wl_list_insert(&shseat->popup_grab.surfaces_list, &shsurf->popup.grab_link);
	}
}

static void
remove_popup_grab(shell_surface *shsurf)
{
	page::shell_seat *shseat = shsurf->popup.shseat;

	wl_list_remove(&shsurf->popup.grab_link);
	wl_list_init(&shsurf->popup.grab_link);
	if (wl_list_empty(&shseat->popup_grab.surfaces_list)) {
		if (shseat->popup_grab.type == page::shell_seat::POINTER) {
			weston_pointer_end_grab(shseat->popup_grab.grab.pointer);
			shseat->popup_grab.grab.interface = NULL;
		} else if (shseat->popup_grab.type == page::shell_seat::TOUCH) {
			weston_touch_end_grab(shseat->popup_grab.touch_grab.touch);
			shseat->popup_grab.touch_grab.interface = NULL;
		}
	}
}

static void
shell_map_popup(shell_surface *shsurf)
{
	page::shell_seat *shseat = shsurf->popup.shseat;
	struct weston_view *parent_view = get_default_view(shsurf->parent);

	shsurf->surface->output = parent_view->output;
	shsurf->view->output = parent_view->output;

	weston_view_set_transform_parent(shsurf->view, parent_view);
	weston_view_set_position(shsurf->view, shsurf->popup.x, shsurf->popup.y);
	weston_view_update_transform(shsurf->view);

	if (shseat->seat->pointer &&
	    shseat->seat->pointer->grab_serial == shsurf->popup.serial) {
		add_popup_grab(shsurf, shseat, page::shell_seat::POINTER);
	} else if (shseat->seat->touch &&
	           shseat->seat->touch->grab_serial == shsurf->popup.serial) {
		add_popup_grab(shsurf, shseat, page::shell_seat::TOUCH);
	} else {
		shell_surface_send_popup_done(shsurf);
		shseat->popup_grab.client = NULL;
	}
}




void
fade_out_done(struct weston_view_animation *animation, void *data)
{
	shell_surface *shsurf = data;

	weston_surface_destroy(shsurf->surface);
}



static void
shell_surface_configure(struct weston_surface *, int32_t, int32_t);





/****************************
 * xdg-shell implementation */




void
desktop_shell_set_background(struct wl_client *client,
			     struct wl_resource *resource,
			     struct wl_resource *output_resource,
			     struct wl_resource *surface_resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct weston_view *view, *next;

	if (surface->configure) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface role already assigned");
		return;
	}

	wl_list_for_each_safe(view, next, &surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_view_create(surface);

	surface->configure = background_configure;
	surface->configure_private = shell;
	surface->output = wl_resource_get_user_data(output_resource);
	view->output = surface->output;
	desktop_shell_send_configure(resource, 0,
				     surface_resource,
				     surface->output->width,
				     surface->output->height);
}

void
desktop_shell_set_panel(struct wl_client *client,
			struct wl_resource *resource,
			struct wl_resource *output_resource,
			struct wl_resource *surface_resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);
	struct weston_view *view, *next;

	if (surface->configure) {
		wl_resource_post_error(surface_resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "surface role already assigned");
		return;
	}

	wl_list_for_each_safe(view, next, &surface->views, surface_link)
		weston_view_destroy(view);
	view = weston_view_create(surface);

	surface->configure = panel_configure;
	surface->configure_private = shell;
	surface->output = wl_resource_get_user_data(output_resource);
	view->output = surface->output;
	desktop_shell_send_configure(resource, 0,
				     surface_resource,
				     surface->output->width,
				     surface->output->height);
}





static bool
shell_surface_is_xdg_surface(shell_surface *shsurf)
{
	return shsurf->resource &&
		wl_resource_instance_of(shsurf->resource,
					&xdg_surface_interface,
					&page::xdg_surface_implementation);
}

/* xdg-popup implementation */

static bool
shell_surface_is_xdg_popup(shell_surface *shsurf)
{
	return wl_resource_instance_of(shsurf->resource,
				       &xdg_popup_interface,
				       &xdg_popup_implementation);
}

static int
xdg_shell_unversioned_dispatch(const void *implementation,
			       void *_target, uint32_t opcode,
			       const struct wl_message *message,
			       union wl_argument *args)
{
	struct wl_resource *resource = _target;
	struct shell_client *sc = wl_resource_get_user_data(resource);

	if (opcode != 0) {
		wl_resource_post_error(resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "must call use_unstable_version first");
		return 0;
	}

#define XDG_SERVER_VERSION 4

	static_assert(XDG_SERVER_VERSION == XDG_SHELL_VERSION_CURRENT,
		      "shell implementation doesn't match protocol version");

	if (args[0].i != XDG_SERVER_VERSION) {
		wl_resource_post_error(resource,
				       WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "incompatible version, server is %d "
				       "client wants %d",
				       XDG_SERVER_VERSION, args[0].i);
		return 0;
	}

	wl_resource_set_implementation(resource, &xdg_implementation,
				       sc, NULL);

	return 1;
}

/* end of xdg-shell implementation */
/***********************************/

static int
screensaver_timeout(void *data)
{
	struct desktop_shell *shell = data;

	shell->shell_fade(FADE_OUT);

	return 1;
}


static void
configure_static_view(struct weston_view *ev, struct weston_layer *layer)
{
	struct weston_view *v, *next;

	wl_list_for_each_safe(v, next, &layer->view_list.link, layer_link.link) {
		if (v->output == ev->output && v != ev) {
			weston_view_unmap(v);
			v->surface->configure = NULL;
		}
	}

	weston_view_set_position(ev, ev->output->x, ev->output->y);

	if (wl_list_empty(&ev->layer_link.link)) {
		weston_layer_entry_insert(&layer->view_list, &ev->layer_link);
		weston_compositor_schedule_repaint(ev->surface->compositor);
	}
}

static void
background_configure(struct weston_surface *es, int32_t sx, int32_t sy)
{
	struct desktop_shell *shell = es->configure_private;
	struct weston_view *view;

	view = container_of(es->views.next, struct weston_view, surface_link);

	configure_static_view(view, &shell->background_layer);
}

static void
panel_configure(struct weston_surface *es, int32_t sx, int32_t sy)
{
	struct desktop_shell *shell = es->configure_private;
	struct weston_view *view;

	view = container_of(es->views.next, struct weston_view, surface_link);

	configure_static_view(view, &shell->panel_layer);
}



static void
lock_surface_configure(struct weston_surface *surface, int32_t sx, int32_t sy)
{
	struct desktop_shell *shell = surface->configure_private;
	struct weston_view *view;

	view = container_of(surface->views.next, struct weston_view, surface_link);

	if (surface->width == 0)
		return;

	center_on_output(view, get_default_output(shell->compositor));

	if (!weston_surface_is_mapped(surface)) {
		weston_layer_entry_insert(&shell->lock_layer.view_list,
					  &view->layer_link);
		weston_view_update_transform(view);
		shell->shell_fade(FADE_IN);
	}
}

static void
handle_lock_surface_destroy(struct wl_listener *listener, void *data)
{
	struct desktop_shell *shell =
	    container_of(listener, struct desktop_shell, lock_surface_listener);

	weston_log("lock surface gone\n");
	shell->lock_surface = NULL;
}

static void
desktop_shell_set_lock_surface(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *surface_resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);
	struct weston_surface *surface =
		wl_resource_get_user_data(surface_resource);

	shell->prepare_event_sent = false;

	if (!shell->locked)
		return;

	shell->lock_surface = surface;

	shell->lock_surface_listener.notify = handle_lock_surface_destroy;
	wl_signal_add(&surface->destroy_signal,
		      &shell->lock_surface_listener);

	weston_view_create(surface);
	surface->configure = lock_surface_configure;
	surface->configure_private = shell;
}

static void
desktop_shell_unlock(struct wl_client *client,
		     struct wl_resource *resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	shell->prepare_event_sent = false;

	if (shell->locked)
		shell->resume_desktop();
}

static void
desktop_shell_set_grab_surface(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *surface_resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	shell->grab_surface = wl_resource_get_user_data(surface_resource);
	weston_view_create(shell->grab_surface);
}

static void
desktop_shell_desktop_ready(struct wl_client *client,
			    struct wl_resource *resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	shell_fade_startup(shell);
}

static void
desktop_shell_set_panel_position(struct wl_client *client,
				 struct wl_resource *resource,
				 uint32_t position)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	if (position != DESKTOP_SHELL_PANEL_POSITION_TOP &&
	    position != DESKTOP_SHELL_PANEL_POSITION_BOTTOM &&
	    position != DESKTOP_SHELL_PANEL_POSITION_LEFT &&
	    position != DESKTOP_SHELL_PANEL_POSITION_RIGHT) {
		wl_resource_post_error(resource,
				       DESKTOP_SHELL_ERROR_INVALID_ARGUMENT,
				       "bad position argument");
		return;
	}

	shell->panel_position = position;
}

static const struct desktop_shell_interface desktop_shell_implementation = {
	desktop_shell_set_background,
	desktop_shell_set_panel,
	desktop_shell_set_lock_surface,
	desktop_shell_unlock,
	desktop_shell_set_grab_surface,
	desktop_shell_desktop_ready,
	desktop_shell_set_panel_position
};

static shell_surface_type
get_shell_surface_type(struct weston_surface *surface)
{
	shell_surface *shsurf;

	shsurf = shell_surface::get_shell_surface(surface);
	if (!shsurf)
		return SHELL_SURFACE_NONE;
	return shsurf->type;
}

static void
move_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data)
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
maximize_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data)
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

	if (!shell_surface_is_xdg_surface(shsurf))
		return;

	shsurf->state_requested = true;
	shsurf->requested_state.maximized = !shsurf->state.maximized;
	shsurf->send_configure_for_surface();
}

static void
fullscreen_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data)
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

	if (!shell_surface_is_xdg_surface(shsurf))
		return;

	shsurf->state_requested = true;
	shsurf->requested_state.fullscreen = !shsurf->state.fullscreen;
	shsurf->fullscreen_output = shsurf->output;
	shsurf->send_configure_for_surface();
}

static void
touch_move_binding(struct weston_seat *seat, uint32_t time, void *data)
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
resize_binding(struct weston_seat *seat, uint32_t time, uint32_t button, void *data)
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

static void
surface_opacity_binding(struct weston_seat *seat, uint32_t time, uint32_t axis,
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
zoom_axis_binding(struct weston_seat *seat, uint32_t time, uint32_t axis,
		  wl_fixed_t value, void *data)
{
	do_zoom(seat, time, 0, axis, value);
}

static void
zoom_key_binding(struct weston_seat *seat, uint32_t time, uint32_t key,
		 void *data)
{
	do_zoom(seat, time, key, 0, 0);
}

static void
terminate_binding(struct weston_seat *seat, uint32_t time, uint32_t key,
		  void *data)
{
	struct weston_compositor *compositor = data;

	wl_display_terminate(compositor->wl_display);
}

static void
rotate_binding(struct weston_seat *seat, uint32_t time, uint32_t button,
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

/* Move all fullscreen layers down to the current workspace and hide their
 * black views. The surfaces' state is set to both fullscreen and lowered,
 * and this is reversed when such a surface is re-configured, see
 * shell_configure_fullscreen() and shell_ensure_fullscreen_black_view().
 *
 * This should be used when implementing shell-wide overlays, such as
 * the alt-tab switcher, which need to de-promote fullscreen layers. */
void
lower_fullscreen_layer(struct desktop_shell *shell)
{
	struct workspace *ws;
	struct weston_view *view, *prev;

	ws = get_current_workspace(shell);
	wl_list_for_each_reverse_safe(view, prev,
				      &shell->fullscreen_layer.view_list.link,
				      layer_link.link) {
		shell_surface *shsurf = shell_surface::get_shell_surface(view->surface);

		if (!shsurf)
			continue;

		/* We can have a non-fullscreen popup for a fullscreen surface
		 * in the fullscreen layer. */
		if (shsurf->state.fullscreen) {
			/* Hide the black view */
			weston_layer_entry_remove(&shsurf->fullscreen.black_view->layer_link);
			wl_list_init(&shsurf->fullscreen.black_view->layer_link.link);
			weston_view_damage_below(shsurf->fullscreen.black_view);

		}

		/* Lower the view to the workspace layer */
		weston_layer_entry_remove(&view->layer_link);
		weston_layer_entry_insert(&ws->layer.view_list, &view->layer_link);
		weston_view_damage_below(view);
		weston_surface_damage(view->surface);

		shsurf->state.lowered = true;
	}
}

void
activate(struct desktop_shell *shell, struct weston_surface *es,
	 struct weston_seat *seat, bool configure)
{
	struct weston_surface *main_surface;
	struct focus_state *state;
	struct workspace *ws;
	struct weston_surface *old_es;
	shell_surface *shsurf;

	lower_fullscreen_layer(shell);

	main_surface = weston_surface_get_main_surface(es);

	weston_surface_activate(es, seat);

	state = ensure_focus_state(shell, seat);
	if (state == NULL)
		return;

	old_es = state->keyboard_focus;
	state->focus_state_set_focus(es);

	shsurf = shell_surface::get_shell_surface(main_surface);
	assert(shsurf);

	if (shsurf->state.fullscreen && configure)
		shell_configure_fullscreen(shsurf);
	else
		restore_all_output_modes(shell->compositor);

	/* Update the surface’s layer. This brings it to the top of the stacking
	 * order as appropriate. */
	shsurf->shell_surface_update_layer();

	if (shell->focus_animation_type != ANIMATION_NONE) {
		ws = get_current_workspace(shell);
		animate_focus_change(shell, ws, get_default_view(old_es), get_default_view(es));
	}
}

/* no-op func for checking black surface */
static void
black_surface_configure(struct weston_surface *es, int32_t sx, int32_t sy)
{
}

static bool
is_black_surface (struct weston_surface *es, struct weston_surface **fs_surface)
{
	if (es->configure == black_surface_configure) {
		if (fs_surface)
			*fs_surface = (struct weston_surface *)es->configure_private;
		return true;
	}
	return false;
}

static void
activate_binding(struct weston_seat *seat,
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

static void
click_to_activate_binding(struct weston_seat *seat, uint32_t time, uint32_t button,
			  void *data)
{
	if (seat->pointer->grab != &seat->pointer->default_grab)
		return;
	if (seat->pointer->focus == NULL)
		return;

	activate_binding(seat, data, seat->pointer->focus->surface);
}

static void
touch_to_activate_binding(struct weston_seat *seat, uint32_t time, void *data)
{
	if (seat->touch->grab != &seat->touch->default_grab)
		return;
	if (seat->touch->focus == NULL)
		return;

	activate_binding(seat, data, seat->touch->focus->surface);
}

static void
do_shell_fade_startup(void *data)
{
	struct desktop_shell *shell = data;

	if (shell->startup_animation_type == ANIMATION_FADE)
		shell->shell_fade(FADE_IN);
	else if (shell->startup_animation_type == ANIMATION_NONE) {
		weston_surface_destroy(shell->fade.view->surface);
		shell->fade.view = NULL;
	}
}

static void
shell_fade_startup(struct desktop_shell *shell)
{
	struct wl_event_loop *loop;

	if (!shell->fade.startup_timer)
		return;

	wl_event_source_remove(shell->fade.startup_timer);
	shell->fade.startup_timer = NULL;

	loop = wl_display_get_event_loop(shell->compositor->wl_display);
	wl_event_loop_add_idle(loop, do_shell_fade_startup, shell);
}

static int
fade_startup_timeout(void *data)
{
	struct desktop_shell *shell = data;

	shell_fade_startup(shell);
	return 0;
}

static void
shell_fade_init(struct desktop_shell *shell)
{
	/* Make compositor output all black, and wait for the desktop-shell
	 * client to signal it is ready, then fade in. The timer triggers a
	 * fade-in, in case the desktop-shell client takes too long.
	 */

	struct wl_event_loop *loop;

	if (shell->fade.view != NULL) {
		weston_log("%s: warning: fade surface already exists\n",
			   __func__);
		return;
	}

	shell->fade.view = shell->shell_fade_create_surface();
	if (!shell->fade.view)
		return;

	weston_view_update_transform(shell->fade.view);
	weston_surface_damage(shell->fade.view->surface);

	loop = wl_display_get_event_loop(shell->compositor->wl_display);
	shell->fade.startup_timer =
		wl_event_loop_add_timer(loop, fade_startup_timeout, shell);
	wl_event_source_timer_update(shell->fade.startup_timer, 15000);
}

static void
idle_handler(struct wl_listener *listener, void *data)
{
	struct desktop_shell *shell =
		container_of(listener, struct desktop_shell, idle_listener);
	struct weston_seat *seat;

	wl_list_for_each(seat, &shell->compositor->seat_list, link) {
		if (seat->pointer)
			popup_grab_end(seat->pointer);
		if (seat->touch)
			touch_popup_grab_end(seat->touch);
	}

	shell->shell_fade(FADE_OUT);
	/* lock() is called from shell_fade_done() */
}

static void
wake_handler(struct wl_listener *listener, void *data)
{
	struct desktop_shell *shell =
		container_of(listener, struct desktop_shell, wake_listener);

	shell->unlock();
}

void
center_on_output(struct weston_view *view, struct weston_output *output)
{
	int32_t surf_x, surf_y, width, height;
	float x, y;

	surface_subsurfaces_boundingbox(view->surface, &surf_x, &surf_y, &width, &height);

	x = output->x + (output->width - width) / 2 - surf_x / 2;
	y = output->y + (output->height - height) / 2 - surf_y / 2;

	weston_view_set_position(view, x, y);
}

void
weston_view_set_initial_position(struct weston_view *view,
				 struct desktop_shell *shell)
{
	struct weston_compositor *compositor = shell->compositor;
	int ix = 0, iy = 0;
	int32_t range_x, range_y;
	int32_t dx, dy, x, y;
	struct weston_output *output, *target_output = NULL;
	struct weston_seat *seat;
	pixman_rectangle32_t area;

	/* As a heuristic place the new window on the same output as the
	 * pointer. Falling back to the output containing 0, 0.
	 *
	 * TODO: Do something clever for touch too?
	 */
	wl_list_for_each(seat, &compositor->seat_list, link) {
		if (seat->pointer) {
			ix = wl_fixed_to_int(seat->pointer->x);
			iy = wl_fixed_to_int(seat->pointer->y);
			break;
		}
	}

	wl_list_for_each(output, &compositor->output_list, link) {
		if (pixman_region32_contains_point(&output->region, ix, iy, NULL)) {
			target_output = output;
			break;
		}
	}

	if (!target_output) {
		weston_view_set_position(view, 10 + random() % 400,
					 10 + random() % 400);
		return;
	}

	/* Valid range within output where the surface will still be onscreen.
	 * If this is negative it means that the surface is bigger than
	 * output.
	 */
	shell->get_output_work_area(target_output, &area);

	dx = area.x;
	dy = area.y;
	range_x = area.width - view->surface->width;
	range_y = area.height - view->surface->height;

	if (range_x > 0)
		dx += random() % range_x;

	if (range_y > 0)
		dy += random() % range_y;

	x = target_output->x + dx;
	y = target_output->y + dy;

	weston_view_set_position(view, x, y);
}

static void
set_maximized_position(struct desktop_shell *shell,
		       shell_surface *shsurf)
{
	int32_t surf_x, surf_y;
	pixman_rectangle32_t area;
	pixman_box32_t *e;

	shell->get_output_work_area(shsurf->output, &area);
	surface_subsurfaces_boundingbox(shsurf->surface,
					&surf_x, &surf_y, NULL, NULL);
	e = pixman_region32_extents(&shsurf->output->region);

	weston_view_set_position(shsurf->view,
				 e->x1 + area.x - surf_x,
				 e->y1 + area.y - surf_y);
}

void
map(struct desktop_shell *shell, shell_surface *shsurf,
    int32_t sx, int32_t sy)
{
	struct weston_compositor *compositor = shell->compositor;
	struct weston_seat *seat;

	/* initial positioning, see also configure() */
	switch (shsurf->type) {
	case SHELL_SURFACE_TOPLEVEL:
		if (shsurf->state.fullscreen) {
			center_on_output(shsurf->view, shsurf->fullscreen_output);
			shell_map_fullscreen(shsurf);
		} else if (shsurf->state.maximized) {
			set_maximized_position(shell, shsurf);
		} else if (!shsurf->state.relative) {

			weston_matrix_init(&(shsurf->rotation.transform.matrix));
			weston_matrix_scale(&(shsurf->rotation.transform.matrix),0.5,0.5,0.5);
			wl_list_insert(&shsurf->view->geometry.transformation_list, &(shsurf->rotation.transform.link));

			weston_view_set_initial_position(shsurf->view, shell);
		}
		break;
	case SHELL_SURFACE_POPUP:
		shell_map_popup(shsurf);
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
		if (shell->locked)
			break;
		wl_list_for_each(seat, &compositor->seat_list, link)
			activate(shell, shsurf->surface, seat, true);
		break;
	case SHELL_SURFACE_POPUP:
	case SHELL_SURFACE_NONE:
	default:
		break;
	}

	if (shsurf->type == SHELL_SURFACE_TOPLEVEL &&
	    !shsurf->state.maximized && !shsurf->state.fullscreen)
	{
		switch (shell->win_animation_type) {
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

void
configure(struct desktop_shell *shell, struct weston_surface *surface,
	  float x, float y)
{
	shell_surface *shsurf;
	struct weston_view *view;

	shsurf = shell_surface::get_shell_surface(surface);

	assert(shsurf);

	if (shsurf->state.fullscreen)
		shell_configure_fullscreen(shsurf);
	else if (shsurf->state.maximized) {
		set_maximized_position(shell, shsurf);
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



static bool
check_desktop_shell_crash_too_early(struct desktop_shell *shell)
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
	if (now.tv_sec - shell->startup_time.tv_sec < 30) {
		weston_log("Error: %s apparently cannot run at all.\n",
			   shell->client);
		weston_log_continue(STAMP_SPACE "Quitting...");
		wl_display_terminate(shell->compositor->wl_display);

		return true;
	}

	return false;
}

static void launch_desktop_shell_process(void *data);

static void
respawn_desktop_shell_process(struct desktop_shell *shell)
{
	uint32_t time;

	/* if desktop-shell dies more than 5 times in 30 seconds, give up */
	time = weston_compositor_get_time();
	if (time - shell->child.deathstamp > 30000) {
		shell->child.deathstamp = time;
		shell->child.deathcount = 0;
	}

	shell->child.deathcount++;
	if (shell->child.deathcount > 5) {
		weston_log("%s disconnected, giving up.\n", shell->client);
		return;
	}

	weston_log("%s disconnected, respawning...\n", shell->client);
	launch_desktop_shell_process(shell);
}

static void
desktop_shell_client_destroy(struct wl_listener *listener, void *data)
{
	struct desktop_shell *shell;

	shell = container_of(listener, struct desktop_shell,
			     child.client_destroy_listener);

	wl_list_remove(&shell->child.client_destroy_listener.link);
	shell->child.client = NULL;
	/*
	 * unbind_desktop_shell() will reset shell->child.desktop_shell
	 * before the respawned process has a chance to create a new
	 * desktop_shell object, because we are being called from the
	 * wl_client destructor which destroys all wl_resources before
	 * returning.
	 */

	if (!check_desktop_shell_crash_too_early(shell))
		respawn_desktop_shell_process(shell);

	shell_fade_startup(shell);
}

static void
launch_desktop_shell_process(void *data)
{
	struct desktop_shell *shell = data;

	shell->child.client = weston_client_start(shell->compositor,
						  shell->client);

	if (!shell->child.client) {
		weston_log("not able to start %s\n", shell->client);
		return;
	}

	shell->child.client_destroy_listener.notify =
		desktop_shell_client_destroy;
	wl_client_add_destroy_listener(shell->child.client,
				       &shell->child.client_destroy_listener);
}

/**
 * This function is called when a client request to use shell-interface.
 *
 * Client do this by bind the corresponding wl_global
 **/
static void
bind_shell(wl_client *client, desktop_shell * shell, uint32_t version, uint32_t id)
{
	/**
	 * reate a new object, this object will be handled by wl_resource
	 * And it will be destroyed on destroy of this resources
	 **/
	new page::shell_client{client, shell, page::shell_client::API_SHELL, id};
}

static void
bind_xdg_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct desktop_shell *shell = data;

	auto sc = new page::shell_client(client, shell, page::shell_client::API_XDG, id);
	if (sc)
		wl_resource_set_dispatcher(sc->resource,
					   xdg_shell_unversioned_dispatch,
					   NULL, sc, NULL);
}

static void
unbind_desktop_shell(struct wl_resource *resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	if (shell->locked)
		shell->resume_desktop();

	shell->child.desktop_shell = NULL;
	shell->prepare_event_sent = false;
}

static void
bind_desktop_shell(struct wl_client *client,
		   void *data, uint32_t version, uint32_t id)
{
	struct desktop_shell *shell = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &desktop_shell_interface,
				      MIN(version, 3), id);

	if (client == shell->child.client) {
		wl_resource_set_implementation(resource,
					       &page::desktop_shell_implementation,
					       shell, unbind_desktop_shell);
		shell->child.desktop_shell = resource;

		if (version < 2)
			shell_fade_startup(shell);

		return;
	}

	wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
			       "permission to bind desktop_shell denied");
}





static void
unbind_screensaver(struct wl_resource *resource)
{
	struct desktop_shell *shell = wl_resource_get_user_data(resource);

	shell->screensaver.binding = NULL;
}

static void
bind_screensaver(struct wl_client *client,
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

struct switcher {
	struct desktop_shell *shell;
	struct weston_surface *current;
	struct wl_listener listener;
	struct weston_keyboard_grab grab;
	struct wl_array minimized_array;
};

static void
switcher_next(struct switcher *switcher)
{
	struct weston_view *view;
	struct weston_surface *first = NULL, *prev = NULL, *next = NULL;
	shell_surface *shsurf;
	struct workspace *ws = get_current_workspace(switcher->shell);

	 /* temporary re-display minimized surfaces */
	struct weston_view *tmp;
	struct weston_view **minimized;
	wl_list_for_each_safe(view, tmp, &switcher->shell->minimized_layer.view_list.link, layer_link.link) {
		weston_layer_entry_remove(&view->layer_link);
		weston_layer_entry_insert(&ws->layer.view_list, &view->layer_link);
		minimized = wl_array_add(&switcher->minimized_array, sizeof *minimized);
		*minimized = view;
	}

	wl_list_for_each(view, &ws->layer.view_list.link, layer_link.link) {
		shsurf = shell_surface::get_shell_surface(view->surface);
		if (shsurf &&
		    shsurf->type == SHELL_SURFACE_TOPLEVEL &&
		    shsurf->parent == NULL) {
			if (first == NULL)
				first = view->surface;
			if (prev == switcher->current)
				next = view->surface;
			prev = view->surface;
			view->alpha = 0.25;
			weston_view_geometry_dirty(view);
			weston_surface_damage(view->surface);
		}

		if (is_black_surface(view->surface, NULL)) {
			view->alpha = 0.25;
			weston_view_geometry_dirty(view);
			weston_surface_damage(view->surface);
		}
	}

	if (next == NULL)
		next = first;

	if (next == NULL)
		return;

	wl_list_remove(&switcher->listener.link);
	wl_signal_add(&next->destroy_signal, &switcher->listener);

	switcher->current = next;
	wl_list_for_each(view, &next->views, surface_link)
		view->alpha = 1.0;

	shsurf = shell_surface::get_shell_surface(switcher->current);
	if (shsurf && shsurf->state.fullscreen)
		shsurf->fullscreen.black_view->alpha = 1.0;
}

static void
switcher_handle_surface_destroy(struct wl_listener *listener, void *data)
{
	struct switcher *switcher =
		container_of(listener, struct switcher, listener);

	switcher_next(switcher);
}

static void
switcher_destroy(struct switcher *switcher)
{
	struct weston_view *view;
	struct weston_keyboard *keyboard = switcher->grab.keyboard;
	struct workspace *ws = get_current_workspace(switcher->shell);

	wl_list_for_each(view, &ws->layer.view_list.link, layer_link.link) {
		if (is_focus_view(view))
			continue;

		view->alpha = 1.0;
		weston_surface_damage(view->surface);
	}

	if (switcher->current)
		activate(switcher->shell, switcher->current,
			 (struct weston_seat *) keyboard->seat, true);
	wl_list_remove(&switcher->listener.link);
	weston_keyboard_end_grab(keyboard);
	if (keyboard->input_method_resource)
		keyboard->grab = &keyboard->input_method_grab;

	 /* re-hide surfaces that were temporary shown during the switch */
	struct weston_view **minimized;
	wl_array_for_each(minimized, &switcher->minimized_array) {
		/* with the exception of the current selected */
		if ((*minimized)->surface != switcher->current) {
			weston_layer_entry_remove(&(*minimized)->layer_link);
			weston_layer_entry_insert(&switcher->shell->minimized_layer.view_list, &(*minimized)->layer_link);
			weston_view_damage_below(*minimized);
		}
	}
	wl_array_release(&switcher->minimized_array);

	free(switcher);
}

static void
switcher_key(struct weston_keyboard_grab *grab,
	     uint32_t time, uint32_t key, uint32_t state_w)
{
	struct switcher *switcher = container_of(grab, struct switcher, grab);
	enum wl_keyboard_key_state state = state_w;

	if (key == KEY_TAB && state == WL_KEYBOARD_KEY_STATE_PRESSED)
		switcher_next(switcher);
}

static void
switcher_modifier(struct weston_keyboard_grab *grab, uint32_t serial,
		  uint32_t mods_depressed, uint32_t mods_latched,
		  uint32_t mods_locked, uint32_t group)
{
	struct switcher *switcher = container_of(grab, struct switcher, grab);
	struct weston_seat *seat = (struct weston_seat *) grab->keyboard->seat;

	if ((seat->modifier_state & switcher->shell->binding_modifier) == 0)
		switcher_destroy(switcher);
}

static void
switcher_cancel(struct weston_keyboard_grab *grab)
{
	struct switcher *switcher = container_of(grab, struct switcher, grab);

	switcher_destroy(switcher);
}

static const struct weston_keyboard_grab_interface switcher_grab = {
	switcher_key,
	switcher_modifier,
	switcher_cancel,
};

static void
switcher_binding(struct weston_seat *seat, uint32_t time, uint32_t key,
		 void *data)
{
	desktop_shell *shell = data;
	struct switcher *switcher;

	switcher = malloc(sizeof *switcher);
	switcher->shell = shell;
	switcher->current = NULL;
	switcher->listener.notify = switcher_handle_surface_destroy;
	wl_list_init(&switcher->listener.link);
	wl_array_init(&switcher->minimized_array);

	restore_all_output_modes(shell->compositor);
	lower_fullscreen_layer(switcher->shell);
	switcher->grab.interface = &switcher_grab;
	weston_keyboard_start_grab(seat->keyboard, &switcher->grab);
	weston_keyboard_set_focus(seat->keyboard, NULL);
	switcher_next(switcher);
}

static void
backlight_binding(struct weston_seat *seat, uint32_t time, uint32_t key,
		  void *data)
{
	struct weston_compositor *compositor = data;
	struct weston_output *output;
	long backlight_new = 0;

	/* TODO: we're limiting to simple use cases, where we assume just
	 * control on the primary display. We'd have to extend later if we
	 * ever get support for setting backlights on random desktop LCD
	 * panels though */
	output = get_default_output(compositor);
	if (!output)
		return;

	if (!output->set_backlight)
		return;

	if (key == KEY_F9 || key == KEY_BRIGHTNESSDOWN)
		backlight_new = output->backlight_current - 25;
	else if (key == KEY_F10 || key == KEY_BRIGHTNESSUP)
		backlight_new = output->backlight_current + 25;

	if (backlight_new < 5)
		backlight_new = 5;
	if (backlight_new > 255)
		backlight_new = 255;

	output->backlight_current = backlight_new;
	output->set_backlight(output, output->backlight_current);
}

struct debug_binding_grab {
	struct weston_keyboard_grab grab;
	struct weston_seat *seat;
	uint32_t key[2];
	int key_released[2];
};

static void
debug_binding_key(struct weston_keyboard_grab *grab, uint32_t time,
		  uint32_t key, uint32_t state)
{
	struct debug_binding_grab *db = (struct debug_binding_grab *) grab;
	struct weston_compositor *ec = db->seat->compositor;
	struct wl_display *display = ec->wl_display;
	struct wl_resource *resource;
	uint32_t serial;
	int send = 0, terminate = 0;
	int check_binding = 1;
	int i;
	struct wl_list *resource_list;

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		/* Do not run bindings on key releases */
		check_binding = 0;

		for (i = 0; i < 2; i++)
			if (key == db->key[i])
				db->key_released[i] = 1;

		if (db->key_released[0] && db->key_released[1]) {
			/* All key releases been swalled so end the grab */
			terminate = 1;
		} else if (key != db->key[0] && key != db->key[1]) {
			/* Should not swallow release of other keys */
			send = 1;
		}
	} else if (key == db->key[0] && !db->key_released[0]) {
		/* Do not check bindings for the first press of the binding
		 * key. This allows it to be used as a debug shortcut.
		 * We still need to swallow this event. */
		check_binding = 0;
	} else if (db->key[1]) {
		/* If we already ran a binding don't process another one since
		 * we can't keep track of all the binding keys that were
		 * pressed in order to swallow the release events. */
		send = 1;
		check_binding = 0;
	}

	if (check_binding) {
		if (weston_compositor_run_debug_binding(ec, db->seat, time,
							key, state)) {
			/* We ran a binding so swallow the press and keep the
			 * grab to swallow the released too. */
			send = 0;
			terminate = 0;
			db->key[1] = key;
		} else {
			/* Terminate the grab since the key pressed is not a
			 * debug binding key. */
			send = 1;
			terminate = 1;
		}
	}

	if (send) {
		serial = wl_display_next_serial(display);
		resource_list = &grab->keyboard->focus_resource_list;
		wl_resource_for_each(resource, resource_list) {
			wl_keyboard_send_key(resource, serial, time, key, state);
		}
	}

	if (terminate) {
		weston_keyboard_end_grab(grab->keyboard);
		if (grab->keyboard->input_method_resource)
			grab->keyboard->grab = &grab->keyboard->input_method_grab;
		free(db);
	}
}

static void
debug_binding_modifiers(struct weston_keyboard_grab *grab, uint32_t serial,
			uint32_t mods_depressed, uint32_t mods_latched,
			uint32_t mods_locked, uint32_t group)
{
	struct wl_resource *resource;
	struct wl_list *resource_list;

	resource_list = &grab->keyboard->focus_resource_list;

	wl_resource_for_each(resource, resource_list) {
		wl_keyboard_send_modifiers(resource, serial, mods_depressed,
					   mods_latched, mods_locked, group);
	}
}

static void
debug_binding_cancel(struct weston_keyboard_grab *grab)
{
	struct debug_binding_grab *db = (struct debug_binding_grab *) grab;

	weston_keyboard_end_grab(grab->keyboard);
	free(db);
}

struct weston_keyboard_grab_interface debug_binding_keyboard_grab = {
	debug_binding_key,
	debug_binding_modifiers,
	debug_binding_cancel,
};

static void
debug_binding(struct weston_seat *seat, uint32_t time, uint32_t key, void *data)
{
	struct debug_binding_grab *grab;

	grab = calloc(1, sizeof *grab);
	if (!grab)
		return;

	grab->seat = (struct weston_seat *) seat;
	grab->key[0] = key;
	grab->grab.interface = &debug_binding_keyboard_grab;
	weston_keyboard_start_grab(seat->keyboard, &grab->grab);
}

static void
force_kill_binding(struct weston_seat *seat, uint32_t time, uint32_t key,
		   void *data)
{
	struct weston_surface *focus_surface;
	struct wl_client *client;
	struct desktop_shell *shell = data;
	struct weston_compositor *compositor = shell->compositor;
	pid_t pid;

	focus_surface = seat->keyboard->focus;
	if (!focus_surface)
		return;

	wl_signal_emit(&compositor->kill_signal, focus_surface);

	client = wl_resource_get_client(focus_surface->resource);
	wl_client_get_credentials(client, &pid, NULL, NULL);

	/* Skip clients that we launched ourselves (the credentials of
	 * the socketpair is ours) */
	if (pid == getpid())
		return;

	kill(pid, SIGKILL);
}

static void
workspace_up_binding(struct weston_seat *seat, uint32_t time,
		     uint32_t key, void *data)
{
	struct desktop_shell *shell = data;
	unsigned int new_index = shell->workspaces.current;

	if (shell->locked)
		return;
	if (new_index != 0)
		new_index--;

	change_workspace(shell, new_index);
}

static void
workspace_down_binding(struct weston_seat *seat, uint32_t time,
		       uint32_t key, void *data)
{
	struct desktop_shell *shell = data;
	unsigned int new_index = shell->workspaces.current;

	if (shell->locked)
		return;
	if (new_index < shell->workspaces.num - 1)
		new_index++;

	change_workspace(shell, new_index);
}

static void
workspace_f_binding(struct weston_seat *seat, uint32_t time,
		    uint32_t key, void *data)
{
	struct desktop_shell *shell = data;
	unsigned int new_index;

	if (shell->locked)
		return;
	new_index = key - KEY_F1;
	if (new_index >= shell->workspaces.num)
		new_index = shell->workspaces.num - 1;

	change_workspace(shell, new_index);
}

static void
workspace_move_surface_up_binding(struct weston_seat *seat, uint32_t time,
				  uint32_t key, void *data)
{
	struct desktop_shell *shell = data;
	unsigned int new_index = shell->workspaces.current;

	if (shell->locked)
		return;

	if (new_index != 0)
		new_index--;

	take_surface_to_workspace_by_seat(shell, seat, new_index);
}

static void
workspace_move_surface_down_binding(struct weston_seat *seat, uint32_t time,
				    uint32_t key, void *data)
{
	struct desktop_shell *shell = data;
	unsigned int new_index = shell->workspaces.current;

	if (shell->locked)
		return;

	if (new_index < shell->workspaces.num - 1)
		new_index++;

	take_surface_to_workspace_by_seat(shell, seat, new_index);
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

void
shell_for_each_layer(struct desktop_shell *shell,
		     shell_for_each_layer_func_t func, void *data)
{
	struct workspace **ws;

	func(shell, &shell->fullscreen_layer, data);
	func(shell, &shell->panel_layer, data);
	func(shell, &shell->background_layer, data);
	func(shell, &shell->lock_layer, data);
	func(shell, &shell->input_panel_layer, data);

	wl_array_for_each(ws, &shell->workspaces.array)
		func(shell, &(*ws)->layer, data);
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

	shell_for_each_layer(shell, shell_output_destroy_move_layer, output);

	wl_list_remove(&output_listener->destroy_listener.link);
	wl_list_remove(&output_listener->link);
	free(output_listener);
}

static void
create_shell_output(struct desktop_shell *shell,
					struct weston_output *output)
{
	struct shell_output *shell_output;

	shell_output = zalloc(sizeof *shell_output);
	if (shell_output == NULL)
		return;

	shell_output->output = output;
	shell_output->shell = shell;
	shell_output->destroy_listener.notify = handle_output_destroy;
	wl_signal_add(&output->destroy_signal,
		      &shell_output->destroy_listener);
	wl_list_insert(shell->output_list.prev, &shell_output->link);
}

static void
handle_output_create(struct wl_listener *listener, void *data)
{
	struct desktop_shell *shell =
		container_of(listener, struct desktop_shell, output_create_listener);
	struct weston_output *output = (struct weston_output *)data;

	create_shell_output(shell, output);
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

static void
handle_output_move(struct wl_listener *listener, void *data)
{
	struct desktop_shell *shell;

	shell = container_of(listener, struct desktop_shell,
			     output_move_listener);

	shell_for_each_layer(shell, handle_output_move_layer, data);
}

static void
setup_output_destroy_handler(struct weston_compositor *ec,
							struct desktop_shell *shell)
{
	struct weston_output *output;

	wl_list_init(&shell->output_list);
	wl_list_for_each(output, &ec->output_list, link)
		create_shell_output(shell, output);

	shell->output_create_listener.notify = handle_output_create;
	wl_signal_add(&ec->output_created_signal,
				&shell->output_create_listener);

	shell->output_move_listener.notify = handle_output_move;
	wl_signal_add(&ec->output_moved_signal, &shell->output_move_listener);
}

static void
shell_destroy(struct wl_listener *listener, void *data)
{
	struct desktop_shell *shell =
		container_of(listener, struct desktop_shell, destroy_listener);
	struct workspace **ws;
	struct shell_output *shell_output, *tmp;

	/* Force state to unlocked so we don't try to fade */
	shell->locked = false;

	if (shell->child.client) {
		/* disable respawn */
		wl_list_remove(&shell->child.client_destroy_listener.link);
		wl_client_destroy(shell->child.client);
	}

	wl_list_remove(&shell->idle_listener.link);
	wl_list_remove(&shell->wake_listener.link);

	input_panel_destroy(shell);

	wl_list_for_each_safe(shell_output, tmp, &shell->output_list, link) {
		wl_list_remove(&shell_output->destroy_listener.link);
		wl_list_remove(&shell_output->link);
		free(shell_output);
	}

	wl_list_remove(&shell->output_create_listener.link);
	wl_list_remove(&shell->output_move_listener.link);

	wl_array_for_each(ws, &shell->workspaces.array)
		workspace_destroy(*ws);
	wl_array_release(&shell->workspaces.array);

	free(shell->screensaver.path);
	free(shell->client);
	free(shell);
}

static void
shell_add_bindings(struct weston_compositor *ec, struct desktop_shell *shell)
{
	uint32_t mod;
	int i, num_workspace_bindings;

	/* fixed bindings */
	weston_compositor_add_key_binding(ec, KEY_BACKSPACE,
				          MODIFIER_CTRL | MODIFIER_ALT,
				          terminate_binding, ec);
	weston_compositor_add_button_binding(ec, BTN_LEFT, 0,
					     click_to_activate_binding,
					     shell);
	weston_compositor_add_button_binding(ec, BTN_RIGHT, 0,
					     click_to_activate_binding,
					     shell);
	weston_compositor_add_touch_binding(ec, 0,
					    touch_to_activate_binding,
					    shell);
	weston_compositor_add_axis_binding(ec, WL_POINTER_AXIS_VERTICAL_SCROLL,
				           MODIFIER_SUPER | MODIFIER_ALT,
				           surface_opacity_binding, NULL);
	weston_compositor_add_axis_binding(ec, WL_POINTER_AXIS_VERTICAL_SCROLL,
					   MODIFIER_SUPER, zoom_axis_binding,
					   NULL);

	/* configurable bindings */
	mod = shell->binding_modifier;
	weston_compositor_add_key_binding(ec, KEY_PAGEUP, mod,
					  zoom_key_binding, NULL);
	weston_compositor_add_key_binding(ec, KEY_PAGEDOWN, mod,
					  zoom_key_binding, NULL);
	weston_compositor_add_key_binding(ec, KEY_M, mod | MODIFIER_SHIFT,
					  maximize_binding, NULL);
	weston_compositor_add_key_binding(ec, KEY_F, mod | MODIFIER_SHIFT,
					  fullscreen_binding, NULL);
	weston_compositor_add_button_binding(ec, BTN_LEFT, mod, move_binding,
					     shell);
	weston_compositor_add_touch_binding(ec, mod, touch_move_binding, shell);
	weston_compositor_add_button_binding(ec, BTN_MIDDLE, mod,
					     resize_binding, shell);
	weston_compositor_add_button_binding(ec, BTN_LEFT,
					     mod | MODIFIER_SHIFT,
					     resize_binding, shell);

	if (ec->capabilities & WESTON_CAP_ROTATION_ANY)
		weston_compositor_add_button_binding(ec, BTN_RIGHT, mod,
						     rotate_binding, NULL);

	weston_compositor_add_key_binding(ec, KEY_TAB, mod, switcher_binding,
					  shell);
	weston_compositor_add_key_binding(ec, KEY_F9, mod, backlight_binding,
					  ec);
	weston_compositor_add_key_binding(ec, KEY_BRIGHTNESSDOWN, 0,
				          backlight_binding, ec);
	weston_compositor_add_key_binding(ec, KEY_F10, mod, backlight_binding,
					  ec);
	weston_compositor_add_key_binding(ec, KEY_BRIGHTNESSUP, 0,
				          backlight_binding, ec);
	weston_compositor_add_key_binding(ec, KEY_K, mod,
				          force_kill_binding, shell);
	weston_compositor_add_key_binding(ec, KEY_UP, mod,
					  workspace_up_binding, shell);
	weston_compositor_add_key_binding(ec, KEY_DOWN, mod,
					  workspace_down_binding, shell);
	weston_compositor_add_key_binding(ec, KEY_UP, mod | MODIFIER_SHIFT,
					  workspace_move_surface_up_binding,
					  shell);
	weston_compositor_add_key_binding(ec, KEY_DOWN, mod | MODIFIER_SHIFT,
					  workspace_move_surface_down_binding,
					  shell);

	if (shell->exposay_modifier)
		weston_compositor_add_modifier_binding(ec, shell->exposay_modifier,
						       exposay_binding, shell);

	/* Add bindings for mod+F[1-6] for workspace 1 to 6. */
	if (shell->workspaces.num > 1) {
		num_workspace_bindings = shell->workspaces.num;
		if (num_workspace_bindings > 6)
			num_workspace_bindings = 6;
		for (i = 0; i < num_workspace_bindings; i++)
			weston_compositor_add_key_binding(ec, KEY_F1 + i, mod,
							  workspace_f_binding,
							  shell);
	}

	/* Debug bindings */
	weston_compositor_add_key_binding(ec, KEY_SPACE, mod | MODIFIER_SHIFT,
					  debug_binding, shell);
}

static void
handle_seat_created(struct wl_listener *listener, void *data)
{
	struct weston_seat *seat = data;

	create_shell_seat(seat);
}

/*** THE ENTRY POINT ***/
WL_EXPORT int
module_init(struct weston_compositor *ec,
	    int *argc, char *argv[])
{
	struct weston_seat *seat;
	struct desktop_shell *shell;
	struct workspace **pws;
	unsigned int i;
	struct wl_event_loop *loop;

	shell = zalloc(sizeof *shell);
	if (shell == NULL)
		return -1;

	shell->compositor = ec;

	shell->destroy_listener.notify = shell_destroy;
	wl_signal_add(&ec->destroy_signal, &shell->destroy_listener);
	shell->idle_listener.notify = idle_handler;
	wl_signal_add(&ec->idle_signal, &shell->idle_listener);
	shell->wake_listener.notify = wake_handler;
	wl_signal_add(&ec->wake_signal, &shell->wake_listener);

	ec->shell_interface = page::weston_shell_interface_impl;
	ec->shell_interface.shell = shell;

	weston_layer_init(&shell->fullscreen_layer, &ec->cursor_layer.link);
	weston_layer_init(&shell->panel_layer, &shell->fullscreen_layer.link);
	weston_layer_init(&shell->background_layer, &shell->panel_layer.link);
	weston_layer_init(&shell->lock_layer, NULL);
	weston_layer_init(&shell->input_panel_layer, NULL);

	wl_array_init(&shell->workspaces.array);
	wl_list_init(&shell->workspaces.client_list);

	if (input_panel_setup(shell) < 0)
		return -1;

	shell_configuration(shell);

	shell->exposay.state_cur = EXPOSAY_LAYOUT_INACTIVE;
	shell->exposay.state_target = EXPOSAY_TARGET_CANCEL;

	for (i = 0; i < shell->workspaces.num; i++) {
		pws = wl_array_add(&shell->workspaces.array, sizeof *pws);
		if (pws == NULL)
			return -1;

		*pws = workspace_create();
		if (*pws == NULL)
			return -1;
	}
	activate_workspace(shell, 0);

	weston_layer_init(&shell->minimized_layer, NULL);

	wl_list_init(&shell->workspaces.anim_sticky_list);
	wl_list_init(&shell->workspaces.animation.link);
	shell->workspaces.animation.frame = animate_workspace_change_frame;

	/**
	 * create a global interface of type shell
	 * wl_shell_interface is the description of shell_interface defined in libwayland.
	 **/
	if (wl_global_create(ec->wl_display, &wl_shell_interface, 1,
				  shell, bind_shell) == NULL)
		return -1;

	/**
	 * create a global interface of type xdg-shell
	 * xdg_shell_interface is the description of xdg-shell-insterface defined in _weston_ only.
	 **/
	if (wl_global_create(ec->wl_display, &xdg_shell_interface, 1,
				  shell, bind_xdg_shell) == NULL)
		return -1;

	/**
	 * create a global interface of type desktop_shell_interface
	 * desktop_shell_interface is an extension provided by _weston_ only and is intend to be used by client
	 * to dynamically change setting of weston.
	 *
	 * xdg_shell_interface is the description of xdg-shell-interface defined in _weston_ only.
	 **/
	if (wl_global_create(ec->wl_display,
			     &desktop_shell_interface, 3,
			     shell, bind_desktop_shell) == NULL)
		return -1;

	if (wl_global_create(ec->wl_display, &screensaver_interface, 1,
			     shell, bind_screensaver) == NULL)
		return -1;

	if (wl_global_create(ec->wl_display, &workspace_manager_interface, 1,
			     shell, bind_workspace_manager) == NULL)
		return -1;

	shell->child.deathstamp = weston_compositor_get_time();

	shell->panel_position = DESKTOP_SHELL_PANEL_POSITION_TOP;

	setup_output_destroy_handler(ec, shell);

	loop = wl_display_get_event_loop(ec->wl_display);
	wl_event_loop_add_idle(loop, launch_desktop_shell_process, shell);

	shell->screensaver.timer =
		wl_event_loop_add_timer(loop, screensaver_timeout, shell);

	wl_list_for_each(seat, &ec->seat_list, link)
		handle_seat_created(NULL, seat);
	shell->seat_create_listener.notify = handle_seat_created;
	wl_signal_add(&ec->seat_created_signal, &shell->seat_create_listener);

	screenshooter_create(ec);

	shell_add_bindings(ec, shell);

	shell_fade_init(shell);

	clock_gettime(CLOCK_MONOTONIC, &shell->startup_time);

	return 0;
}
