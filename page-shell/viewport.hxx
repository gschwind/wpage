/*
 * viewport.hxx
 *
 * copyright (2010-2014) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#ifndef VIEWPORT_HXX_
#define VIEWPORT_HXX_

#include <memory>
#include <vector>

#include "split.hxx"
#include "theme.hxx"
#include "page_component.hxx"

namespace page {

struct notebook_t;

class viewport_t: public page_component_t {

	static uint32_t const DEFAULT_BUTTON_EVENT_MASK = XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_BUTTON_MOTION;

	page_component_t * _parent;

	region _damaged;

	xcb_pixmap_t _pix;
	xcb_window_t _win;

	bool _is_durty;
	bool _is_hidden;

	/** rendering tabs is time consuming, thus use back buffer **/
	cairo_surface_t * _back_surf;

	theme_t * _theme;

	/** area without considering dock windows **/
	i_rect _raw_aera;

	/** area considering dock windows **/
	i_rect _effective_area;
	i_rect _page_area;
	page_component_t * _subtree;

	struct weston_surface * _wsurf;
	cxx_weston_buffer_reference background_buffer;

	viewport_t(viewport_t const & v);
	viewport_t & operator= (viewport_t const &);

public:

	page_component_t * parent() const;

	viewport_t(theme_t * theme, i_rect const & area, struct weston_surface * wsurf, struct weston_buffer * buffer);

	~viewport_t() {

	}

	virtual void replace(page_component_t * src, page_component_t * by);
	virtual void remove(tree_t * src);

	notebook_t * get_nearest_notebook();

	virtual void set_allocation(i_rect const & area);

	void set_raw_area(i_rect const & area);
	void set_effective_area(i_rect const & area);

	bool is_visible();

	virtual std::list<tree_t *> childs() const;

	void raise_child(tree_t * t);

	virtual std::string get_node_name() const;
//	virtual void prepare_render(std::vector<std::shared_ptr<renderable_t>> & out, page::time_t const & time) {
//		if(_is_hidden)
//			return;
//		_renderable->clear_damaged();
//		_renderable->add_damaged(_damaged);
//		_damaged.clear();
//		out.push_back(_renderable);
//		if(_subtree != nullptr) {
//			_subtree->prepare_render(out, time);
//		}
//	}

	void set_parent(tree_t * t);

	void set_parent(page_component_t * t);

	i_rect allocation() const;

	i_rect const & page_area() const;

	void render_legacy(cairo_t * cr, i_rect const & area) const;

	i_rect const & raw_area() const;

	void get_all_children(std::vector<tree_t *> & out) const;

	void children(std::vector<tree_t *> & out) const;

	void hide();

	void show();

	i_rect const & raw_area();

	void get_visible_children(std::vector<tree_t *> & out);

//	void destroy_renderable() {
//
//		_renderable = nullptr;
//
//		if(_pix != XCB_NONE) {
//			_pix = XCB_NONE;
//		}
//
//		if(_back_surf != nullptr) {
//			cairo_surface_destroy(_back_surf);
//			_back_surf = nullptr;
//		}
//
//	}

	void update_renderable();


	void create_window();

	void repair_damaged();

//	std::shared_ptr<renderable_surface_t> prepare_render() {
//		_renderable->clear_damaged();
//		_renderable->add_damaged(_damaged);
//		_damaged.clear();
//		return _renderable;
//	}

	/* mark renderable_page for redraw */
	void mark_durty();

	region const & get_damaged();

	xcb_window_t wid();

	void expose();

	void expose(region const & r);

	void render_background();


};

}

#endif /* VIEWPORT_HXX_ */
