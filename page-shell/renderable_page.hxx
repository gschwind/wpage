/*
 * renderable_page.hxx
 *
 * copyright (2010-2014) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */


#ifndef RENDERABLE_PAGE_HXX_
#define RENDERABLE_PAGE_HXX_

#include <memory>

#include "theme.hxx"

#include "region.hxx"
#include "renderable_surface.hxx"

namespace page {

/**
 * renderable_page is responsible to render page background (i.e. notebooks and background image)
 **/
class renderable_page_t {
	theme_t * _theme;
	region _damaged;

	xcb_pixmap_t _pix;
	xcb_window_t _win;

	i_rect _position;

	bool _has_alpha;
	bool _is_durty;
	bool _is_visible;

	/** rendering tabs is time consumming, thus use back buffer **/
	cairo_surface_t * _back_surf;

	std::shared_ptr<renderable_surface_t> _renderable;

public:

	renderable_page_t(theme_t * theme, int width,
			int height) {
		_theme = theme;
		_is_durty = true;
		_is_visible = true;
		_has_alpha = false;

		// TODO: create the background surface.

	}

	void update_renderable() {
		// update back surf ?
	}

	~renderable_page_t() {
		cout << "call " << __FUNCTION__ << endl;
		cairo_surface_destroy(_back_surf);
	}

	bool repair_damaged(std::vector<tree_t *> tree) {

		if(not _is_durty)
			return false;

		cairo_t * cr = cairo_create(_back_surf);

		cairo_rectangle(cr, _position.x, _position.y, _position.w, _position.h);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
		cairo_fill(cr);

		i_rect area = _position;
		area.x = 0;
		area.y = 0;

		region empty_area{_position};

		for (auto &j : tree) {
			split_t * rtree = dynamic_cast<split_t *>(j);
			if (rtree != nullptr) {
				rtree->render_legacy(cr, area);
//				empty_area -= rtree->get_split_bar_area();
			}
		}

		for (auto &j : tree) {
			notebook_t * rtree = dynamic_cast<notebook_t *>(j);
			if (rtree != nullptr) {
				rtree->render_legacy(cr, area);
//				empty_area -= rtree->allocation();
			}
		}

//		for(auto &b: empty_area) {
//			_theme->render_empty(cr, b);
//		}

		warn(cairo_get_reference_count(cr) == 1);
		cairo_destroy(cr);

		_is_durty = false;
		_damaged += _position;

		return true;

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

	void move_resize(i_rect const & area) {
		// TODO
	}

	void move(int x, int y) {
		// TODO
	}

	void show() {
		_is_visible = true;
	}

	void hide() {
		_is_visible = false;
	}

	bool is_visible() {
		return _is_visible;
	}

	i_rect const & position() {
		return _position;
	}

	region const & get_damaged() {
		return _damaged;
	}

	xcb_window_t wid() {
		return _win;
	}

	void expose() {
		expose(_position);
	}

	void expose(region const & r) {
		cairo_surface_t * surf = nullptr; //TODO
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


#endif /* RENDERABLE_PAGE_HXX_ */
