/*
 * unmanaged_window.hxx
 *
 * copyright (2010-2014) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#ifndef CLIENT_NOT_MANAGED_HXX_
#define CLIENT_NOT_MANAGED_HXX_

#include <X11/Xlib.h>

#include <memory>


#include "client_properties.hxx"
#include "client_base.hxx"

#include "renderable.hxx"
#include "renderable_floating_outer_gradien.hxx"
#include "renderable_unmanaged_outer_gradien.hxx"
#include "renderable_pixmap.hxx"

namespace page {

class client_not_managed_t : public client_base_t {
private:

	static unsigned long const UNMANAGED_ORIG_WINDOW_EVENT_MASK =
	XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;

	xcb_atom_t _net_wm_type;

	mutable i_rect _base_position;

	/* avoid copy */
	client_not_managed_t(client_not_managed_t const &);
	client_not_managed_t & operator=(client_not_managed_t const &);

public:

	client_not_managed_t(xcb_atom_t type, std::shared_ptr<client_properties_t> c) :
			client_base_t(c),
			_net_wm_type(type)
	{
		_is_hidden = false;
	}

	~client_not_managed_t() {

	}

	xcb_atom_t net_wm_type() {
		return _net_wm_type;
	}

	bool has_window(xcb_window_t w) const {
		return w == _properties->id();
	}

	virtual std::string get_node_name() const {
		std::string s = _get_node_name<'U'>();
		std::ostringstream oss;
		oss << s << " " << orig();
		oss << " " << "TODO" << " ";

		if(_properties->net_wm_name() != nullptr) {
			oss << " " << *_properties->net_wm_name();
		}

		if(_properties->geometry() != nullptr) {
			oss << " " << _properties->geometry()->width << "x" << _properties->geometry()->height << "+" << _properties->geometry()->x << "+" << _properties->geometry()->y;
		}

		return oss.str();
	}

	virtual void prepare_render(std::vector<std::shared_ptr<renderable_t>> & out, page::time_t const & time) {
		// DONE BY WESTON
	}

	virtual bool need_render(time_t time) {
		return false;
	}

	region visible_area() const {
		i_rect rec{base_position()};
		rec.x -= 4;
		rec.y -= 4;
		rec.w += 8;
		rec.h += 8;
		return rec;
	}

	xcb_window_t base() const {
		return _properties->id();
	}

	xcb_window_t orig() const {
		return _properties->id();
	}

	virtual i_rect const & base_position() const {
		_base_position = _properties->position();
		return _base_position;
	}

	virtual i_rect const & orig_position() const {
		_base_position = _properties->position();
		return _base_position;
	}

	void get_visible_children(std::vector<tree_t *> & out) {
		out.push_back(this);
		for (auto i : tree_t::children()) {
			i->get_visible_children(out);
		}
	}

};

}

#endif /* CLIENT_NOT_MANAGED_HXX_ */
