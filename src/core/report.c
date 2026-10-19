#include "report.h"
#include "platform.h"
#include "log.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

struct buf { char *s; size_t n, cap; };

static void put(struct buf *b, const char *fmt, ...)
{
	va_list ap;
	int k;
	va_start(ap, fmt);
	k = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (k < 0) return;
	if (b->n + (size_t)k + 1 > b->cap) { while (b->n + (size_t)k + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 4096; b->s = sku_xrealloc(b->s, b->cap); }
	va_start(ap, fmt);
	vsnprintf(b->s + b->n, b->cap - b->n, fmt, ap);
	va_end(ap);
	b->n += (size_t)k;
}

char *sku_report_render(sku_addon *a, const char *title)
{
	struct buf b = {0};
	char when[32], nice[32], text[256], detail[512];
	const sku_plan *p = &a->plan;
	sku_status st = sku_addon_status(a, text, sizeof text, detail, sizeof detail);

	sku_format_time(when, sizeof when, sku_time_now());
	sku_nice_size(nice, sizeof nice, p->bytes);
	put(&b, "%s - %s\n", title, when);
	put(&b, "Addon    : %s (%s)\n", a->name, sku_genre_name(a->genre));
	put(&b, "Folder   : %s\n", a->dir);
	if (a->fallback[0]) put(&b, "Source   : %s (channel %s, no main depot: public channel %s used, protocol %d)\n", a->base_url, a->channel, a->fallback, a->level);
	else put(&b, "Source   : %s (channel %s, protocol %d)\n", a->base_url, a->channel, a->level);
	put(&b, "Version  : local %s, remote %s\n", a->version, a->checked || a->remote_version[0] ? a->remote_version : "?");
	put(&b, "Status   : %s - %s\n", sku_status_name(st), text);
	if (*detail) put(&b, "           %s\n", detail);
	put(&b, "Files    : %d new, %d changed, %d to delete, %d unchanged (%d from cache), %d ignored, %d error(s)\n",
		p->n_new, p->n_changed, p->n_delete, p->n_unchanged, p->n_cached, p->n_ignored, p->n_error);
	put(&b, "Download : %s in %d file(s), %lld ms\n", nice, p->n_new + p->n_changed, (long long)p->duration_ms);
	if (p->n) {
		put(&b, "\n%-8s %-8s %12s  %s\n", "reason", "result", "size", "path");
		for (int i = 0; i < p->n; i++) {
			const sku_plan_item *it = &p->v[i];
			const char *res = it->reason == SKU_PLAN_IGNORED ? "-" : it->result == SKU_OK ? "ok" : sku_err_str(it->result);
			if (it->size >= 0) sku_snprintf(nice, sizeof nice, "%lld", (long long)it->size); else sku_strlcpy(nice, "?", sizeof nice);
			put(&b, "%-8s %-8s %12s  %s%s%s\n", sku_reason_name(it->reason), res, nice, it->path, it->detail ? "  # " : "", it->detail ? it->detail : "");
		}
	}
	if (!b.s) b.s = sku_xstrdup("");
	return b.s;
}

int sku_report_save(sku_addon *a, const char *title)
{
	char path[SKU_PATH_MAX], *text = sku_report_render(a, title);
	int rc;
	sku_instance_report_path(a->inst, a->dir, path, sizeof path);
	rc = sku_write_file_atomic(path, text, strlen(text));
	if (rc != SKU_OK) sku_warn("%s: cannot write report %s", a->name, path);
	free(text);
	return rc;
}
