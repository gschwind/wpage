/*
 * workspace.hxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */
#ifndef PAGE_SHELL_WORKSPACE_HXX_
#define PAGE_SHELL_WORKSPACE_HXX_

#include "compositor.h"

#include "tree.hxx"
#include "viewport.hxx"
#include "focus_state.hxx"
#include "page_event.hxx"

namespace page {

struct notebook_t;
struct viewport_t;

struct workspace : public page_component_t {
	struct weston_layer layer;

	struct wl_list focus_list;
	//struct wl_listener seat_destroyed_listener;

	focus_surface *fsurf_front;
	focus_surface *fsurf_back;
	weston_view_animation *focus_animation;

private:
	theme_t * _theme;
	page_component_t * _parent;
	i_rect _allocation;
	i_rect _workarea;

	unsigned const _id;

	/** map viewport to real outputs **/
	std::map<struct weston_output *, viewport_t *> _viewport_outputs;
	std::list<viewport_t *> _viewport_stack;
	std::list<shell_surface *> _floating_clients;
	std::list<shell_surface *> _fullscreen_clients;


	viewport_t * _primary_viewport;
	notebook_t * _default_pop;
	bool _is_hidden;
	workspace(workspace const & v);
	workspace & operator= (workspace const &);

public:

	std::list<shell_surface *> client_focus;

	page_component_t * parent() const;

	workspace(unsigned id, theme_t * theme);
	void render(cairo_t * cr, i_rect const & area) const;
	void raise_child(tree_t * t);
	std::string get_node_name() const;
	void prepare_render(std::vector<std::shared_ptr<renderable_t>> & out, page::time_t const & time);
	void set_parent(tree_t * t);
	void set_parent(page_component_t * t);

	i_rect allocation() const;

	void render_legacy(cairo_t * cr, i_rect const & area) const;

	auto get_any_viewport() const -> viewport_t *;

	auto get_viewports() const -> std::vector<viewport_t * >;
	void set_default_pop(notebook_t * n);
	notebook_t * default_pop();

	void children(std::vector<tree_t *> & out) const;
	void update_default_pop();
	void add_floating_client(shell_surface * c);
	void add_fullscreen_client(shell_surface * c);
	void replace(page_component_t * src, page_component_t * by);
	void remove(tree_t * src);
	void set_allocation(i_rect const & area);
	void get_all_children(std::vector<tree_t *> & out) const;
	void hide();
	void show();
	bool is_hidden();
	void get_visible_children(std::vector<tree_t *> & out);
	void set_workarea(i_rect const & r);
	i_rect const & workarea();
	void set_primary_viewport(viewport_t * v);
	viewport_t * primary_viewport();
	int id();

	void add_weston_output(struct weston_output * output, struct weston_surface * wsurf, struct weston_buffer * buffer);

	workspace();
	~workspace();

	std::vector<page_event_t> compute_page_areas(std::list<tree_t const *> const & page) const;
	std::vector<page_event_t> compute_page_areas() const;

};

}


#endif /* PAGE_SHELL_WORKSPACE_HXX_ */
