/*
 * surface.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "surface.hxx"

#include <cassert>
#include "exception.hxx"
#include "shell.h"

#include "wayland-server.h"
#include "compositor.h"
#include "desktop-shell-server-protocol.h"
#include "xdg-shell-server-protocol.h"

#include "protocols_implementation.hxx"
#include "grab_handlers.hxx"

#include "workspace.hxx"
#include "notebook.hxx"

shell_surface::shell_surface(
		shell_client *owner,
		void *shell,
		weston_surface *surface,
		const weston_shell_client *client) :
	resource{nullptr},
	destroy_signal{0},
	owner{nullptr},
	surface{nullptr},
	view{nullptr},
	last_width{0}, last_height{0},
	_parent{nullptr},
	//children_list{0},  /* child surfaces of this one */
	//children_link{0},  /* sibling surfaces of this one */
	shell{nullptr},
	type{SHELL_SURFACE_NONE},
	title{nullptr}, class_{nullptr},
	saved_x{0}, saved_y{0},
	saved_width{0}, saved_height{0},
	saved_position_valid{false},
	saved_size_valid{false},
	saved_rotation_valid{false},
	unresponsive{0}, grabbed{0},
	resize_edges{0U},
	rotation{0},
	popup{0},
	transient{0},
	fullscreen{},
	workspace_transform{0},
	fullscreen_output{nullptr},
	output{nullptr},
	link{nullptr},
	client{nullptr},
	state{0}, next_state{0}, requested_state{0}, /* surface states */
	state_changed{false},
	state_requested{false},
	geometry{0}, next_geometry{0},
	has_set_geometry{false}, has_next_geometry{false},
	focus_count{0},
	resource_destroy_listener{this, &shell_surface::handle_resource_destroy},
	surface_destroy_listener{this, &shell_surface::shell_handle_surface_destroy}
{

	if (surface->configure) {
		weston_log("surface->configure already set\n");
		throw exception_t{"surface->configure already set\n"};
	}

	this->view = weston_view_create(surface);
	if (!this->view) {
		weston_log("no memory to allocate shell surface\n");
		throw exception_t{"no memory to allocate shell surface\n"};
	}

	surface->configure = shell_surface_configure;
	surface->configure_private = this;

	wl_resource_add_destroy_listener(surface->resource,
			&this->resource_destroy_listener.listener);
	this->owner = reinterpret_cast<page::shell_client*>(owner);

	this->shell = (struct desktop_shell *) shell;
	this->unresponsive = 0;
	this->saved_position_valid = false;
	this->saved_size_valid = false;
	this->saved_rotation_valid = false;
	this->surface = surface;
	this->fullscreen.type = WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT;
	this->fullscreen.framerate = 0;
	this->fullscreen.black_view = NULL;
	wl_list_init(&this->fullscreen.transform.link);

	this->output = get_default_output(this->shell->compositor);

	wl_signal_init(&this->destroy_signal);
	wl_signal_add(&surface->destroy_signal, &this->surface_destroy_listener.listener);

	/* init link so its safe to always remove it in destroy_shell_surface */
	wl_list_init(&this->link);
	wl_list_init(&this->popup.grab_link);

	/* empty when not in use */
	wl_list_init(&this->rotation.transform.link);
	weston_matrix_init(&this->rotation.rotation);

	wl_list_init(&this->workspace_transform.link);

	//wl_list_init(&this->children_link);
	//wl_list_init(&this->children_list);
	this->_parent = NULL;

	this->type = SHELL_SURFACE_NONE;

	this->client = client;

}

/**
 * Call on surface commit.
 */
void
shell_surface::shell_surface_configure(struct weston_surface *es, int32_t sx, int32_t sy)
{
	shell_surface *shsurf = shell_surface::get_shell_surface(es);
	struct desktop_shell *shell;
	int type_changed = 0;

	assert(shsurf);

	shell = shsurf->shell;

	if (!weston_surface_is_mapped(es) &&
	    !wl_list_empty(&shsurf->popup.grab_link)) {
		shsurf->remove_popup_grab();
	}

	if (es->width == 0)
		return;

	if (shsurf->has_next_geometry) {
		shsurf->geometry = shsurf->next_geometry;
		shsurf->has_next_geometry = false;
		shsurf->has_set_geometry = true;
	} else if (!shsurf->has_set_geometry) {
		surface_subsurfaces_boundingbox(shsurf->surface,
						&shsurf->geometry.x,
						&shsurf->geometry.y,
						&shsurf->geometry.width,
						&shsurf->geometry.height);
	}

	if (shsurf->state_changed) {
		shsurf->set_surface_type();
		type_changed = 1;
	}

	if (!weston_surface_is_mapped(es)) {
		shell->map(shsurf, sx, sy);
	} else if (type_changed || sx != 0 || sy != 0 ||
		   shsurf->last_width != es->width ||
		   shsurf->last_height != es->height) {
		float from_x, from_y;
		float to_x, to_y;

		if (shsurf->resize_edges) {
			sx = 0;
			sy = 0;
		}

		if (shsurf->resize_edges & WL_SHELL_SURFACE_RESIZE_LEFT)
			sx = shsurf->last_width - es->width;
		if (shsurf->resize_edges & WL_SHELL_SURFACE_RESIZE_TOP)
			sy = shsurf->last_height - es->height;

		shsurf->last_width = es->width;
		shsurf->last_height = es->height;

		weston_view_to_global_float(shsurf->view, 0, 0, &from_x, &from_y);
		weston_view_to_global_float(shsurf->view, sx, sy, &to_x, &to_y);
		shell->configure(es,
			  shsurf->view->geometry.x + to_x - from_x,
			  shsurf->view->geometry.y + to_y - from_y);

	}

	notebook_t * n = dynamic_cast<notebook_t*>(shsurf->parent());
	if(n) {

		n->configure_client(shsurf);
		n->update_client_position(shsurf);
	}

}

void shell_surface::handle_resource_destroy()
{
	if (!weston_surface_is_mapped(this->surface))
		return;

	this->surface->ref_count++;

	pixman_region32_fini(&this->surface->pending.input);
	pixman_region32_init(&this->surface->pending.input);
	pixman_region32_fini(&this->surface->input);
	pixman_region32_init(&this->surface->input);
	if (this->shell->win_close_animation_type == ANIMATION_FADE) {
		weston_fade_run(this->view, 1.0, 0.0, 300.0,
				fade_out_done, this);
	} else {
		weston_surface_destroy(this->surface);
	}

	notebook_t * n = dynamic_cast<notebook_t*>(parent());
	if(n) {
		n->remove_client(this);
	}
}

void
shell_surface::shell_handle_surface_destroy()
{
	/** will call resource destruction **/
	if (this->resource)
		wl_resource_destroy(this->resource);
	/** self destruction **/
	delete this;
}

shell_surface * shell_surface::get_shell_surface(weston_surface *surface)
{
	if (surface->configure == shell_surface_configure)
		return reinterpret_cast<shell_surface*>(surface->configure_private);
	else
		return nullptr;
}

void shell_surface::remove_popup_grab()
{
	struct shell_seat *shseat = this->popup.shseat;

	wl_list_remove(&this->popup.grab_link);
	wl_list_init(&this->popup.grab_link);
	if (wl_list_empty(&shseat->popup_grab.surfaces_list)) {
		if (shseat->popup_grab.type == shell_seat::POINTER) {
			weston_pointer_end_grab(shseat->popup_grab.grab.pointer);
			shseat->popup_grab.grab.interface = NULL;
		} else if (shseat->popup_grab.type == shell_seat::TOUCH) {
			weston_touch_end_grab(shseat->popup_grab.touch_grab.touch);
			shseat->popup_grab.touch_grab.interface = NULL;
		}
	}
}


void shell_surface::set_surface_type()
{
	struct weston_surface *pes = this->_parent;
	struct weston_view *pev = get_default_view(pes);

	reset_surface_type();

	this->state = this->next_state;
	this->state_changed = false;

	switch (this->type) {
	case SHELL_SURFACE_TOPLEVEL:
		if (this->state.maximized || this->state.fullscreen) {
			set_full_output();
		} else if (this->state.relative && pev) {
			weston_view_set_position(this->view,
						 pev->geometry.x + this->transient.x,
						 pev->geometry.y + this->transient.y);
		}
		break;

	case SHELL_SURFACE_XWAYLAND:
		weston_view_set_position(this->view, this->transient.x,
					 this->transient.y);
		break;

	case SHELL_SURFACE_POPUP:
	case SHELL_SURFACE_NONE:
	default:
		break;
	}

	/* Update the surface’s layer. */
	shell_surface_update_layer();
}

shell_surface::~shell_surface()
{
	struct shell_surface *child, *next;

	wl_signal_emit(&this->destroy_signal, this);

	if (!wl_list_empty(&this->popup.grab_link)) {
		remove_popup_grab();
	}

	if (this->fullscreen.type == WL_SHELL_SURFACE_FULLSCREEN_METHOD_DRIVER &&
	    shell_surface_is_top_fullscreen())
		restore_output_mode (this->fullscreen_output);

	if (this->fullscreen.black_view)
		weston_surface_destroy(this->fullscreen.black_view->surface);

	/* As destroy_resource() use wl_list_for_each_safe(),
	 * we can always remove the listener.
	 */
	wl_list_remove(&this->surface_destroy_listener.listener.link);
	this->surface->configure = NULL;
	free(this->title);

	weston_view_destroy(this->view);

	if(tree_parent)
		tree_parent->remove(this);
	//wl_list_remove(&this->children_link);
	for(auto child: _children)
		child->shell_surface_set_parent(nullptr);

	wl_list_remove(&this->link);

}

/* This is only ever called from set_surface_type(), so there’s no need to
 * update layer_links here, since they’ll be updated when we return. */
int shell_surface::reset_surface_type()
{
	if (this->state.fullscreen)
		unset_fullscreen();
	if (this->state.maximized)
		unset_maximized();

	return 0;
}

void shell_surface::unset_fullscreen()
{
	/* Unset the fullscreen output, driver configuration and transforms. */
	if (this->fullscreen.type == WL_SHELL_SURFACE_FULLSCREEN_METHOD_DRIVER &&
	    shell_surface_is_top_fullscreen()) {
		restore_output_mode(this->fullscreen_output);
	}

	this->fullscreen.type = WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT;
	this->fullscreen.framerate = 0;

	wl_list_remove(&this->fullscreen.transform.link);
	wl_list_init(&this->fullscreen.transform.link);

	if (this->fullscreen.black_view)
		weston_surface_destroy(this->fullscreen.black_view->surface);
	this->fullscreen.black_view = NULL;

	if (this->saved_position_valid)
		weston_view_set_position(this->view,
					 this->saved_x, this->saved_y);
	else
		weston_view_set_initial_position(this->view, this->shell);

	if (this->saved_rotation_valid) {
		wl_list_insert(&this->view->geometry.transformation_list,
		               &this->rotation.transform.link);
		this->saved_rotation_valid = false;
	}

	/* Layer is updated in set_surface_type(). */
}


void
shell_surface::unset_maximized()
{
	/* undo all maximized things here */
	this->output = get_default_output(this->surface->compositor);

	if (this->saved_position_valid)
		weston_view_set_position(this->view,
					 this->saved_x, this->saved_y);
	else
		weston_view_set_initial_position(this->view, this->shell);

	if (this->saved_rotation_valid) {
		wl_list_insert(&this->view->geometry.transformation_list,
			       &this->rotation.transform.link);
		this->saved_rotation_valid = false;
	}

	/* Layer is updated in set_surface_type(). */
}

bool
shell_surface::shell_surface_is_top_fullscreen()
{
	struct desktop_shell *shell;
	struct weston_view *top_fs_ev;

	shell = shell_surface_get_shell();

	if (wl_list_empty(&shell->fullscreen_layer.view_list.link))
		return false;

	top_fs_ev = container_of(shell->fullscreen_layer.view_list.link.next,
			         struct weston_view,
				 layer_link.link);
	return (this == shell_surface::get_shell_surface(top_fs_ev->surface));
}

void
shell_surface::set_full_output()
{
	this->saved_x = this->view->geometry.x;
	this->saved_y = this->view->geometry.y;
	this->saved_width = this->surface->width;
	this->saved_height = this->surface->height;
	this->saved_size_valid = true;
	this->saved_position_valid = true;

	if (!wl_list_empty(&this->rotation.transform.link)) {
		wl_list_remove(&this->rotation.transform.link);
		wl_list_init(&this->rotation.transform.link);
		weston_view_geometry_dirty(this->view);
		this->saved_rotation_valid = true;
	}
}

/* Update the surface’s layer. Mark both the old and new views as having dirty
 * geometry to ensure the changes are redrawn.
 *
 * If any child surfaces exist and are mapped, ensure they’re in the same layer
 * as this surface. */
void
shell_surface::shell_surface_update_layer()
{
	struct weston_layer_entry *new_layer_link;

	new_layer_link = shell_surface_calculate_layer_link();

	if (new_layer_link == NULL)
		return;
	if (new_layer_link == &this->view->layer_link)
		return;

	weston_view_geometry_dirty(this->view);
	weston_layer_entry_remove(&this->view->layer_link);
	weston_layer_entry_insert(new_layer_link, &this->view->layer_link);
	weston_view_geometry_dirty(this->view);
	weston_surface_damage(this->surface);

	shell_surface_update_child_surface_layers();
}

desktop_shell *
shell_surface::shell_surface_get_shell()
{
	return this->shell;
}

void shell_surface::shell_surface_set_parent(weston_surface *parent)
{
	this->_parent = parent;

	if(tree_parent)
		tree_parent->remove(this);
	//wl_list_remove(&this->children_link);
	//wl_list_init(&this->children_link);

	/* Insert into the parent surface’s child list. */
	if (parent != NULL) {
		shell_surface *parent_shsurf = shell_surface::get_shell_surface(parent);
		if (parent_shsurf != NULL)
			parent_shsurf->_children.push_back(this);
			//wl_list_insert(&parent_shsurf->children_list,
			//              &this->children_link);
	}
}

/* The surface will be inserted into the list immediately after the link
 * returned by this function (i.e. will be stacked immediately above the
 * returned link). */
weston_layer_entry * shell_surface::shell_surface_calculate_layer_link ()
{
	page::workspace *ws;
	struct weston_view *parent;

	switch (this->type) {
	case SHELL_SURFACE_XWAYLAND:
		return &this->shell->fullscreen_layer.view_list;

	case SHELL_SURFACE_NONE:
		return NULL;

	case SHELL_SURFACE_POPUP:
	case SHELL_SURFACE_TOPLEVEL:
		if (this->state.fullscreen && !this->state.lowered) {
			return &this->shell->fullscreen_layer.view_list;
		} else if (this->_parent) {
			/* Move the surface to its parent layer so
			 * that surfaces which are transient for
			 * fullscreen surfaces don't get hidden by the
			 * fullscreen surfaces. */

			/* TODO: Handle a parent with multiple views */
			parent = get_default_view(this->_parent);
			if (parent)
				return container_of(parent->layer_link.link.prev,
						    struct weston_layer_entry, link);
		}

		/* Move the surface to a normal workspace layer so that surfaces
		 * which were previously fullscreen or transient are no longer
		 * rendered on top. */
		ws = get_current_workspace(this->shell);
		return &ws->layer.view_list;
	}

	assert(0 && "Unknown shell surface type");
}

void shell_surface::shell_surface_update_child_surface_layers()
{
	struct weston_layer_entry *prev;

	/* Move the child layers to the same workspace as shsurf. They will be
	 * stacked above shsurf. */
	for(auto child: _children) {
		if (this->view->layer_link.link.prev != &child->view->layer_link.link) {
			weston_view_damage_below(child->view);
			weston_view_geometry_dirty(child->view);
			prev = container_of(this->view->layer_link.link.prev,
					    struct weston_layer_entry, link);
			weston_layer_entry_remove(&child->view->layer_link);
			weston_layer_entry_insert(prev,
						  &child->view->layer_link);
			weston_view_geometry_dirty(child->view);
			weston_surface_damage(child->surface);

			/* Recurse. We don’t expect this to recurse very far (if
			 * at all) because that would imply we have transient
			 * (or popup) children of transient surfaces, which
			 * would be unusual. */
			child->shell_surface_update_child_surface_layers();
		}
	}
}

bool shell_surface::shell_surface_is_wl_shell_surface()
{
	/* A shell surface without a resource is created from xwayland
	 * and is considered a wl_shell surface for now. */

	return this->resource == nullptr ||
		wl_resource_instance_of(this->resource,
					&wl_shell_surface_interface,
					&shell_surface_implementation);
}

bool
shell_surface::shell_surface_is_xdg_surface()
{
	return this->resource &&
		wl_resource_instance_of(this->resource,
					&xdg_surface_interface,
					&xdg_surface_implementation);
}

void shell_surface::common_surface_move(wl_resource *resource, wl_resource *seat_resource, uint32_t serial)
{
	auto seat = reinterpret_cast<weston_seat*>(wl_resource_get_user_data(seat_resource));
	auto shsurf = reinterpret_cast<shell_surface*>(wl_resource_get_user_data(resource));
	weston_surface *surface;

	if (seat->pointer &&
	    seat->pointer->focus &&
	    seat->pointer->button_count > 0 &&
	    seat->pointer->grab_serial == serial) {
		surface = weston_surface_get_main_surface(seat->pointer->focus->surface);
		if ((surface == shsurf->surface) &&
		    (shsurf->surface_move(seat, 1) < 0))
			wl_resource_post_no_memory(resource);
	} else if (seat->touch &&
		   seat->touch->focus &&
		   seat->touch->grab_serial == serial) {
		surface = weston_surface_get_main_surface(seat->touch->focus->surface);
		if ((surface == shsurf->surface) &&
		    (shsurf->surface_touch_move(seat) < 0))
			wl_resource_post_no_memory(resource);
	}
}

int shell_surface::surface_move(weston_seat *seat, int client_initiated)
{
	struct weston_move_grab *move;

	if (!this)
		return -1;

	if (this->grabbed ||
	    this->state.fullscreen || this->state.maximized)
		return 0;

	move = reinterpret_cast<weston_move_grab*>(malloc(sizeof *move));
	if (!move)
		return -1;

	move->dx = wl_fixed_from_double(this->view->geometry.x) -
			seat->pointer->grab_x;
	move->dy = wl_fixed_from_double(this->view->geometry.y) -
			seat->pointer->grab_y;
	move->client_initiated = client_initiated;

	shell_grab_start(&move->base, &move_grab_interface, this,
			 seat->pointer, DESKTOP_SHELL_CURSOR_MOVE);

	return 0;
}

int shell_surface::surface_touch_move(weston_seat *seat)
{
	struct weston_touch_move_grab *move;

	if (!this)
		return -1;

	if (this->state.fullscreen || this->state.maximized)
		return 0;

	move = reinterpret_cast<weston_touch_move_grab*>(malloc(sizeof *move));
	if (!move)
		return -1;

	move->active = 1;
	move->dx = wl_fixed_from_double(this->view->geometry.x) -
			seat->touch->grab_x;
	move->dy = wl_fixed_from_double(this->view->geometry.y) -
			seat->touch->grab_y;

	shell_touch_grab_start(&move->base, &touch_move_grab_interface, this,
			       seat->touch);

	return 0;
}

void shell_surface::shell_destroy_shell_surface(wl_resource *resource)
{
	auto shsurf = reinterpret_cast<shell_surface*>(wl_resource_get_user_data(resource));
	if (!wl_list_empty(&shsurf->popup.grab_link))
		shsurf->remove_popup_grab();
	shsurf->resource = nullptr;
}

void shell_surface::send_configure_for_surface()
{
	int32_t width, height;
	shell_surface::surface_state *state;

	if (this->state_requested)
		state = &this->requested_state;
	else if (this->state_changed)
		state = &this->next_state;
	else
		state = &this->state;

	if (state->fullscreen) {
		width = this->output->width;
		height = this->output->height;
	} else if (state->maximized) {
		struct desktop_shell *shell;
		pixman_rectangle32_t area;

		shell = this->shell_surface_get_shell();
		shell->get_output_work_area(this->output, &area);

		width = area.width;
		height = area.height;
	} else {
		//width = 157;
		//height = 277;
	}

	this->client->send_configure(this->surface, width, height);
}

void shell_surface::send_configure(int width, int height) {
	this->client->send_configure(this->surface, width, height);
}

void shell_surface::surface_rotate(struct weston_seat *seat)
{
	struct rotate_grab *rotate;
	float dx, dy;
	float r;

	rotate = reinterpret_cast<rotate_grab *>(malloc(sizeof *rotate));
	if (!rotate)
		return;

	weston_view_to_global_float(this->view,
				    this->surface->width * 0.5f,
				    this->surface->height * 0.5f,
				    &rotate->center.x, &rotate->center.y);

	dx = wl_fixed_to_double(seat->pointer->x) - rotate->center.x;
	dy = wl_fixed_to_double(seat->pointer->y) - rotate->center.y;
	r = sqrtf(dx * dx + dy * dy);
	if (r > 20.0f) {
		struct weston_matrix inverse;

		weston_matrix_init(&inverse);
		weston_matrix_rotate_xy(&inverse, dx / r, -dy / r);
		weston_matrix_multiply(&this->rotation.rotation, &inverse);

		weston_matrix_init(&rotate->rotation);
		weston_matrix_rotate_xy(&rotate->rotation, dx / r, dy / r);
	} else {
		weston_matrix_init(&this->rotation.rotation);
		weston_matrix_init(&rotate->rotation);
	}

	shell_grab_start(&rotate->base, &rotate_grab_interface, this,
			 seat->pointer, DESKTOP_SHELL_CURSOR_ARROW);
}

void shell_surface::shell_surface_state_changed()
{
	if (this->shell_surface_is_xdg_surface())
		this->send_configure_for_surface();
}


void shell_surface::common_surface_resize(struct wl_resource *resource,
		      struct wl_resource *seat_resource, uint32_t serial,
		      uint32_t edges)
{
	struct weston_seat *seat = reinterpret_cast<struct weston_seat *>(wl_resource_get_user_data(seat_resource));
	shell_surface *shsurf = reinterpret_cast<shell_surface *>(wl_resource_get_user_data(resource));
	struct weston_surface *surface;

	if (seat->pointer == NULL ||
	    seat->pointer->button_count == 0 ||
	    seat->pointer->grab_serial != serial ||
	    seat->pointer->focus == NULL)
		return;

	surface = weston_surface_get_main_surface(seat->pointer->focus->surface);
	if (surface != shsurf->surface)
		return;

	if (shsurf->surface_resize(seat, edges) < 0)
		wl_resource_post_no_memory(resource);
}

int shell_surface::surface_resize(struct weston_seat *seat, uint32_t edges)
{
	struct weston_resize_grab *resize;
	const unsigned resize_topbottom =
		WL_SHELL_SURFACE_RESIZE_TOP | WL_SHELL_SURFACE_RESIZE_BOTTOM;
	const unsigned resize_leftright =
		WL_SHELL_SURFACE_RESIZE_LEFT | WL_SHELL_SURFACE_RESIZE_RIGHT;
	const unsigned resize_any = resize_topbottom | resize_leftright;

	if (this->grabbed ||
	    this->state.fullscreen || this->state.maximized)
		return 0;

	/* Check for invalid edge combinations. */
	if (edges == WL_SHELL_SURFACE_RESIZE_NONE || edges > resize_any ||
	    (edges & resize_topbottom) == resize_topbottom ||
	    (edges & resize_leftright) == resize_leftright)
		return 0;

	resize = reinterpret_cast<weston_resize_grab*>(malloc(sizeof *resize));
	if (!resize)
		return -1;

	resize->edges = edges;

	resize->width = this->geometry.width;
	resize->height = this->geometry.height;

	this->resize_edges = edges;
	this->shell_surface_state_changed();
	shell_grab_start(&resize->base, &resize_grab_interface, this,
			 seat->pointer, edges);

	return 0;
}

void shell_surface::surface_clear_next_states()
{
	this->next_state.maximized = false;
	this->next_state.fullscreen = false;
	if ((this->next_state.maximized != this->state.maximized) ||
	    (this->next_state.fullscreen != this->state.fullscreen))
		this->state_changed = true;
}


void shell_surface::shell_surface_set_output(struct weston_output *output)
{
	struct weston_surface *es = this->surface;

	/* get the default output, if the client set it as NULL
	   check whether the ouput is available */
	if (output)
		this->output = output;
	else if (es->output)
		this->output = es->output;
	else
		this->output = get_default_output(es->compositor);
}

void shell_surface::set_popup(
          struct weston_surface *parent,
          struct weston_seat *seat,
          uint32_t serial,
          int32_t x,
          int32_t y)
{
	assert(parent != NULL);

	this->popup.shseat = shell_seat::get_shell_seat(seat);
	this->popup.serial = serial;
	this->popup.x = x;
	this->popup.y = y;

	this->type = SHELL_SURFACE_POPUP;
}

void shell_surface::set_fullscreen(
	       uint32_t method,
	       uint32_t framerate,
	       struct weston_output *output)
{
	this->shell_surface_set_output(output);
	this->type = SHELL_SURFACE_TOPLEVEL;

	this->fullscreen_output = this->output;
	this->fullscreen.type = method;
	this->fullscreen.framerate = framerate;

	this->send_configure_for_surface();
}

bool shell_surface::shell_surface_is_xdg_popup()
{
	return wl_resource_instance_of(this->resource,
				       &xdg_popup_interface,
				       &xdg_popup_implementation);
}


void shell_surface::shell_surface_lose_keyboard_focus()
{
	if (--this->focus_count == 0)
		this->shell_surface_state_changed();
}

void shell_surface::shell_surface_gain_keyboard_focus()
{
	if (this->focus_count++ == 0)
		this->shell_surface_state_changed();
}


void shell_surface::shell_map_fullscreen()
{
	shell_configure_fullscreen();
}

/* Create black surface and append it to the associated fullscreen surface.
 * Handle size dismatch and positioning according to the method. */
void shell_surface::shell_configure_fullscreen()
{
	struct weston_output *output = this->fullscreen_output;
	struct weston_surface *surface = this->surface;
	struct weston_matrix *matrix;
	float scale, output_aspect, surface_aspect, x, y;
	int32_t surf_x, surf_y, surf_width, surf_height;

	if (this->fullscreen.type != WL_SHELL_SURFACE_FULLSCREEN_METHOD_DRIVER)
		restore_output_mode(output);

	/* Reverse the effect of lower_fullscreen_layer() */
	weston_layer_entry_remove(&this->view->layer_link);
	weston_layer_entry_insert(&this->shell->fullscreen_layer.view_list, &this->view->layer_link);

	shell_ensure_fullscreen_black_view();

	surface_subsurfaces_boundingbox(this->surface, &surf_x, &surf_y,
	                                &surf_width, &surf_height);

	switch (this->fullscreen.type) {
	case WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT:
		if (surface->buffer_ref.buffer)
			center_on_output(this->view, this->fullscreen_output);
		break;
	case WL_SHELL_SURFACE_FULLSCREEN_METHOD_SCALE:
		/* 1:1 mapping between surface and output dimensions */
		if (output->width == surf_width &&
			output->height == surf_height) {
			weston_view_set_position(this->view,
						 output->x - surf_x,
						 output->y - surf_y);
			break;
		}

		matrix = &this->fullscreen.transform.matrix;
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
		wl_list_remove(&this->fullscreen.transform.link);
		wl_list_insert(&this->view->geometry.transformation_list,
			       &this->fullscreen.transform.link);
		x = output->x + (output->width - surf_width * scale) / 2 - surf_x;
		y = output->y + (output->height - surf_height * scale) / 2 - surf_y;
		weston_view_set_position(this->view, x, y);

		break;
	case WL_SHELL_SURFACE_FULLSCREEN_METHOD_DRIVER:
		if (shell_surface_is_top_fullscreen()) {
			struct weston_mode mode = {0,
				surf_width * surface->buffer_viewport.buffer.scale,
				surf_height * surface->buffer_viewport.buffer.scale,
				this->fullscreen.framerate};

			if (weston_output_mode_switch_to_temporary(output, &mode,
					surface->buffer_viewport.buffer.scale) == 0) {
				weston_view_set_position(this->view,
							 output->x - surf_x,
							 output->y - surf_y);
				this->fullscreen.black_view->surface->width = output->width;
				this->fullscreen.black_view->surface->height = output->height;
				weston_view_set_position(this->fullscreen.black_view,
							 output->x - surf_x,
							 output->y - surf_y);
				break;
			} else {
				restore_output_mode(output);
				center_on_output(this->view, output);
			}
		}
		break;
	case WL_SHELL_SURFACE_FULLSCREEN_METHOD_FILL:
		center_on_output(this->view, output);
		break;
	default:
		break;
	}
}

/* no-op func for checking black surface */
void black_surface_configure(struct weston_surface *es, int32_t sx, int32_t sy) { }

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

	surface->configure = ::black_surface_configure;
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

void shell_surface::shell_ensure_fullscreen_black_view()
{
	struct weston_output *output = this->fullscreen_output;

	assert(this->state.fullscreen);

	if (!this->fullscreen.black_view)
		this->fullscreen.black_view =
			create_black_surface(this->surface->compositor,
			                     this->surface,
			                     output->x, output->y,
			                     output->width,
			                     output->height);

	weston_view_geometry_dirty(this->fullscreen.black_view);
	weston_layer_entry_remove(&this->fullscreen.black_view->layer_link);
	weston_layer_entry_insert(&this->view->layer_link,
				  &this->fullscreen.black_view->layer_link);
	weston_view_geometry_dirty(this->fullscreen.black_view);
	weston_surface_damage(this->surface);

	this->state.lowered = false;
}

void shell_surface::shell_map_popup()
{
	page::shell_seat *shseat = this->popup.shseat;
	struct weston_view *parent_view = get_default_view(this->_parent);

	this->surface->output = parent_view->output;
	this->view->output = parent_view->output;

	weston_view_set_transform_parent(this->view, parent_view);
	weston_view_set_position(this->view, this->popup.x, this->popup.y);
	weston_view_update_transform(this->view);

	if (shseat->seat->pointer &&
	    shseat->seat->pointer->grab_serial == this->popup.serial) {
		this->add_popup_grab(shseat, page::shell_seat::POINTER);
	} else if (shseat->seat->touch &&
	           shseat->seat->touch->grab_serial == this->popup.serial) {
		this->add_popup_grab(shseat, page::shell_seat::TOUCH);
	} else {
		shell_surface_send_popup_done(this);
		shseat->popup_grab.client = NULL;
	}
}


void shell_surface::add_popup_grab(page::shell_seat *shseat, int32_t type)
{
	struct weston_seat *seat = shseat->seat;

	if (wl_list_empty(&shseat->popup_grab.surfaces_list)) {
		shseat->popup_grab.type = type;
		shseat->popup_grab.client = wl_resource_get_client(this->resource);

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

		wl_list_insert(&shseat->popup_grab.surfaces_list, &this->popup.grab_link);

		if (type == page::shell_seat::POINTER)
			weston_pointer_start_grab(seat->pointer, &shseat->popup_grab.grab);
		else if (type == page::shell_seat::TOUCH)
			weston_touch_start_grab(seat->touch, &shseat->popup_grab.touch_grab);
	} else {
		wl_list_insert(&shseat->popup_grab.surfaces_list, &this->popup.grab_link);
	}
}

void shell_surface::remove_popup_grab2()
{
	page::shell_seat *shseat = this->popup.shseat;

	wl_list_remove(&this->popup.grab_link);
	wl_list_init(&this->popup.grab_link);
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

auto shell_surface::parent() const -> tree_t * {
	return tree_parent;
}

auto shell_surface::get_node_name() const -> std::string {
	return tree_t::_get_node_name<'M'>();
}

auto shell_surface::raise_child(tree_t * t = nullptr) -> void {

}

auto shell_surface::remove(tree_t * t) -> void {

}

auto shell_surface::set_parent(tree_t * parent) -> void {
	tree_parent = parent;
}

auto shell_surface::children(std::vector<tree_t *> & out) const -> void {
	/* TODO */
}

auto shell_surface::get_all_children(std::vector<tree_t *> & out) const -> void {

}

auto shell_surface::get_visible_children(std::vector<tree_t *> & out) -> void {

}

auto shell_surface::hide() -> void {
	/* TODO remove */
}

auto shell_surface::show() -> void {
	/* TODO remove */
}

auto shell_surface::prepare_render(std::vector<std::shared_ptr<renderable_t>> & out, page::time_t const & time) -> void {
	/* TODO remove */
}



