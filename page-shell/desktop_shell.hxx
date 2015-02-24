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

#include "desktop-shell-server-protocol.h"

#include "exposay.hxx"
#include "grab_handlers.hxx"


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


	void get_output_work_area(
			     struct weston_output *output,
			     pixman_rectangle32_t *area)
	{
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

	void get_output_panel_size(
			      struct weston_output *output,
			      int *width,
			      int *height)
	{
		struct weston_view *view;

		*width = 0;
		*height = 0;

		if (!output)
			return;

		wl_list_for_each(view, &this->panel_layer.view_list.link, layer_link.link) {
			float x, y;

			if (view->surface->output != output)
				continue;

			switch (this->panel_position) {
			case DESKTOP_SHELL_PANEL_POSITION_TOP:
			case DESKTOP_SHELL_PANEL_POSITION_BOTTOM:

				weston_view_to_global_float(view,
							    view->surface->width, 0,
							    &x, &y);

				*width = (int) x;
				*height = view->surface->height + (int) y;
				return;

			case DESKTOP_SHELL_PANEL_POSITION_LEFT:
			case DESKTOP_SHELL_PANEL_POSITION_RIGHT:
				weston_view_to_global_float(view,
							    0, view->surface->height,
							    &x, &y);

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



};


#endif /* PAGE_SHELL_DESKTOP_SHELL_HXX_ */
