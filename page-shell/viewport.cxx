/*
 * viewport.cxx
 *
 * copyright (2010-2014) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include <algorithm>
#include <typeinfo>
#include "notebook.hxx"
#include "viewport.hxx"

#include "utils.hxx"

namespace page {

viewport_t::viewport_t(theme_t * theme, i_rect const & area, struct weston_surface * wsurf, struct weston_buffer * buffer) :
		_raw_aera(area),
		_effective_area(area),
		_parent(nullptr),
		_is_hidden(false),
		_is_durty(true),
		_win(XCB_NONE),
		_pix(XCB_NONE),
		_back_surf(nullptr),
		_wsurf(wsurf)
{
	_page_area = i_rect{0, 0, _effective_area.w, _effective_area.h};
	_subtree = nullptr;
	_theme = theme;
	_subtree = new notebook_t(_theme);
	_subtree->set_parent(this);
	_subtree->set_allocation(_page_area);

	weston_buffer_reference(&background_buffer, buffer);
	create_window();

}

void viewport_t::replace(page_component_t * src, page_component_t * by) {
	//printf("replace %p by %p\n", src, by);

	if (_subtree == src) {
		_subtree->set_parent(nullptr);
		_subtree = by;
		_subtree->set_parent(this);
		_subtree->set_allocation(_effective_area);
	} else {
		throw std::runtime_error("viewport: bad child replacement!");
	}
}

void viewport_t::remove(tree_t * src) {
	if(src == _subtree) {
		cout << "WARNING do not use viewport_t::remove to remove subtree element" << endl;
		_subtree = nullptr;
		return;
	}

}

void viewport_t::set_allocation(i_rect const & area) {
	_effective_area = area;
	_page_area = i_rect{0, 0, _effective_area.w, _effective_area.h};
	if(_subtree != nullptr)
		_subtree->set_allocation(_effective_area);
	update_renderable();
}

void viewport_t::set_raw_area(i_rect const & area) {
	_raw_aera = area;
}

i_rect const & viewport_t::raw_area() const {
	return _raw_aera;
}

void viewport_t::get_all_children(std::vector<tree_t *> & out) const {
	if (_subtree != nullptr) {
		out.push_back(_subtree);
		_subtree->get_all_children(out);
	}
}

page_component_t * viewport_t::parent() const {
	return _parent;
}

bool viewport_t::is_visible() {
	return not _is_hidden;
}

std::list<tree_t *> viewport_t::childs() const {
	std::list<tree_t *> ret;

	if (_subtree != nullptr) {
		ret.push_back(_subtree);
	}

	return ret;
}

void viewport_t::raise_child(tree_t * t) {

	if(t != _subtree and t != nullptr) {
		throw exception_t("viewport::raise_child trying to raise a non child tree");
	}

	if(_parent != nullptr and (t == _subtree or t == nullptr)) {
		_parent->raise_child(this);
	}
}

std::string viewport_t::get_node_name() const {
	return _get_node_name<'V'>();
}

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

void viewport_t::set_parent(tree_t * t) {
	throw exception_t("viewport cannot have tree_t as parent");
}

void viewport_t::set_parent(page_component_t * t) {
	_parent = t;
}

i_rect viewport_t::allocation() const {
	return _effective_area;
}

i_rect const & viewport_t::page_area() const {
	return _page_area;
}

void viewport_t::render_legacy(cairo_t * cr, i_rect const & area) const { }

i_rect const & viewport_t::raw_area() const;

void viewport_t::get_all_children(std::vector<tree_t *> & out) const;

void viewport_t::children(std::vector<tree_t *> & out) const {
	if(_subtree != nullptr) {
		out.push_back(_subtree);
	}
}

void viewport_t::hide() {
	_is_hidden = true;

	for(auto i: tree_t::children()) {
		i->hide();
	}

	//destroy_renderable();
}

void viewport_t::show() {
	_is_hidden = false;

	for(auto i: tree_t::children()) {
		i->show();
	}

	update_renderable();

}

i_rect const & viewport_t::raw_area() {
	return _raw_aera;
}

void viewport_t::get_visible_children(std::vector<tree_t *> & out) {
	if (not _is_hidden) {
		out.push_back(this);
		for (auto i : tree_t::children()) {
			i->get_visible_children(out);
		}
	}
}

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

void viewport_t::update_renderable() {

}


void viewport_t::create_window() {

}

void viewport_t::repair_damaged() {

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

//	std::shared_ptr<renderable_surface_t> prepare_render() {
//		_renderable->clear_damaged();
//		_renderable->add_damaged(_damaged);
//		_damaged.clear();
//		return _renderable;
//	}

/* mark renderable_page for redraw */
void viewport_t::mark_durty() {
	_is_durty = true;
}

region const & viewport_t::get_damaged() {
	return _damaged;
}

xcb_window_t viewport_t::wid() {
	return _win;
}

void viewport_t::expose() {
	expose(_page_area);
}

void viewport_t::expose(region const & r) {
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

void viewport_t::render_background() {

//	struct weston_buffer * buffer = weston_buffer_create_local_texture(WL_SHM_FORMAT_ARGB8888, _raw_aera.w, _raw_aera.h);
//	weston_buffer_reference(&background_buffer, buffer);

	cairo_surface_t * surf = get_cairo_surface_for_weston_buffer(
			background_buffer.buffer);

	printf("resource = %p\n", background_buffer.buffer->resource);
	cairo_t * cr = cairo_create(surf);
	cairo_identity_matrix(cr);
	cairo_reset_clip(cr);
	/** draw background **/
	_theme->render_background(cr, this->_raw_aera);

	for(auto i: filter_class<notebook_t>(tree_t::get_all_children())) {
		i->render_legacy(cr, this->_raw_aera);

		printf("xxxx %s\n", i->allocation().to_string().c_str());
		cairo_reset_clip(cr);
		cairo_identity_matrix(cr);
		_draw_crossed_box(cr, i->allocation(), 1.0, 0.0, 0.0);

	}

//	weston_layer_entry_remove(_wsurf->views))
//
//	weston_surface_set_size(surface, x->width, x->height);
//	weston_view_set_position(view, x->x, x->y);
//	/** add the surface to a layer **/
//	weston_layer_entry_insert(&background_real_layer.view_list, &view->layer_link);
//
	struct weston_view * view = container_of(_wsurf->views.next, weston_view, surface_link);
	printf("x = %f, y = %f\n", view->geometry.x, view->geometry.y);
//
//	weston_view_set_position(view, 100, 100);
//	weston_layer_entry_insert(&background_real_layer.view_list, &view->layer_link);

	cairo_destroy(cr);
	cairo_surface_destroy(surf);
	weston_surface_attach(_wsurf, background_buffer.buffer);
	weston_surface_damage(_wsurf);

}

void viewport_t::update_allocation() {
	if(_subtree) {
		_subtree->set_allocation(_raw_aera);
	}
}


}
