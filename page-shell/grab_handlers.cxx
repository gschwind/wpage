/*
 * grab_handlers.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "grab_handlers.hxx"

#include <cassert>


namespace page {

static void noop_grab_focus(struct weston_pointer_grab *grab);
static void move_grab_motion(struct weston_pointer_grab *grab, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void move_grab_button(struct weston_pointer_grab *grab, uint32_t time, uint32_t button, uint32_t state_w);
static void move_grab_cancel(struct weston_pointer_grab *grab);

const struct weston_pointer_grab_interface move_grab_interface = {
	noop_grab_focus,
	move_grab_motion,
	move_grab_button,
	move_grab_cancel,
};

static void popup_grab_focus(struct weston_pointer_grab *grab);
static void popup_grab_motion(struct weston_pointer_grab *grab, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void popup_grab_button(struct weston_pointer_grab *grab, uint32_t time, uint32_t button, uint32_t state_w);
static void popup_grab_cancel(struct weston_pointer_grab *grab);

const weston_pointer_grab_interface popup_grab_interface = {
	popup_grab_focus,
	popup_grab_motion,
	popup_grab_button,
	popup_grab_cancel
};

void destroy_shell_grab_shsurf(struct wl_listener *listener, void *data);

weston_view * get_default_view(weston_surface *surface);


void activate(struct desktop_shell *shell, struct weston_surface *es, struct weston_seat *seat, bool configure);


static void constrain_position(struct weston_move_grab *move, int *cx, int *cy);

static void busy_cursor_grab_button(struct weston_pointer_grab *base, uint32_t time, uint32_t button, uint32_t state);
static void busy_cursor_grab_cancel(struct weston_pointer_grab *base);
static void busy_cursor_grab_focus(struct weston_pointer_grab *base);
static void busy_cursor_grab_motion(struct weston_pointer_grab *grab, uint32_t time, wl_fixed_t x, wl_fixed_t y);

const struct weston_pointer_grab_interface busy_cursor_grab_interface = {
	busy_cursor_grab_focus,
	busy_cursor_grab_motion,
	busy_cursor_grab_button,
	busy_cursor_grab_cancel
};

static void touch_move_grab_down(struct weston_touch_grab *grab, uint32_t time, int touch_id, wl_fixed_t sx, wl_fixed_t sy);
static void touch_move_grab_up(struct weston_touch_grab *grab, uint32_t time, int touch_id);
static void touch_move_grab_motion(struct weston_touch_grab *grab, uint32_t time, int touch_id, wl_fixed_t sx, wl_fixed_t sy);
static void touch_move_grab_frame(struct weston_touch_grab *grab);
static void touch_move_grab_cancel(struct weston_touch_grab *grab);

const struct weston_touch_grab_interface touch_move_grab_interface = {
	touch_move_grab_down,
	touch_move_grab_up,
	touch_move_grab_motion,
	touch_move_grab_frame,
	touch_move_grab_cancel
};

static void rotate_grab_motion(struct weston_pointer_grab *grab, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void rotate_grab_button(struct weston_pointer_grab *grab, uint32_t time, uint32_t button, uint32_t state_w);
static void rotate_grab_cancel(struct weston_pointer_grab *grab);

const struct weston_pointer_grab_interface rotate_grab_interface = {
	noop_grab_focus,
	rotate_grab_motion,
	rotate_grab_button,
	rotate_grab_cancel
};



static void resize_grab_motion(struct weston_pointer_grab *grab, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void resize_grab_button(struct weston_pointer_grab *grab, uint32_t time, uint32_t button, uint32_t state_w);
static void resize_grab_cancel(struct weston_pointer_grab *grab);

const struct weston_pointer_grab_interface resize_grab_interface = {
	noop_grab_focus,
	resize_grab_motion,
	resize_grab_button,
	resize_grab_cancel
};





void move_grab_button(struct weston_pointer_grab *grab,
		 uint32_t time, uint32_t button, uint32_t state_w)
{
	struct shell_grab *shell_grab = container_of(grab, struct shell_grab,
						    grab);
	struct weston_pointer *pointer = grab->pointer;
	wl_pointer_button_state state = static_cast<wl_pointer_button_state>(state_w);

	if (pointer->button_count == 0 &&
	    state == WL_POINTER_BUTTON_STATE_RELEASED) {
		shell_grab_end(shell_grab);
		free(grab);
	}
}

 void
move_grab_cancel(struct weston_pointer_grab *grab)
{
	struct shell_grab *shell_grab =
		container_of(grab, struct shell_grab, grab);

	shell_grab_end(shell_grab);
	free(grab);
}



 void
shell_grab_start(struct shell_grab *grab,
		 const struct weston_pointer_grab_interface *interface,
		 shell_surface *shsurf,
		 struct weston_pointer *pointer,
		 enum desktop_shell_cursor cursor)
{
	desktop_shell *shell = shsurf->shell;

	/** terminate current grabs ?? **/
	popup_grab_end(pointer);
	if (pointer->seat->touch)
		touch_popup_grab_end(pointer->seat->touch);

	grab->grab.interface = interface;
	grab->shsurf = shsurf;
	grab->shsurf_destroy_listener.notify = destroy_shell_grab_shsurf;
	wl_signal_add(&shsurf->destroy_signal,
		      &grab->shsurf_destroy_listener);

	shsurf->grabbed = 1;
	weston_pointer_start_grab(pointer, &grab->grab);
	if (shell->child.desktop_shell) {
		desktop_shell_send_grab_cursor(shell->child.desktop_shell,
					       cursor);
		weston_pointer_set_focus(pointer,
					 get_default_view(shell->grab_surface),
					 wl_fixed_from_int(0),
					 wl_fixed_from_int(0));
	}
}

void
shell_touch_grab_start(struct shell_touch_grab *grab,
		       const struct weston_touch_grab_interface *interface,
		       shell_surface *shsurf,
		       struct weston_touch *touch)
{
	desktop_shell *shell = shsurf->shell;

	touch_popup_grab_end(touch);
	if (touch->seat->pointer)
		popup_grab_end(touch->seat->pointer);

	grab->grab.interface = interface;
	grab->shsurf = shsurf;
	grab->shsurf_destroy_listener.notify = destroy_shell_grab_shsurf;
	wl_signal_add(&shsurf->destroy_signal,
		      &grab->shsurf_destroy_listener);

	grab->touch = touch;
	shsurf->grabbed = 1;

	weston_touch_start_grab(touch, &grab->grab);
	if (shell->child.desktop_shell)
		weston_touch_set_focus(touch->seat,
				       get_default_view(shell->grab_surface));
}

 void
shell_touch_grab_end(struct shell_touch_grab *grab)
{
	if (grab->shsurf) {
		wl_list_remove(&grab->shsurf_destroy_listener.link);
		grab->shsurf->grabbed = 0;
	}

	weston_touch_end_grab(grab->touch);
}


void
shell_grab_end(struct shell_grab *grab)
{
	if (grab->shsurf) {
		wl_list_remove(&grab->shsurf_destroy_listener.link);
		grab->shsurf->grabbed = 0;

		if (grab->shsurf->resize_edges) {
			grab->shsurf->resize_edges = 0;
			grab->shsurf->shell_surface_state_changed();
		}
	}

	weston_pointer_end_grab(grab->grab.pointer);
}

void
end_busy_cursor(struct weston_compositor *compositor, struct wl_client *client)
{
	struct shell_grab *grab;
	struct weston_seat *seat;

	wl_list_for_each(seat, &compositor->seat_list, link) {
		if (seat->pointer == NULL)
			continue;

		grab = (struct shell_grab *) seat->pointer->grab;
		if (grab->grab.interface == &busy_cursor_grab_interface &&
		    wl_resource_get_client(grab->shsurf->resource) == client) {
			shell_grab_end(grab);
			free(grab);
		}
	}
}

static void
busy_cursor_grab_focus(struct weston_pointer_grab *base)
{
	struct shell_grab *grab = (struct shell_grab *) base;
	struct weston_pointer *pointer = base->pointer;
	struct weston_view *view;
	wl_fixed_t sx, sy;

	view = weston_compositor_pick_view(pointer->seat->compositor,
					   pointer->x, pointer->y,
					   &sx, &sy);

	if (!grab->shsurf || grab->shsurf->surface != view->surface) {
		shell_grab_end(grab);
		free(grab);
	}
}

static void
busy_cursor_grab_motion(struct weston_pointer_grab *grab, uint32_t time,
			wl_fixed_t x, wl_fixed_t y)
{
	weston_pointer_move(grab->pointer, x, y);
}

static void
busy_cursor_grab_button(struct weston_pointer_grab *base,
			uint32_t time, uint32_t button, uint32_t state)
{
	struct shell_grab *grab = (struct shell_grab *) base;
	shell_surface *shsurf = grab->shsurf;
	struct weston_seat *seat = grab->grab.pointer->seat;

	if (shsurf && button == BTN_LEFT && state) {
		activate(shsurf->shell, shsurf->surface, seat, true);
		shsurf->surface_move(seat, 0);
	} else if (shsurf && button == BTN_RIGHT && state) {
		activate(shsurf->shell, shsurf->surface, seat, true);
		shsurf->surface_rotate(seat);
	}
}

static void
busy_cursor_grab_cancel(struct weston_pointer_grab *base)
{
	struct shell_grab *grab = (struct shell_grab *) base;

	shell_grab_end(grab);
	free(grab);
}



static void
noop_grab_focus(struct weston_pointer_grab *grab)
{
}

static void
move_grab_motion(struct weston_pointer_grab *grab, uint32_t time,
		 wl_fixed_t x, wl_fixed_t y)
{
	struct weston_move_grab *move = (struct weston_move_grab *) grab;
	struct weston_pointer *pointer = grab->pointer;
	shell_surface *shsurf = move->base.shsurf;
	int cx, cy;

	weston_pointer_move(pointer, x, y);
	if (!shsurf)
		return;

	constrain_position(move, &cx, &cy);

	weston_view_set_position(shsurf->view, cx, cy);

	weston_compositor_schedule_repaint(shsurf->surface->compositor);
}

static void
destroy_shell_grab_shsurf(struct wl_listener *listener, void *data)
{
	struct shell_grab *grab;

	grab = container_of(listener, struct shell_grab,
			    shsurf_destroy_listener);

	grab->shsurf = NULL;
}

void
touch_move_grab_down(struct weston_touch_grab *grab, uint32_t time,
		     int touch_id, wl_fixed_t sx, wl_fixed_t sy)
{
}

void
touch_move_grab_up(struct weston_touch_grab *grab, uint32_t time, int touch_id)
{
	struct weston_touch_move_grab *move =
		(struct weston_touch_move_grab *) container_of(
			grab, struct shell_touch_grab, grab);

	if (touch_id == 0)
		move->active = 0;

	if (grab->touch->num_tp == 0) {
		shell_touch_grab_end(&move->base);
		free(move);
	}
}

void
touch_move_grab_motion(struct weston_touch_grab *grab, uint32_t time,
		       int touch_id, wl_fixed_t sx, wl_fixed_t sy)
{
	struct weston_touch_move_grab *move = (struct weston_touch_move_grab *) grab;
	shell_surface *shsurf = move->base.shsurf;
	struct weston_surface *es;
	int dx = wl_fixed_to_int(grab->touch->grab_x + move->dx);
	int dy = wl_fixed_to_int(grab->touch->grab_y + move->dy);

	if (!shsurf || !move->active)
		return;

	es = shsurf->surface;

	weston_view_set_position(shsurf->view, dx, dy);

	weston_compositor_schedule_repaint(es->compositor);
}

void
touch_move_grab_frame(struct weston_touch_grab *grab)
{
}

void
touch_move_grab_cancel(struct weston_touch_grab *grab)
{
	struct weston_touch_move_grab *move =
		(struct weston_touch_move_grab *) container_of(
			grab, struct shell_touch_grab, grab);

	shell_touch_grab_end(&move->base);
	free(move);
}



static void
rotate_grab_motion(struct weston_pointer_grab *grab, uint32_t time,
		   wl_fixed_t x, wl_fixed_t y)
{
	struct rotate_grab *rotate =
		container_of(grab, struct rotate_grab, base.grab);
	struct weston_pointer *pointer = grab->pointer;
	shell_surface *shsurf = rotate->base.shsurf;
	float cx, cy, dx, dy, cposx, cposy, dposx, dposy, r;

	weston_pointer_move(pointer, x, y);

	if (!shsurf)
		return;

	cx = 0.5f * shsurf->surface->width;
	cy = 0.5f * shsurf->surface->height;

	dx = wl_fixed_to_double(pointer->x) - rotate->center.x;
	dy = wl_fixed_to_double(pointer->y) - rotate->center.y;
	r = sqrtf(dx * dx + dy * dy);

	wl_list_remove(&shsurf->rotation.transform.link);
	weston_view_geometry_dirty(shsurf->view);

	if (r > 20.0f) {
		struct weston_matrix *matrix =
			&shsurf->rotation.transform.matrix;

		weston_matrix_init(&rotate->rotation);
		weston_matrix_rotate_xy(&rotate->rotation, dx / r, dy / r);

		weston_matrix_init(matrix);
		weston_matrix_translate(matrix, -cx, -cy, 0.0f);
		weston_matrix_multiply(matrix, &shsurf->rotation.rotation);
		weston_matrix_multiply(matrix, &rotate->rotation);
		weston_matrix_translate(matrix, cx, cy, 0.0f);

		wl_list_insert(
			&shsurf->view->geometry.transformation_list,
			&shsurf->rotation.transform.link);
	} else {
		wl_list_init(&shsurf->rotation.transform.link);
		weston_matrix_init(&shsurf->rotation.rotation);
		weston_matrix_init(&rotate->rotation);
	}

	/* We need to adjust the position of the surface
	 * in case it was resized in a rotated state before */
	cposx = shsurf->view->geometry.x + cx;
	cposy = shsurf->view->geometry.y + cy;
	dposx = rotate->center.x - cposx;
	dposy = rotate->center.y - cposy;
	if (dposx != 0.0f || dposy != 0.0f) {
		weston_view_set_position(shsurf->view,
					 shsurf->view->geometry.x + dposx,
					 shsurf->view->geometry.y + dposy);
	}

	/* Repaint implies weston_view_update_transform(), which
	 * lazily applies the damage due to rotation update.
	 */
	weston_compositor_schedule_repaint(shsurf->surface->compositor);
}

static void
rotate_grab_button(struct weston_pointer_grab *grab,
		   uint32_t time, uint32_t button, uint32_t state_w)
{
	struct rotate_grab *rotate =
		container_of(grab, struct rotate_grab, base.grab);
	struct weston_pointer *pointer = grab->pointer;
	shell_surface *shsurf = rotate->base.shsurf;
	enum wl_pointer_button_state state = state_w;

	if (pointer->button_count == 0 &&
	    state == WL_POINTER_BUTTON_STATE_RELEASED) {
		if (shsurf)
			weston_matrix_multiply(&shsurf->rotation.rotation,
					       &rotate->rotation);
		shell_grab_end(&rotate->base);
		free(rotate);
	}
}

static void
rotate_grab_cancel(struct weston_pointer_grab *grab)
{
	struct rotate_grab *rotate =
		container_of(grab, struct rotate_grab, base.grab);

	shell_grab_end(&rotate->base);
	free(rotate);
}


static void
resize_grab_motion(struct weston_pointer_grab *grab, uint32_t time,
		   wl_fixed_t x, wl_fixed_t y)
{
	struct weston_resize_grab *resize = (struct weston_resize_grab *) grab;
	struct weston_pointer *pointer = grab->pointer;
	shell_surface *shsurf = resize->base.shsurf;
	int32_t width, height;
	wl_fixed_t from_x, from_y;
	wl_fixed_t to_x, to_y;

	weston_pointer_move(pointer, x, y);

	if (!shsurf)
		return;

	weston_view_from_global_fixed(shsurf->view,
				      pointer->grab_x, pointer->grab_y,
				      &from_x, &from_y);
	weston_view_from_global_fixed(shsurf->view,
				      pointer->x, pointer->y, &to_x, &to_y);

	width = resize->width;
	if (resize->edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
		width += wl_fixed_to_int(from_x - to_x);
	} else if (resize->edges & WL_SHELL_SURFACE_RESIZE_RIGHT) {
		width += wl_fixed_to_int(to_x - from_x);
	}

	height = resize->height;
	if (resize->edges & WL_SHELL_SURFACE_RESIZE_TOP) {
		height += wl_fixed_to_int(from_y - to_y);
	} else if (resize->edges & WL_SHELL_SURFACE_RESIZE_BOTTOM) {
		height += wl_fixed_to_int(to_y - from_y);
	}

	shsurf->client->send_configure(shsurf->surface, width, height);
}

static void
resize_grab_button(struct weston_pointer_grab *grab,
		   uint32_t time, uint32_t button, uint32_t state_w)
{
	struct weston_resize_grab *resize = (struct weston_resize_grab *) grab;
	struct weston_pointer *pointer = grab->pointer;
	enum wl_pointer_button_state state = state_w;

	if (pointer->button_count == 0 &&
	    state == WL_POINTER_BUTTON_STATE_RELEASED) {
		shell_grab_end(&resize->base);
		free(grab);
	}
}

static void
resize_grab_cancel(struct weston_pointer_grab *grab)
{
	struct weston_resize_grab *resize = (struct weston_resize_grab *) grab;

	shell_grab_end(&resize->base);
	free(grab);
}

static void
popup_grab_focus(struct weston_pointer_grab *grab)
{
	struct weston_pointer *pointer = grab->pointer;
	struct weston_view *view;
	page::shell_seat *shseat =
	    container_of(grab, page::shell_seat, popup_grab.grab);
	struct wl_client *client = shseat->popup_grab.client;
	wl_fixed_t sx, sy;

	view = weston_compositor_pick_view(pointer->seat->compositor,
					   pointer->x, pointer->y,
					   &sx, &sy);

	if (view && view->surface->resource &&
	    wl_resource_get_client(view->surface->resource) == client) {
		weston_pointer_set_focus(pointer, view, sx, sy);
	} else {
		weston_pointer_set_focus(pointer, NULL,
					 wl_fixed_from_int(0),
					 wl_fixed_from_int(0));
	}
}


static void
popup_grab_motion(struct weston_pointer_grab *grab, uint32_t time,
		  wl_fixed_t x, wl_fixed_t y)
{
	struct weston_pointer *pointer = grab->pointer;
	struct wl_resource *resource;
	wl_fixed_t sx, sy;

	if (pointer->focus) {
		weston_view_from_global_fixed(pointer->focus, x, y,
					      &pointer->sx, &pointer->sy);
	}

	weston_pointer_move(pointer, x, y);

	wl_resource_for_each(resource, &pointer->focus_resource_list) {
		weston_view_from_global_fixed(pointer->focus,
					      pointer->x, pointer->y,
					      &sx, &sy);
		wl_pointer_send_motion(resource, time, sx, sy);
	}
}

static void
popup_grab_button(struct weston_pointer_grab *grab,
		  uint32_t time, uint32_t button, uint32_t state_w)
{
	struct wl_resource *resource;
	page::shell_seat *shseat =
	    container_of(grab, page::shell_seat, popup_grab.grab);
	struct wl_display *display = shseat->seat->compositor->wl_display;
	enum wl_pointer_button_state state = state_w;
	uint32_t serial;
	struct wl_list *resource_list;

	resource_list = &grab->pointer->focus_resource_list;
	if (!wl_list_empty(resource_list)) {
		serial = wl_display_get_serial(display);
		wl_resource_for_each(resource, resource_list) {
			wl_pointer_send_button(resource, serial,
					       time, button, state);
		}
	} else if (state == WL_POINTER_BUTTON_STATE_RELEASED &&
		   (shseat->popup_grab.initial_up ||
		    time - shseat->seat->pointer->grab_time > 500)) {
		popup_grab_end(grab->pointer);
	}

	if (state == WL_POINTER_BUTTON_STATE_RELEASED)
		shseat->popup_grab.initial_up = 1;
}

static void
popup_grab_cancel(struct weston_pointer_grab *grab)
{
	popup_grab_end(grab->pointer);
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

const struct weston_touch_grab_interface touch_popup_grab_interface = {
	touch_popup_grab_down,
	touch_popup_grab_up,
	touch_popup_grab_motion,
	touch_popup_grab_frame,
	touch_popup_grab_cancel,
};



void shell_surface_send_popup_done(shell_surface *shsurf)
{
	if (shsurf->shell_surface_is_wl_shell_surface())
		wl_shell_surface_send_popup_done(shsurf->resource);
	else if (shsurf->shell_surface_is_xdg_popup())
		xdg_popup_send_popup_done(shsurf->resource,
					  shsurf->popup.serial);
}


void touch_popup_grab_end(struct weston_touch *touch)
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

void popup_grab_end(struct weston_pointer *pointer)
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


}

