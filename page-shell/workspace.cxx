/*
 * workspace.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include "workspace.hxx"
#include "focus_state.hxx"
#include "shell.h"

#include "notebook.hxx"
#include "viewport.hxx"


namespace page {

workspace::~workspace()
{
	focus_state *state, *next;

	wl_list_for_each_safe(state, next, &this->focus_list, link)
		focus_state_destroy(state);

	if (this->fsurf_front)
		focus_surface_destroy(this->fsurf_front);
	if (this->fsurf_back)
		focus_surface_destroy(this->fsurf_back);

}

page_component_t * workspace::parent() const {
	return _parent;
}

workspace::workspace(unsigned id, theme_t * theme) :
	_allocation(),
	_parent(nullptr),
	_viewport_outputs(),
	_default_pop(nullptr),
	_is_hidden(false),
	_workarea(),
	_primary_viewport(nullptr),
	_id(id),
	_theme{theme}
{
	client_focus.push_back(nullptr);

	weston_layer_init(&this->layer, nullptr);
	wl_list_init(&this->focus_list);
	//wl_list_init(&this->seat_destroyed_listener.link);
	//this->seat_destroyed_listener.notify = seat_destroyed;
	this->fsurf_front = nullptr;
	this->fsurf_back = nullptr;
	this->focus_animation = nullptr;

}

void workspace::render(cairo_t * cr, i_rect const & area) const { }

void workspace::raise_child(tree_t * t) {

	shell_surface * mw = dynamic_cast<shell_surface *>(t);
	if(has_key(_fullscreen_clients, mw)) {
		_fullscreen_clients.remove(mw);
		_fullscreen_clients.push_back(mw);
	}

	if(has_key(_floating_clients, mw)) {
		_floating_clients.remove(mw);
		_floating_clients.push_back(mw);
	}

	viewport_t * v = dynamic_cast<viewport_t *>(t);
	if(has_key(_viewport_stack, v)) {
		_viewport_stack.remove(v);
		_viewport_stack.push_back(v);
	}

	if(_parent != nullptr) {
		_parent->raise_child(this);
	}
}

std::string workspace::get_node_name() const {
	return _get_node_name<'D'>();
}

void workspace::prepare_render(std::vector<std::shared_ptr<renderable_t>> & out, page::time_t const & time) {
//		if(_is_hidden)
//			return;
//
//		for(auto i: _viewport_outputs) {
//			i.second->prepare_render(out, time);
//		}
//
//		for(auto i: _floating_clients) {
//			i->prepare_render(out, time);
//		}
//
//		for(auto i: _fullscreen_clients) {
//			i->prepare_render(out, time);
//		}
}

void workspace::set_parent(tree_t * t) {
	throw exception_t("viewport cannot have tree_t as parent");
}

void workspace::set_parent(page_component_t * t) {
	_parent = t;
}

i_rect workspace::allocation() const {
	return _allocation;
}

void workspace::render_legacy(cairo_t * cr, i_rect const & area) const { }

//	auto get_viewport_map() const -> std::map<xcb_randr_crtc_t, viewport_t *> const & {
//		return _viewport_outputs;
//	}

//	auto set_layout(std::map<xcb_randr_crtc_t, viewport_t *> const & new_layout) -> void {
//		_viewport_outputs = new_layout;
//		_viewport_stack.clear();
//		for(auto i: _viewport_outputs) {
//			_viewport_stack.push_back(i.second);
//		}
//	}

auto workspace::get_any_viewport() const -> viewport_t * {
	if(_viewport_stack.size() > 0) {
		return _viewport_stack.front();
	} else {
		return nullptr;
	}
}

auto workspace::get_viewports() const -> std::vector<viewport_t * > {
	std::vector<viewport_t *> ret;
	for(auto i: _viewport_outputs) {
			ret.push_back(i.second);
	}
	return ret;
}

void workspace::set_default_pop(notebook_t * n) {
	_default_pop->set_default(false);
	_default_pop = n;
	_default_pop->set_default(true);
}

notebook_t * workspace::default_pop() {
	return _default_pop;
}

void workspace::children(std::vector<tree_t *> & out) const {
	for(auto i: _viewport_outputs) {
			out.push_back(i.second);
	}

	for(auto i: _floating_clients) {
			out.push_back(i);
	}

	for(auto i: _fullscreen_clients) {
			out.push_back(i);
	}

}

void workspace::update_default_pop() {
	_default_pop = nullptr;
	for(auto i: filter_class<notebook_t>(tree_t::get_all_children())) {
		if(_default_pop == nullptr) {
			_default_pop = i;
			_default_pop->set_default(true);
		} else {
			i->set_default(false);
		}
	}
}

void workspace::add_floating_client(shell_surface * c) {
	_floating_clients.push_back(c);
	c->set_parent(this);
}

void workspace::add_fullscreen_client(shell_surface * c) {
	_fullscreen_clients.push_back(c);
	c->set_parent(this);
}

void workspace::replace(page_component_t * src, page_component_t * by) {
	throw std::runtime_error("desktop_t::replace implemented yet!");
}

void workspace::remove(tree_t * src) {

	{
		auto i = _viewport_outputs.begin();
		while(i != _viewport_outputs.end()) {
			if(i->second == src) {
				i = _viewport_outputs.erase(i);
			} else {
				++i;
			}
		}
	}

	/**
	 * use reinterpret_cast because we try to remove src pointer
	 * and we don't care is type
	 **/
	_viewport_stack.remove(reinterpret_cast<viewport_t*>(src));
	_floating_clients.remove(reinterpret_cast<shell_surface*>(src));
	_fullscreen_clients.remove(reinterpret_cast<shell_surface*>(src));

}

void workspace::set_allocation(i_rect const & area) {
	_allocation = area;
}

void workspace::get_all_children(std::vector<tree_t *> & out) const {
	for(auto i: _viewport_outputs) {
		out.push_back(i.second);
		i.second->get_all_children(out);
	}

	for(auto i: _floating_clients) {
		out.push_back(i);
		i->get_all_children(out);
	}

	for(auto i: _fullscreen_clients) {
		out.push_back(i);
		i->get_all_children(out);
	}
}

void workspace::hide() {
	_is_hidden = true;
	for(auto i: tree_t::children()) {
		i->hide();
	}
}

void workspace::show() {
	_is_hidden = false;
	for(auto i: tree_t::children()) {
		i->show();
	}
}

bool workspace::is_hidden() {
	return _is_hidden;
}

void workspace::get_visible_children(std::vector<tree_t *> & out) {
	if (not _is_hidden) {
		out.push_back(this);
		for (auto i : tree_t::children()) {
			i->get_visible_children(out);
		}
	}
}

void workspace::set_workarea(i_rect const & r) {
	_workarea = r;
}

i_rect const & workspace::workarea() {
	return _workarea;
}

void workspace::set_primary_viewport(viewport_t * v) {
	_primary_viewport = v;
}

viewport_t * workspace::primary_viewport() {
	return _primary_viewport;
}

int workspace::id() {
	return _id;
}

void workspace::add_weston_output(struct weston_output * output, struct weston_surface * wsurf, struct weston_buffer * buffer) {
	auto x = _viewport_outputs.find(output);
	if(x == _viewport_outputs.end()) {
		viewport_t * v = new viewport_t(_theme, i_rect(output->x, output->y, output->width, output->height), wsurf, buffer);
		_viewport_outputs[output] = v;
		v->set_parent(this);
	}
}

std::vector<page_event_t> workspace::compute_page_areas(
		std::list<tree_t const *> const & page) const {

	std::vector<page_event_t> ret;

	for (auto i : page) {
		if(dynamic_cast<split_t const *>(i)) {
			split_t const * s = dynamic_cast<split_t const *>(i);
			page_event_t bsplit(PAGE_EVENT_SPLIT);
			bsplit.position = s->compute_split_bar_location();

			if(s->type() == VERTICAL_SPLIT) {
				bsplit.position.w += _theme->notebook.margin.right + _theme->notebook.margin.left;
				bsplit.position.x -= _theme->notebook.margin.right;
			} else {
				bsplit.position.h += _theme->notebook.margin.bottom;
				bsplit.position.y -= _theme->notebook.margin.bottom;
			}

			bsplit.spt = s;
			ret.push_back(bsplit);
		} else if (dynamic_cast<notebook_t const *>(i)) {
			notebook_t const * n = dynamic_cast<notebook_t const *>(i);
			n->compute_areas_for_notebook(&ret);
		}
	}

	return ret;
}

std::vector<page_event_t>  workspace::compute_page_areas() const {
	std::vector<tree_t *> tmp = tree_t::get_all_children();
	std::list<tree_t const *> lc(tmp.begin(), tmp.end());
	std::vector<page_event_t> page_areas{compute_page_areas(lc)};
	return page_areas;
}

}
