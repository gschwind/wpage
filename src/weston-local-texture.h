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

#ifndef _WAYLAND_SYSTEM_WESTON_LOCAL_TEXTURE_H_
#define _WAYLAND_SYSTEM_WESTON_LOCAL_TEXTURE_H_

#include <stdint.h>
#include "compositor.h"

#ifdef  __cplusplus
extern "C" {
#endif

struct weston_local_buffer {
	int32_t width, height;
	int32_t stride;
	uint32_t format;
	/** data are appended here **/
};

int weston_local_buffer_stride_for(uint32_t format, int32_t width, int32_t height);

/**
 * @input format: Wayland surface format, currently only WL_SHM_FORMAT_ARGB8888 and WL_SHM_FORMAT_ARGB8888 are suported.
 * @input width, height: desired texture width and height.
 */
struct weston_local_buffer * weston_local_buffer_create(uint32_t format, int32_t width, int32_t height);

void * weston_local_buffer_get_data(struct weston_local_buffer * tex);

#ifdef  __cplusplus
}
#endif

#endif /* SRC_WESTON_LOCAL_TEXTURE_H_ */
