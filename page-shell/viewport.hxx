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

#include "renderable.hxx"
#include "renderable_surface.hxx"
#include "split.hxx"
#include "theme.hxx"
#include "page_component.hxx"
#include "notebook.hxx"

namespace page {

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

	std::shared_ptr<renderable_surface_t> _renderable;

	theme_t * _theme;

	/** area without considering dock windows **/
	i_rect _raw_aera;

	/** area considering dock windows **/
	i_rect _effective_area;
	i_rect _page_area;
	page_component_t * _subtree;

	viewport_t(viewport_t const & v);
	viewport_t & operator= (viewport_t const &);

public:

	page_component_t * parent() const {
		return _parent;
	}

	viewport_t(theme_t * theme, i_rect const & area);

	~viewport_t() {

	}

	virtual void replace(page_component_t * src, page_component_t * by);
	virtual void remove(tree_t * src);

	notebook_t * get_nearest_notebook();

	virtual void set_allocation(i_rect const & area);

	void set_raw_area(i_rect const & area);
	void set_effective_area(i_rect const & area);

	bool is_visible() {
		return not _is_hidden;
	}

	virtual std::list<tree_t *> childs() const {
		std::list<tree_t *> ret;

		if (_subtree != nullptr) {
			ret.push_back(_subtree);
		}

		return ret;
	}

	void raise_child(tree_t * t) {

		if(t != _subtree and t != nullptr) {
			throw exception_t("viewport::raise_child trying to raise a non child tree");
		}

		if(_parent != nullptr and (t == _subtree or t == nullptr)) {
			_parent->raise_child(this);
		}
	}

	virtual std::string get_node_name() const {
		return _get_node_name<'V'>();
	}

	virtual void prepare_render(std::vector<std::shared_ptr<renderable_t>> & out, page::time_t const & time) {
		if(_is_hidden)
			return;
		_renderable->clear_damaged();
		_renderable->add_damaged(_damaged);
		_damaged.clear();
		out.push_back(_renderable);
		if(_subtree != nullptr) {
			_subtree->prepare_render(out, time);
		}
	}

	void set_parent(tree_t * t) {
		throw exception_t("viewport cannot have tree_t as parent");
	}

	void set_parent(page_component_t * t) {
		_parent = t;
	}

	i_rect allocation() const {
		return _effective_area;
	}

	i_rect const & page_area() const {
		return _page_area;
	}

	void render_legacy(cairo_t * cr, i_rect const & area) const { }

	i_rect const & raw_area() const;

	void get_all_children(std::vector<tree_t *> & out) const;

	void children(std::vector<tree_t *> & out) const {
		if(_subtree != nullptr) {
			out.push_back(_subtree);
		}
	}

	void hide() {
		_is_hidden = true;

		for(auto i: tree_t::children()) {
			i->hide();
		}

		destroy_renderable();
	}

	void show() {
		_is_hidden = false;

		for(auto i: tree_t::children()) {
			i->show();
		}

		update_renderable();

	}

	i_rect const & raw_area() {
		return _raw_aera;
	}

	void get_visible_children(std::vector<tree_t *> & out) {
		if (not _is_hidden) {
			out.push_back(this);
			for (auto i : tree_t::children()) {
				i->get_visible_children(out);
			}
		}
	}

	void destroy_renderable() {

		_renderable = nullptr;

		if(_pix != XCB_NONE) {
			_pix = XCB_NONE;
		}

		if(_back_surf != nullptr) {
			cairo_surface_destroy(_back_surf);
			_back_surf = nullptr;
		}

	}

	void update_renderable() {

	}


	void create_window() {

	}

	void repair_damaged() {

		if(not _is_durty)
			return;

		cairo_t * cr = cairo_create(_back_surf);

		cairo_translate(cr, -_effective_area.x, -_effective_area.y);

		region empty_area{_page_area};

		auto splits = filter_class<split_t>(tree_t::get_all_children());
		for (auto x : splits) {
			x->render_legacy(cr, _page_area);
			empty_area -= x->get_split_bar_area();
		}

		auto notebooks = filter_class<notebook_t>(tree_t::get_all_children());
		for (auto x : notebooks) {
			x->render_legacy(cr, _page_area);
			empty_area -= x->allocation();
		}

		for(auto &b: empty_area) {
			_theme->render_empty(cr, b);
		}

		warn(cairo_get_reference_count(cr) == 1);
		cairo_destroy(cr);

		_is_durty = false;
		_damaged += _page_area;

		return;

	}

	std::shared_ptr<renderable_surface_t> prepare_render() {
		_renderable->clear_damaged();
		_renderable->add_damaged(_damaged);
		_damaged.clear();
		return _renderable;
	}

	/* mark renderable_page for redraw */
	void mark_durty() {
		_is_durty = true;
	}

	region const & get_damaged() {
		return _damaged;
	}

	xcb_window_t wid() {
		return _win;
	}

	void expose() {
		expose(_page_area);
	}

	void expose(region const & r) {
		if(_is_hidden)
			return;

		repair_damaged();

		cairo_surface_t * surf = nullptr; // TODO
		cairo_t * cr = cairo_create(surf);
		for(auto a: r) {
			cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
			cairo_set_source_surface(cr, _back_surf, 0.0, 0.0);
			cairo_rectangle(cr, a.x, a.y, a.w, a.h);
			cairo_fill(cr);
		}

		cairo_destroy(cr);
		cairo_surface_destroy(surf);
	}


};

}

#endif /* VIEWPORT_HXX_ */
