/*
 * Copyright © 2014 Pekka Paalanen <pq@iki.fi>
 * Copyright © 2014 Collabora, Ltd.
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

#include "config.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "timeline.h"
#include "compositor.h"

struct timeline_log {
	clock_t clk_id;
	FILE *file;
	unsigned series;
	struct wl_listener compositor_destroy_listener;
};

WL_EXPORT int weston_timeline_enabled_;
static struct timeline_log timeline_ = { CLOCK_MONOTONIC, NULL, 0 };

static int
weston_timeline_do_open(void)
{
	time_t t;
	struct tm *tmp;
	char fname[1000];
	int ret;

	t = time(NULL);
	tmp = localtime(&t);
	if (!tmp) {
		weston_log("Conversion to local time failed, "
			   "cannot open timeline log file.\n");
		return -1;
	}

	ret = strftime(fname, sizeof(fname),
		       "weston-timeline-%F_%H-%M-%S.log", tmp);
	if (ret == 0) {
		weston_log("Time formatting failed, "
			   "cannot open timeline log file.\n");
		return -1;
	}

	timeline_.file = fopen(fname, "w");
	if (!timeline_.file) {
		weston_log("Cannot open '%s' for writing: %s\n",
			   fname, strerror(errno));
		return -1;
	}

	weston_log("Opened timeline file '%s'\n", fname);

	return 0;
}

static void
timeline_notify_destroy(struct wl_listener *listener, void *data)
{
	weston_timeline_close();
}

void
weston_timeline_open(struct weston_compositor *compositor)
{
	if (weston_timeline_enabled_)
		return;

	if (weston_timeline_do_open() < 0)
		return;

	timeline_.compositor_destroy_listener.notify = timeline_notify_destroy;
	wl_signal_add(&compositor->destroy_signal,
		      &timeline_.compositor_destroy_listener);

	if (++timeline_.series == 0)
		++timeline_.series;

	weston_timeline_enabled_ = 1;
}

void
weston_timeline_close(void)
{
	if (!weston_timeline_enabled_)
		return;

	weston_timeline_enabled_ = 0;

	wl_list_remove(&timeline_.compositor_destroy_listener.link);

	fclose(timeline_.file);
	timeline_.file = NULL;
	weston_log("Timeline log file closed.\n");
}

struct timeline_emit_context {
	FILE *cur;
	FILE *out;
	unsigned series;
};

static unsigned
timeline_new_id(void)
{
	static unsigned idc;

	if (++idc == 0)
		++idc;

	return idc;
}

static int
check_series(struct timeline_emit_context *ctx,
	     struct weston_timeline_object *to)
{
	if (to->series == 0 || to->series != ctx->series) {
		to->series = ctx->series;
		to->id = timeline_new_id();
		return 1;
	}

	if (to->force_refresh) {
		to->force_refresh = 0;
		return 1;
	}

	return 0;
}

static void
fprint_quoted_string(FILE *fp, const char *str)
{
	if (!str) {
		fprintf(fp, "null");
		return;
	}

	fprintf(fp, "\"%s\"", str);
}

static int
emit_weston_output(struct timeline_emit_context *ctx, void *obj)
{
	struct weston_output *o = obj;

	if (check_series(ctx, &o->timeline)) {
		fprintf(ctx->out, "{ \"id\":%u, "
			"\"type\":\"weston_output\", \"name\":",
			o->timeline.id);
		fprint_quoted_string(ctx->out, o->name);
		fprintf(ctx->out, " }\n");
	}

	fprintf(ctx->cur, "\"wo\":%u", o->timeline.id);

	return 1;
}

static void
check_weston_surface_description(struct timeline_emit_context *ctx,
				 struct weston_surface *s)
{
	struct weston_surface *mains;
	char d[512];
	char mainstr[32];

	if (!check_series(ctx, &s->timeline))
		return;

	mains = weston_surface_get_main_surface(s);
	if (mains != s) {
		check_weston_surface_description(ctx, mains);
		if (snprintf(mainstr, sizeof(mainstr),
			     ", \"main_surface\":%u", mains->timeline.id) < 0)
			mainstr[0] = '\0';
	} else {
		mainstr[0] = '\0';
	}

	if (!s->get_label || s->get_label(s, d, sizeof(d)) < 0)
		d[0] = '\0';

	fprintf(ctx->out, "{ \"id\":%u, "
		"\"type\":\"weston_surface\", \"desc\":", s->timeline.id);
	fprint_quoted_string(ctx->out, d[0] ? d : NULL);
	fprintf(ctx->out, "%s }\n", mainstr);
}

static int
emit_weston_surface(struct timeline_emit_context *ctx, void *obj)
{
	struct weston_surface *s = obj;

	check_weston_surface_description(ctx, s);
	fprintf(ctx->cur, "\"ws\":%u", s->timeline.id);

	return 1;
}

static int
emit_vblank_timestamp(struct timeline_emit_context *ctx, void *obj)
{
	struct timespec *ts = obj;

	fprintf(ctx->cur, "\"vblank\":[%" PRId64 ", %ld]",
		(int64_t)ts->tv_sec, ts->tv_nsec);

	return 1;
}

typedef int (*type_func)(struct timeline_emit_context *ctx, void *obj);

static const type_func type_dispatch[] = {
	[TLT_OUTPUT] = emit_weston_output,
	[TLT_SURFACE] = emit_weston_surface,
	[TLT_VBLANK] = emit_vblank_timestamp,
};

WL_EXPORT void
weston_timeline_point(const char *name, ...)
{
	va_list argp;
	struct timespec ts;
	enum timeline_type otype;
	void *obj;
	char buf[512];
	struct timeline_emit_context ctx;

	clock_gettime(timeline_.clk_id, &ts);

	ctx.out = timeline_.file;
	ctx.cur = fmemopen(buf, sizeof(buf), "w");
	ctx.series = timeline_.series;

	if (!ctx.cur) {
		weston_log("Timeline error in fmemopen, closing.\n");
		weston_timeline_close();
		return;
	}

	fprintf(ctx.cur, "{ \"T\":[%" PRId64 ", %ld], \"N\":\"%s\"",
		(int64_t)ts.tv_sec, ts.tv_nsec, name);

	va_start(argp, name);
	while (1) {
		otype = va_arg(argp, enum timeline_type);
		if (otype == TLT_END)
			break;

		obj = va_arg(argp, void *);
		if (type_dispatch[otype]) {
			fprintf(ctx.cur, ", ");
			type_dispatch[otype](&ctx, obj);
		}
	}
	va_end(argp);

	fprintf(ctx.cur, " }\n");
	fflush(ctx.cur);
	if (ferror(ctx.cur)) {
		weston_log("Timeline error in constructing entry, closing.\n");
		weston_timeline_close();
	} else {
		fprintf(ctx.out, "%s", buf);
	}

	fclose(ctx.cur);
}
