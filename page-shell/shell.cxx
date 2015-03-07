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
destroy_shell_grab_shsurf(struct wl_listener *listener, void *data)
{
	struct shell_grab *grab;

	grab = container_of(listener, struct shell_grab,
			    shsurf_destroy_listener);

	grab->shsurf = NULL;
}

namespace page {
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

void
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

void
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

//static void
//seat_destroyed(struct wl_listener *listener, void *data)
//{
//	 weston_seat *seat = data;
//	 focus_state *state, *next;
//	 workspace *ws = container_of(listener,
//					    struct workspace,
//					    seat_destroyed_listener);
//
//	wl_list_for_each_safe(state, next, &ws->focus_list, link)
//		if (state->seat == seat)
//			wl_list_remove(&state->link);
//}



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

void
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
workspace_deactivate_transforms(workspace *ws)
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

void
animate_workspace_change_frame(weston_animation *animation,
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
		shell->reverse_workspace_change_animation(index, from, to);
		shell->broadcast_current_workspace_state();
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

	shell->broadcast_current_workspace_state();
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

		shell->reverse_workspace_change_animation(index, from, to);
		shell->broadcast_current_workspace_state();

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

	shell->broadcast_current_workspace_state();

	state = ensure_focus_state(shell, seat);
	if (state != NULL)
		state->focus_state_set_focus(surface);
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

static int
black_surface_get_label(struct weston_surface *surface, char *buf, size_t len)
{
	struct weston_surface *fs_surface = surface->configure_private;
	int n;
	int rem;
	int ret;

	n = snprintf(buf, len, "black background surface for ");
	if (n < 0)
		return n;

	rem = (int)len - n;
	if (rem < 0)
		rem = 0;

	if (fs_surface->get_label)
		ret = fs_surface->get_label(fs_surface, buf + n, rem);
	else
		ret = snprintf(buf + n, rem, "<unknown>");

	if (ret < 0)
		return n;

	return n + ret;
}


static struct weston_view *
create_pix_surface(struct weston_compositor *ec,
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
	weston_surface_set_label_func(surface, black_surface_get_label);


	struct weston_local_texture * tex;
	tex = weston_local_texture_create(WL_SHM_FORMAT_ARGB8888, w, h);
	uint8_t * data = weston_local_texture_get_data(tex);

	int i;
	for(i = 0; i < w*h*4; ++i) {
		data[i] = random()%256;
	}

	struct weston_buffer * buffer = weston_buffer_create_empty();

	/** resource == NULL mean local texture **/
	buffer->resource = NULL;
	buffer->local_tex = tex;
	buffer->width = tex->width;
	buffer->height = tex->height;
	weston_surface_attach(surface, buffer);

	pixman_region32_fini(&surface->opaque);
	pixman_region32_init_rect(&surface->opaque, 0, 0, w, h);
	pixman_region32_fini(&surface->input);
	pixman_region32_init_rect(&surface->input, 0, 0, w, h);

	weston_surface_set_size(surface, w, h);
	weston_view_set_position(view, x, y);

	return view;
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
			shsurf->shell_surface_is_xdg_popup())
		xdg_shell_send_ping(sc->resource, serial);
	else if (shsurf->shell_surface_is_wl_shell_surface())
		wl_shell_surface_send_ping(shsurf->resource, serial);
}

void
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

void
restore_output_mode(struct weston_output *output)
{
	if (output->original_mode)
		weston_output_mode_switch_to_native(output);
}

static void
restore_all_output_modes(struct weston_compositor *compositor)
{
	struct weston_output *output;

	wl_list_for_each(output, &compositor->output_list, link)
		restore_output_mode(output);
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

namespace page {
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
		shsurf->shell_configure_fullscreen();
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

}



bool
is_black_surface (struct weston_surface *es, struct weston_surface **fs_surface)
{
	if (es->configure == black_surface_configure) {
		if (fs_surface)
			*fs_surface = (struct weston_surface *)es->configure_private;
		return true;
	}
	return false;
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





static void launch_desktop_shell_process(void *data);

int
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
		page::activate(switcher->shell, switcher->current,
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










static cairo_surface_t *
get_cairo_surface_for_weston_buffer(struct weston_buffer * buffer) {

	/** this is not a local texture **/
	if(buffer->resource != nullptr)
		return nullptr;

	return cairo_image_surface_create_for_data(weston_local_texture_get_data(buffer->local_tex), CAIRO_FORMAT_ARGB32, buffer->local_tex->width, buffer->local_tex->height, buffer->local_tex->stride);

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

	shell = new desktop_shell{ec, argc, argv};


	struct weston_output * output = get_default_output(shell->compositor);
	struct weston_view * v = create_pix_surface(shell->compositor,
            NULL,
            output->x, output->y,
            output->width,
            output->height);
	weston_layer_entry_insert(&shell->background_real_layer.view_list, &v->layer_link);
	weston_view_geometry_dirty(v);
	weston_surface_schedule_repaint(v->surface);

	weston_buffer_reference(&shell->background_tex, v->surface->buffer_ref.buffer);

	i_rect area{0,0,800,600};
	cairo_surface_t * surf = get_cairo_surface_for_weston_buffer(v->surface->buffer_ref.buffer);
	cairo_t * cr = cairo_create(surf);
	/** draw background **/
	shell->theme->render_empty(cr, area);
	cairo_destroy(cr);
	cairo_surface_destroy(surf);

	weston_surface_attach(v->surface, shell->background_tex.buffer);

//
//	shell = zalloc(sizeof *shell);
//	if (shell == NULL)
//		return -1;
//
//	shell->compositor = ec;
//
//	shell->destroy_listener.notify = shell_destroy;
//	wl_signal_add(&ec->destroy_signal, &shell->destroy_listener);
//	shell->idle_listener.notify = idle_handler;
//	wl_signal_add(&ec->idle_signal, &shell->idle_listener);
//	shell->wake_listener.notify = wake_handler;
//	wl_signal_add(&ec->wake_signal, &shell->wake_listener);
//
//	ec->shell_interface = page::weston_shell_interface_impl;
//	ec->shell_interface.shell = shell;
//
//	weston_layer_init(&shell->fullscreen_layer, &ec->cursor_layer.link);
//	weston_layer_init(&shell->panel_layer, &shell->fullscreen_layer.link);
//	weston_layer_init(&shell->background_layer, &shell->panel_layer.link);
//	weston_layer_init(&shell->lock_layer, NULL);
//	weston_layer_init(&shell->input_panel_layer, NULL);
//
//	wl_array_init(&shell->workspaces.array);
//	wl_list_init(&shell->workspaces.client_list);
//
//	if (input_panel_setup(shell) < 0)
//		return -1;
//
//	shell->shell_configuration();
//
//	shell->exposay.state_cur = EXPOSAY_LAYOUT_INACTIVE;
//	shell->exposay.state_target = EXPOSAY_TARGET_CANCEL;
//
//	for (i = 0; i < shell->workspaces.num; i++) {
//		pws = wl_array_add(&shell->workspaces.array, sizeof *pws);
//		if (pws == NULL)
//			return -1;
//
//		*pws = workspace_create();
//		if (*pws == NULL)
//			return -1;
//	}
//	activate_workspace(shell, 0);
//
//	weston_layer_init(&shell->minimized_layer, NULL);
//
//	wl_list_init(&shell->workspaces.anim_sticky_list);
//	wl_list_init(&shell->workspaces.animation.link);
//	shell->workspaces.animation.frame = animate_workspace_change_frame;
//
//	/**
//	 * create a global interface of type shell
//	 * wl_shell_interface is the description of shell_interface defined in libwayland.
//	 **/
//	if (wl_global_create(ec->wl_display, &wl_shell_interface, 1,
//				  shell, bind_shell) == NULL)
//		return -1;
//
//	/**
//	 * create a global interface of type xdg-shell
//	 * xdg_shell_interface is the description of xdg-shell-insterface defined in _weston_ only.
//	 **/
//	if (wl_global_create(ec->wl_display, &xdg_shell_interface, 1,
//				  shell, bind_xdg_shell) == NULL)
//		return -1;
//
//	/**
//	 * create a global interface of type desktop_shell_interface
//	 * desktop_shell_interface is an extension provided by _weston_ only and is intend to be used by client
//	 * to dynamically change setting of weston.
//	 *
//	 * xdg_shell_interface is the description of xdg-shell-interface defined in _weston_ only.
//	 **/
//	if (wl_global_create(ec->wl_display,
//			     &desktop_shell_interface, 3,
//			     shell, bind_desktop_shell) == NULL)
//		return -1;
//
//	if (wl_global_create(ec->wl_display, &screensaver_interface, 1,
//			     shell, bind_screensaver) == NULL)
//		return -1;
//
//	if (wl_global_create(ec->wl_display, &workspace_manager_interface, 1,
//			     shell, bind_workspace_manager) == NULL)
//		return -1;
//
//	shell->child.deathstamp = weston_compositor_get_time();
//
//	shell->panel_position = DESKTOP_SHELL_PANEL_POSITION_TOP;
//
//	setup_output_destroy_handler(ec, shell);
//
//	loop = wl_display_get_event_loop(ec->wl_display);
//	wl_event_loop_add_idle(loop, launch_desktop_shell_process, shell);
//
//	shell->screensaver.timer =
//		wl_event_loop_add_timer(loop, screensaver_timeout, shell);
//
//	wl_list_for_each(seat, &ec->seat_list, link)
//		handle_seat_created(NULL, seat);
//	shell->seat_create_listener.notify = handle_seat_created;
//	wl_signal_add(&ec->seat_created_signal, &shell->seat_create_listener);
//
//	screenshooter_create(ec);
//
//	shell_add_bindings(ec, shell);
//
//	shell_fade_init(shell);
//
//	clock_gettime(CLOCK_MONOTONIC, &shell->startup_time);

	return 0;
}
