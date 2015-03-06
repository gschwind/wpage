/*
 * client_properties.hxx
 *
 * copyright (2014) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#ifndef CLIENT_PROPERTIES_HXX_
#define CLIENT_PROPERTIES_HXX_

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include <xcb/xcb.h>

#include <limits>
#include <utility>
#include <vector>
#include <list>
#include <iostream>
#include <set>
#include <algorithm>


#include "utils.hxx"
#include "region.hxx"
#include "motif_hints.hxx"

#include "properties.hxx"

namespace page {

class client_properties_t {
private:
	xcb_window_t                 _id;

	xcb_atom_t                   _wm_type;

	xcb_get_window_attributes_reply_t * _wa;
	xcb_get_geometry_reply_t * _geometry;

	/* ICCCM */

	wm_name_t                    _wm_name;

	wm_icon_name_t               _wm_icon_name;
	wm_normal_hints_t            _wm_normal_hints;
	wm_hints_t                   _wm_hints;
	wm_class_t                   _wm_class;
	wm_transient_for_t           _wm_transient_for;
	wm_protocols_t               _wm_protocols;
	wm_colormap_windows_t        _wm_colormap_windows;
	wm_client_machine_t          _wm_client_machine;

	/* wm_state is writen by WM */
	wm_state_t                   _wm_state;

	/* EWMH */

	net_wm_name_t                __net_wm_name;
	net_wm_visible_name_t        __net_wm_visible_name;
	net_wm_icon_name_t           __net_wm_icon_name;
	net_wm_visible_icon_name_t   __net_wm_visible_icon_name;
	net_wm_desktop_t             __net_wm_desktop;
	net_wm_window_type_t         __net_wm_window_type;
	net_wm_state_t               __net_wm_state;
	net_wm_allowed_actions_t     __net_wm_allowed_actions;
	net_wm_strut_t               __net_wm_strut;
	net_wm_strut_partial_t       __net_wm_strut_partial;
	net_wm_icon_geometry_t       __net_wm_icon_geometry;
	net_wm_icon_t                __net_wm_icon;
	net_wm_pid_t                 __net_wm_pid;
	//net_wm_handled_icons_t       __net_wm_handled_icons;
	net_wm_user_time_t           __net_wm_user_time;
	net_wm_user_time_window_t    __net_wm_user_time_window;
	net_frame_extents_t          __net_frame_extents;
	net_wm_opaque_region_t       __net_wm_opaque_region;
	net_wm_bypass_compositor_t   __net_wm_bypass_compositor;

	/* OTHERs */
	motif_hints_t                _motif_hints;

	region *                     _shape;

	xcb_window_t xid() {
		return static_cast<xcb_window_t>(_id);
	}

private:
	client_properties_t(client_properties_t const &);
	client_properties_t & operator=(client_properties_t const &);
public:

	client_properties_t(xcb_window_t id) :
			 _id(id) {

		_wa = nullptr;
		_geometry = nullptr;

		/* ICCCM */
		_wm_name = nullptr;
		_wm_icon_name = nullptr;
		_wm_normal_hints = nullptr;
		_wm_hints = nullptr;
		_wm_class = nullptr;
		_wm_transient_for = nullptr;
		_wm_protocols = nullptr;
		_wm_colormap_windows = nullptr;
		_wm_client_machine = nullptr;
		_wm_state = nullptr;

		/* EWMH */
		__net_wm_name = nullptr;
		__net_wm_visible_name = nullptr;
		__net_wm_icon_name = nullptr;
		__net_wm_visible_icon_name = nullptr;
		__net_wm_desktop = nullptr;
		__net_wm_window_type = nullptr;
		__net_wm_state = nullptr;
		__net_wm_allowed_actions = nullptr;
		__net_wm_strut = nullptr;
		__net_wm_strut_partial = nullptr;
		__net_wm_icon_geometry = nullptr;
		__net_wm_icon = nullptr;
		__net_wm_pid = nullptr;
		//__net_wm_handled_icons = false;
		__net_wm_user_time = nullptr;
		__net_wm_user_time_window = nullptr;
		__net_frame_extents = nullptr;
		__net_wm_opaque_region = nullptr;
		__net_wm_bypass_compositor = nullptr;

		_motif_hints = nullptr;

		_shape = nullptr;

		_wm_type = A(_NET_WM_WINDOW_TYPE_NORMAL);

	}

	~client_properties_t() {
		if(_wa != nullptr)
			free(_wa);
		if(_geometry != nullptr)
			free(_geometry);
		delete_all_properties();
	}

	void read_all_properties() {

	}

	void delete_all_properties() {
		safe_delete(_shape);
	}

	bool read_window_attributes() {

	}

	void update_wm_name() {

	}

	void update_wm_icon_name() {

	}

	void update_wm_normal_hints() {

	}

	void update_wm_hints() {

	}

	void update_wm_class() {

	}

	void update_wm_transient_for() {

	}

	void update_wm_protocols() {

	}

	void update_wm_colormap_windows() {

	}

	void update_wm_client_machine() {

	}

	void update_wm_state() {

	}

	/* EWMH */

	void update_net_wm_name() {

	}

	void update_net_wm_visible_name() {

	}

	void update_net_wm_icon_name() {

	}

	void update_net_wm_visible_icon_name() {

	}

	void update_net_wm_desktop() {

	}

	void update_net_wm_window_type() {

	}

	void update_net_wm_state() {

	}

	void update_net_wm_allowed_actions() {

	}

	void update_net_wm_struct() {

	}

	void update_net_wm_struct_partial() {

	}

	void update_net_wm_icon_geometry() {

	}

	void update_net_wm_icon() {

	}

	void update_net_wm_pid() {

	}

	void update_net_wm_handled_icons();

	void update_net_wm_user_time() {

	}

	void update_net_wm_user_time_window() {

	}

	void update_net_frame_extents() {

	}

	void update_net_wm_opaque_region() {

	}

	void update_net_wm_bypass_compositor() {

	}

	void update_motif_hints() {

	}

	void update_shape() {

	}


	bool has_motif_border() {
		if (_motif_hints != nullptr) {
			if (_motif_hints->flags & MWM_HINTS_DECORATIONS) {
				if (not (_motif_hints->decorations & MWM_DECOR_BORDER)
						and not ((_motif_hints->decorations & MWM_DECOR_ALL))) {
					return false;
				}
			}
		}
		return true;
	}

	void set_net_wm_desktop(unsigned int n) {

	}

public:

	void print_window_attributes() {
		printf(">>> window xid: #%u\n", _id);
		printf("> size: %dx%d+%d+%d\n", _geometry->width, _geometry->height, _geometry->x, _geometry->y);
		printf("> border_width: %d\n", _geometry->border_width);
		printf("> depth: %d\n", _geometry->depth);
		printf("> visual #%u\n", _wa->visual);
		printf("> root: #%u\n", _geometry->root);
		if (_wa->_class == CopyFromParent) {
			printf("> class: CopyFromParent\n");
		} else if (_wa->_class == InputOutput) {
			printf("> class: InputOutput\n");
		} else if (_wa->_class == InputOnly) {
			printf("> class: InputOnly\n");
		} else {
			printf("> class: Unknown\n");
		}

		if (_wa->map_state == IsViewable) {
			printf("> map_state: IsViewable\n");
		} else if (_wa->map_state == IsUnviewable) {
			printf("> map_state: IsUnviewable\n");
		} else if (_wa->map_state == IsUnmapped) {
			printf("> map_state: IsUnmapped\n");
		} else {
			printf("> map_state: Unknown\n");
		}

		printf("> bit_gravity: %d\n", _wa->bit_gravity);
		printf("> win_gravity: %d\n", _wa->win_gravity);
		printf("> backing_store: %dlx\n", _wa->backing_store);
		printf("> backing_planes: %x\n", _wa->backing_planes);
		printf("> backing_pixel: %x\n", _wa->backing_pixel);
		printf("> save_under: %d\n", _wa->save_under);
		printf("> colormap: <Not Implemented>\n");
		printf("> all_event_masks: %08x\n", _wa->all_event_masks);
		printf("> your_event_mask: %08x\n", _wa->your_event_mask);
		printf("> do_not_propagate_mask: %08x\n", _wa->do_not_propagate_mask);
		printf("> override_redirect: %d\n", _wa->override_redirect);
	}


	void print_properties() {

	}

	void update_type() {

	}

	xcb_atom_t wm_type() const { return _wm_type; }
	xcb_window_t         id() const { return _id; }

	auto wa() const -> xcb_get_window_attributes_reply_t const * { return _wa; }
	auto geometry() const -> xcb_get_geometry_reply_t const * { return _geometry; }

	/* ICCCM */

	std::string const *                     wm_name() const { return _wm_name; }
	std::string const *                     wm_icon_name() const { return _wm_icon_name; };
	XSizeHints const *                 wm_normal_hints() const { return _wm_normal_hints; }
	XWMHints const *                   wm_hints() const { return _wm_hints; }
	std::vector<std::string> const *             wm_class() const {return _wm_class; }
	xcb_window_t const *               wm_transient_for() const { return _wm_transient_for; }
	std::list<xcb_atom_t> const *           wm_protocols() const { return _wm_protocols; }
	std::vector<xcb_window_t> const *       wm_colormap_windows() const { return _wm_colormap_windows; }
	std::string const *                     wm_client_machine() const { return _wm_client_machine; }

	/* wm_state is writen by WM */
	wm_state_data_t const *            wm_state() const {return _wm_state; }

	/* EWMH */

	std::string const *                     net_wm_name() const { return __net_wm_name; }
	std::string const *                     net_wm_visible_name() const { return __net_wm_visible_name; }
	std::string const *                     net_wm_icon_name() const { return __net_wm_icon_name; }
	std::string const *                     net_wm_visible_icon_name() const { return __net_wm_visible_icon_name; }
	unsigned int const *                    net_wm_desktop() const { return __net_wm_desktop; }
	std::list<xcb_atom_t> const *           net_wm_window_type() const { return __net_wm_window_type; }
	std::list<xcb_atom_t> const *           net_wm_state() const { return __net_wm_state; }
	std::list<xcb_atom_t> const *           net_wm_allowed_actions() const { return __net_wm_allowed_actions; }
	std::vector<int> const *                net_wm_strut() const { return __net_wm_strut; }
	std::vector<int> const *                net_wm_strut_partial() const { return __net_wm_strut_partial; }
	std::vector<int> const *                net_wm_icon_geometry() const { return __net_wm_icon_geometry; }
	std::vector<uint32_t> const *           net_wm_icon() const { return __net_wm_icon; }
	unsigned int const *                    net_wm_pid() const { return __net_wm_pid; }
	//bool                                  net_wm_handled_icons() const { return __net_wm_handled_icons; }
	uint32_t const *                        net_wm_user_time() const { return __net_wm_user_time; }
	xcb_window_t const *                    net_wm_user_time_window() const { return __net_wm_user_time_window; }
	std::vector<int> const *                net_frame_extents() const { return __net_frame_extents; }
	std::vector<int> const *                net_wm_opaque_region() const { return __net_wm_opaque_region; }
	unsigned int const *                    net_wm_bypass_compositor() const { return __net_wm_bypass_compositor; }

	/* OTHERs */
	motif_wm_hints_t const *           motif_hints() const { return _motif_hints; }

	region const *                     shape() const { return _shape; }

	void net_wm_state_add(atom_e atom) {

	}

	void net_wm_state_remove(atom_e atom) {

	}

	void net_wm_allowed_actions_add(atom_e atom) {

	}

	void net_wm_allowed_actions_set(std::list<atom_e> atom_list) {

	}

	void set_wm_state(int state) {

	}

	void process_event(xcb_configure_notify_event_t const * e) {
		if(_wa->override_redirect != e->override_redirect) {
			_wa->override_redirect = e->override_redirect;
			update_type();
		}
		_geometry->width = e->width;
		_geometry->height = e->height;
		_geometry->x = e->x;
		_geometry->y = e->y;
		_geometry->border_width = e->border_width;
	}

	i_rect position() const { return i_rect{_geometry->x, _geometry->y, _geometry->width, _geometry->height}; }

};

}



#endif /* CLIENT_PROPERTIES_HXX_ */
