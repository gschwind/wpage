/*
 * Copyright © 2010-2012 Intel Corporation
 * Copyright © 2011-2012 Collabora, Ltd.
 * Copyright © 2013 Raspberry Pi Foundation
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef PAGE_SHELL_SHELL_HXX_
#define PAGE_SHELL_SHELL_HXX_

#include <stdbool.h>
#include <time.h>

#include "compositor.h"

#include "desktop-shell-server-protocol.h"

#include "shell_seat.hxx"
#include "client.hxx"
#include "surface.hxx"
#include "desktop_shell.hxx"
#include "focus_state.hxx"

void
center_on_output(struct weston_view *view,
		 struct weston_output *output);

struct weston_output *
get_default_output(struct weston_compositor *compositor);

namespace page {
extern weston_view * get_default_view(weston_surface *surface);
}

page::workspace *
get_current_workspace(struct desktop_shell *shell);

void
activate_workspace( desktop_shell *shell, unsigned int index);

page::workspace * workspace_create();

void
animate_workspace_change_frame(weston_animation *animation,
			        weston_output *output, uint32_t msecs);

void
setup_output_destroy_handler(struct weston_compositor *ec,
							struct desktop_shell *shell);

void
lower_fullscreen_layer(struct desktop_shell *shell);

namespace page {
void
activate(struct desktop_shell *shell, struct weston_surface *es,
	 struct weston_seat *seat, bool configure);
}

void
workspace_destroy(page::workspace *ws);

int
input_panel_setup(struct desktop_shell *shell);
void
input_panel_destroy(struct desktop_shell *shell);

typedef void (*shell_for_each_layer_func_t)(struct desktop_shell *,
					    struct weston_layer *, void *);

void
shell_for_each_layer(struct desktop_shell *shell,
		     shell_for_each_layer_func_t func,
		     void *data);

void fade_out_done(struct weston_view_animation *animation, void *data);

void surface_subsurfaces_boundingbox(struct weston_surface *surface, int32_t *x,
				int32_t *y, int32_t *w, int32_t *h);

void weston_view_set_initial_position(struct weston_view *view,
				 struct desktop_shell *shell);

void restore_output_mode(struct weston_output *output);

void
ping_handler(struct weston_surface *surface, uint32_t serial);

int
xdg_shell_unversioned_dispatch(const void *implementation,
			       void *_target, uint32_t opcode,
			       const struct wl_message *message,
			       union wl_argument *args);

void
focus_state_destroy(struct focus_state *state);

void
focus_surface_destroy(struct focus_surface *fsurf);


bool
is_black_surface (struct weston_surface *es, struct weston_surface **fs_surface);

#endif
