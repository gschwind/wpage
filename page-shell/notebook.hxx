/*
 * notebook.hxx
 *
 * copyright (2010-2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#ifndef NOTEBOOK_HXX_
#define NOTEBOOK_HXX_

#include <algorithm>
#include <cmath>
#include <cassert>
#include <memory>

#include "exception.hxx"
#include "surface.hxx"
#include "theme.hxx"
#include "page_event.hxx"
#include "page_component.hxx"

namespace page {

struct img_t {
  unsigned int   width;
  unsigned int   height;
  unsigned int   bytes_per_pixel; /* 3:RGB, 4:RGBA */
  unsigned char  pixel_data[16 * 16 * 4 + 1];
};

class notebook_t : public page_component_t {
	double const XN = 0.0;
	page::time_t const animation_duration{0, 500000000};

	page_component_t * _parent;

	i_rect _allocation;

	theme_t const * _theme;

	/* child stack order */
	std::list<tree_t *> _children;

	page::time_t swap_start;

//	std::shared_ptr<pixmap_t> prev_surf;
//	i_rect prev_loc;
//	std::shared_ptr<renderable_notebook_fading_t> fading_notebook;

	mutable theme_notebook_t theme_notebook;

	bool _is_default;
	bool _is_hidden;

public:

	enum select_e {
		SELECT_NONE,
		SELECT_TAB,
		SELECT_TOP,
		SELECT_BOTTOM,
		SELECT_LEFT,
		SELECT_RIGHT
	};
	// list of client to maintain tab order
	std::list<shell_surface *> _clients;
	shell_surface * _selected;

	// set of map for fast check is window is in this notebook
	set<shell_surface *> _client_map;

	i_rect client_area;

	i_rect button_close;
	i_rect button_vsplit;
	i_rect button_hsplit;
	i_rect button_pop;

	i_rect tab_area;
	i_rect top_area;
	i_rect bottom_area;
	i_rect left_area;
	i_rect right_area;

	i_rect popup_top_area;
	i_rect popup_bottom_area;
	i_rect popup_left_area;
	i_rect popup_right_area;
	i_rect popup_center_area;

	i_rect close_client_area;
	i_rect undck_client_area;

	void set_selected(shell_surface * c);

	void update_client_position(shell_surface * c);

public:
	notebook_t(theme_t const * theme);
	~notebook_t();
	void update_allocation(i_rect & allocation);

	bool process_button_press_event(XEvent const * e);

	void replace(page_component_t * src, page_component_t * by);
	void close(tree_t * src);
	void remove(tree_t * src);

	std::list<shell_surface *> const & get_clients();

	bool add_client(shell_surface * c, bool prefer_activate);
	void remove_client(shell_surface * c);

	void activate_client(shell_surface * x);
	void iconify_client(shell_surface * x);

	i_rect get_new_client_size();

	void select_next();
	void delete_all();

	virtual void unmap_all();
	virtual void map_all();

	notebook_t * get_nearest_notebook();

	virtual i_rect get_absolute_extend();
	virtual region get_area();

	void set_allocation(i_rect const & area);

	void set_parent(tree_t * t) {
		if(t != nullptr) {
			throw exception_t("notebook_t cannot have tree_t has parent");
		} else {
			_parent = nullptr;
		}
	}

	void set_parent(page_component_t * t) {
		_parent = t;
	}

	shell_surface * find_client_tab(int x, int y);

	void update_close_area();

	/*
	 * Compute client size taking in account possible max_width and max_heigth
	 * and the window Hints.
	 * @output width: width result
	 * @output height: height result
	 */

	static void compute_client_size_with_constraint(shell_surface * c,
			unsigned int max_width, unsigned int max_height,
			unsigned int & width, unsigned int & height);

	i_rect compute_client_size(shell_surface * c);
	i_rect const & get_allocation();
	void set_theme(theme_t const * theme);
	std::list<shell_surface const *> clients() const;
	shell_surface const * selected() const;
	bool is_default() const;
	void set_default(bool x);
	std::list<tree_t *> childs() const;
	void raise_child(tree_t * t = nullptr);
	std::string get_node_name() const;
	void render_legacy(cairo_t * cr, i_rect const & area) const;
	void render(cairo_t * cr, time_t time);
	bool need_render(time_t time);

	shell_surface * get_selected();

	virtual void prepare_render(std::vector<std::shared_ptr<renderable_t>> & out, page::time_t const & time);

	i_rect compute_notebook_close_window_position(i_rect const & allocation,
			int number_of_client, int selected_client_index) const;
	i_rect compute_notebook_unbind_window_position(i_rect const & allocation,
			int number_of_client, int selected_client_index) const;
	i_rect compute_notebook_bookmark_position(i_rect const & allocation) const;
	i_rect compute_notebook_vsplit_position(i_rect const & allocation) const;
	i_rect compute_notebook_hsplit_position(i_rect const & allocation) const;
	i_rect compute_notebook_close_position(i_rect const & allocation) const;
	i_rect compute_notebook_menu_position(i_rect const & allocation) const;

	void compute_areas_for_notebook(std::vector<page_event_t> * l) const;

	i_rect allocation() const {
		return _allocation;
	}

	page_component_t * parent() const {
		return _parent;
	}

	void get_all_children(std::vector<tree_t *> & out) const;

	void update_theme_notebook() const;

	void children(std::vector<tree_t *> & out) const {
		out.insert(out.end(), _children.begin(), _children.end());
	}

	void hide() {
		_is_hidden = true;
		for(auto i: tree_t::children()) {
			i->hide();
		}
	}

	void show() {
		_is_hidden = false;
		for(auto i: tree_t::children()) {
			i->show();
		}
	}

	void get_visible_children(std::vector<tree_t *> & out) {
		if (not _is_hidden) {
			out.push_back(this);
			for (auto i : tree_t::children()) {
				i->get_visible_children(out);
			}
		}
	}

	bool has_client(shell_surface * c) {
		return has_key(_client_map, c);
	}

	void configure_client(shell_surface * c) {
		if (c->surface->width != client_area.w
				|| c->surface->height != client_area.h) {
			//c->send_configure(client_area.w, client_area.h);
			wl_array state;
			wl_array_init(&state);
			uint32_t * pi = wl_array_add(&state, 2 * sizeof(uint32_t));
			pi[0] = XDG_SURFACE_STATE_ACTIVATED;
			pi[1] = XDG_SURFACE_STATE_MAXIMIZED;
			xdg_surface_send_configure(c->resource, client_area.w,
					client_area.h, &state,
					wl_display_next_serial(c->shell->compositor->wl_display));
		}
	}

};

}

#endif /* NOTEBOOK_HXX_ */
