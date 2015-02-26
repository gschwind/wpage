/*
 * weston_shell_interface_impl.cxx
 *
 * copyright (2015) Benoit Gschwind
 *
 * This code is licensed under the GPLv3. see COPYING file for more details.
 *
 */

#include <cassert>
#include <cstring>
#include "surface.hxx"

namespace page {

static shell_surface * create_shell_surface(void *shell, struct weston_surface *surface, const struct weston_shell_client *client);
static struct weston_view * get_primary_view(void *shell, shell_surface *shsurf);
static void set_toplevel(shell_surface *shsurf);
static void set_transient(shell_surface *shsurf, struct weston_surface *parent, int x, int y, uint32_t flags);
static void shell_interface_set_fullscreen(shell_surface *shsurf, uint32_t method, uint32_t framerate, struct weston_output *output);
static void set_xwayland(shell_surface *shsurf, int x, int y, uint32_t flags);
static int shell_interface_move(shell_surface *shsurf, struct weston_seat *ws);
static int surface_resize(shell_surface *shsurf, struct weston_seat *seat, uint32_t edges);
static void set_title(shell_surface *shsurf, const char *title);
static void set_window_geometry(shell_surface *shsurf, int32_t x, int32_t y, int32_t width, int32_t height);

const struct weston_shell_interface weston_shell_interface_impl = {
		nullptr,
		create_shell_surface,
		get_primary_view,
		set_toplevel,
		set_transient,
		shell_interface_set_fullscreen,
		set_xwayland,
		shell_interface_move,
		surface_resize,
		set_title,
		set_window_geometry
};

static shell_surface * create_shell_surface(void *shell, struct weston_surface *surface,
		     const struct weston_shell_client *client)
{
	return new shell_surface(nullptr, shell, surface, client);
}

static struct weston_view * get_primary_view(void *shell, shell_surface *shsurf)
{
	return shsurf->view;
}

static void set_toplevel(shell_surface *shsurf)
{
	shsurf->shell_surface_set_parent(nullptr);
	shsurf->surface_clear_next_states();
	shsurf->type = SHELL_SURFACE_TOPLEVEL;

	/* The layer_link is updated in set_surface_type(),
	 * called from configure. */
}

static void
set_transient(shell_surface *shsurf,
	      struct weston_surface *parent, int x, int y, uint32_t flags)
{
	assert(parent != NULL);

	shsurf->shell_surface_set_parent(parent);

	shsurf->surface_clear_next_states();

	shsurf->transient.x = x;
	shsurf->transient.y = y;
	shsurf->transient.flags = flags;

	shsurf->next_state.relative = true;
	shsurf->state_changed = true;
	shsurf->type = SHELL_SURFACE_TOPLEVEL;

	/* The layer_link is updated in set_surface_type(),
	 * called from configure. */
}

static void
shell_interface_set_fullscreen(shell_surface *shsurf,
			       uint32_t method,
			       uint32_t framerate,
			       struct weston_output *output)
{
	shsurf->surface_clear_next_states();
	shsurf->next_state.fullscreen = true;
	shsurf->state_changed = true;
	shsurf->set_fullscreen(method, framerate, output);
}


static void
set_xwayland(shell_surface *shsurf, int x, int y, uint32_t flags)
{
	/* XXX: using the same fields for transient type */
	shsurf->surface_clear_next_states();
	shsurf->transient.x = x;
	shsurf->transient.y = y;
	shsurf->transient.flags = flags;

	shsurf->shell_surface_set_parent(nullptr);

	shsurf->type = SHELL_SURFACE_XWAYLAND;
	shsurf->state_changed = true;
}


static int
shell_interface_move(shell_surface *shsurf, struct weston_seat *ws)
{
	return shsurf->surface_move(ws, 1);
}


static int surface_resize(shell_surface *shsurf, struct weston_seat *seat, uint32_t edges)
{
	return shsurf->surface_resize(seat, edges);
}

static void set_title(shell_surface *shsurf, const char *title)
{
	free(shsurf->title);
	shsurf->title = strdup(title);
}

static void set_window_geometry(shell_surface *shsurf,
		    int32_t x, int32_t y, int32_t width, int32_t height)
{
	shsurf->next_geometry.x = x;
	shsurf->next_geometry.y = y;
	shsurf->next_geometry.width = width;
	shsurf->next_geometry.height = height;
	shsurf->has_next_geometry = true;
}

}

