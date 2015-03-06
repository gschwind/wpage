/*
 * popup_alt_tab.hxx
 *
 * copyright (2010-2014) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */


#ifndef DROPDOWN_MENU_HXX_
#define DROPDOWN_MENU_HXX_

#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>

#include <string>
#include <memory>
#include <vector>


#include "utils.hxx"
#include "renderable.hxx"
#include "box.hxx"
#include "region.hxx"
#include "icon_handler.hxx"

namespace page {

template<typename TDATA>
class dropdown_menu_entry_t {

	theme_dropdown_menu_entry_t _theme_data;

	TDATA const _data;

	dropdown_menu_entry_t(cycle_window_entry_t const &);
	dropdown_menu_entry_t & operator=(cycle_window_entry_t const &);

public:
	dropdown_menu_entry_t(TDATA data, std::shared_ptr<icon16> icon, std::string label) :
		_data(data)
	{
		_theme_data.icon = icon;
		_theme_data.label = label;
	}

	~dropdown_menu_entry_t() {

	}

	TDATA const & data() const {
		return _data;
	}

	std::shared_ptr<icon16> icon() const {
		return _theme_data.icon;
	}

	std::string const & label() const {
		return _theme_data.label;
	}

	theme_dropdown_menu_entry_t const & get_theme_item() {
		return _theme_data;
	}

};

template<typename TDATA>
class dropdown_menu_t : public renderable_t {

public:
	using item_t = dropdown_menu_entry_t<TDATA>;

private:
	theme_t * _theme;
	std::vector<std::shared_ptr<item_t>> _items;
	int _selected;

	xcb_pixmap_t _pix;
	cairo_surface_t * _surf;

	i_rect _position;

	xcb_window_t _wid;

	bool _is_durty;

public:

	dropdown_menu_t(theme_t * theme, std::vector<std::shared_ptr<item_t>> items, int x, int y, int width) : _theme(theme) {
		_selected = -1;

		_is_durty = true;

		_items = items;
		_position.x = x;
		_position.y = y;
		_position.w = width;
		_position.h = 24*_items.size();


	}

	~dropdown_menu_t() {
		cairo_surface_destroy(_surf);
	}

	TDATA const & get_selected() {
		return _items[_selected]->data();
	}

	void update_backbuffer() {

		cairo_t * cr = cairo_create(_surf);

		for (unsigned k = 0; k < _items.size(); ++k) {
			update_items_back_buffer(cr, k);
		}

		cairo_destroy(cr);

		expose(i_rect(0,0,_position.w,_position.h));

	}

	void update_items_back_buffer(cairo_t * cr, int n) {
		if (n >= 0 and n < _items.size()) {
			i_rect area(0, 24 * n, _position.w, 24);
			_theme->render_menuentry(cr, _items[n]->get_theme_item(), area, n == _selected);
		}
	}

	void set_selected(int s) {
		if(s >= 0 and s < _items.size() and s != _selected) {
			std::swap(_selected, s);
			cairo_t * cr = cairo_create(_surf);
			update_items_back_buffer(cr, _selected);
			update_items_back_buffer(cr, s);
			cairo_destroy(cr);
			_is_durty = true;

			expose(i_rect(0,0,_position.w,_position.h));
		}
	}

	void update_cursor_position(int x, int y) {
		if (_position.is_inside(x, y)) {
			int s = (int) floor((y - _position.y) / 24.0);
			set_selected(s);
		}
	}

	i_rect const & position() {
		return _position;
	}

	xcb_window_t id() {
		return _wid;
	}

	void expose(region const & r) {
		cairo_surface_t * surf = nullptr; //TODO
		cairo_t * cr = cairo_create(surf);
		for(auto a: r) {
			cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
			cairo_set_source_surface(cr, _surf, 0.0, 0.0);
			cairo_rectangle(cr, a.x, a.y, a.w, a.h);
			cairo_fill(cr);
		}
		cairo_destroy(cr);
		cairo_surface_destroy(surf);
	}

	/**
	 * draw the area of a renderable to the destination surface
	 * @param cr the destination surface context
	 * @param area the area to redraw
	 **/
	virtual void render(cairo_t * cr, region const & area) {
//		cairo_save(cr);
//
//		for (auto const & a : area) {
//			cairo_clip(cr, a);
//			cairo_rectangle(cr, _position.x, _position.y, _position.w, _position.h);
//			cairo_set_source_surface(cr, _surf, _position.x, _position.y);
//			cairo_fill(cr);
//		}
//
//		cairo_restore(cr);
	}

	/**
	 * Derived class must return opaque region for this object,
	 * If unknown it's safe to leave this empty.
	 **/
	virtual region get_opaque_region() {
		return region{_position};
	}

	/**
	 * Derived class must return visible region,
	 * If unknow the whole screen can be returned, but draw will be called each time.
	 **/
	virtual region get_visible_region() {
		return region{_position};
	}

	/**
	 * return currently damaged area (absolute)
	 **/
	virtual region get_damaged()  {
		if(_is_durty) {
			_is_durty = false;
			return region{_position};
		} else {
			return region{};
		}
	}

};

}



#endif /* POPUP_ALT_TAB_HXX_ */
