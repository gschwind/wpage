/*
 * Copyright © 2015 Benoit Gschwind
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

#include "weston-local-texture.h"

#include "wayland-server-protocol.h"

#include <stdlib.h>

WL_EXPORT int
weston_local_texture_stride_for(uint32_t format, int32_t width, int32_t height) {
	switch (format) {
	case WL_SHM_FORMAT_ARGB8888:
	case WL_SHM_FORMAT_XRGB8888:
		return 4 * width;
	default:
		/** unsupported format **/
		return -1;
	}
}

/**
 * @input format: Wayland surface format, currently only WL_SHM_FORMAT_ARGB8888 and WL_SHM_FORMAT_ARGB8888 are suported.
 * @input width, height: desired texture width and height.
 */
WL_EXPORT struct weston_local_texture *
weston_local_texture_create(uint32_t format, int32_t width, int32_t height) {

	struct weston_local_texture * tex;

	int stride = weston_local_texture_stride_for(format, width, height);
	if (stride < 0)
		return NULL;

	tex = malloc(sizeof *tex + stride * height);
	if (tex == NULL)
		return NULL;

	tex->width = width;
	tex->height = height;
	tex->stride = stride;
	tex->format = format;

	return tex;

}

/**
 *
 **/
WL_EXPORT void *
weston_local_texture_get_data(struct weston_local_texture * tex) {

	return tex + 1;
}

