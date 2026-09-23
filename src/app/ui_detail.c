/* Detail view of one addon: actions, configuration, counters, files, report. */
#include "app.h"
#include "nuklear_config.h"
#include "platform.h"
#include "report.h"
#include <stdlib.h>
#include <string.h>

static void load_report(app *ap, int index)
{
	char path[SKU_PATH_MAX];
	if (ap->report_for == index && ap->report_text) return;
	free(ap->report_text);
	ap->report_text = NULL;
	ap->report_for = index;
	sku_instance_report_path(&ap->inst, ap->items[index].dir, path, sizeof path);
	if (sku_read_file(path, &ap->report_text, NULL) != SKU_OK) ap->report_text = sku_xstrdup("No report yet: run a check or an update.");
}

static void form_row(struct nk_context *ctx, float row, const char *label, const char *value, struct nk_color *color)
{
	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, 170);
	nk_label(ctx, label, NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, nk_window_get_width(ctx) - 200);
	if (color) nk_label_colored(ctx, value, NK_TEXT_LEFT, *color); else nk_label(ctx, value, NK_TEXT_LEFT);
	nk_layout_row_end(ctx);
}

static int plan_cmp(const void *x, const void *y)
{
	const sku_plan_item *a = x, *b = y;
	if (a->reason != b->reason) return (int)a->reason - (int)b->reason;
	return strcmp(a->path, b->path);
}

static void files_tab(app *ap, app_item *it, float row, float height)
{
	struct nk_context *ctx = ap->ctx;
	sku_plan *p = &it->a->plan;   /* the item is idle: the plan is stable */
	struct nk_list_view view;
	char buf[SKU_PATH_MAX];
	static sku_addon *sorted_for;
	if (sorted_for != it->a) { qsort(p->v, (size_t)p->n, sizeof *p->v, plan_cmp); sorted_for = it->a; }
	nk_layout_row_dynamic(ctx, height, 1);
	if (nk_list_view_begin(ctx, &view, "files", NK_WINDOW_BORDER, (int)row, p->n ? p->n : 1)) {
		if (!p->n) {
			nk_layout_row_dynamic(ctx, row - 4, 1);
			nk_label(ctx, it->checked ? "Nothing to do." : "Run a check to see which files would change.", NK_TEXT_LEFT);
		}
		for (int k = view.begin; k < view.end && k < p->n; k++) {
			const sku_plan_item *pi = &p->v[k];
			struct nk_color c = pi->reason == SKU_PLAN_ERROR ? nk_rgb(255, 60, 60)
				: pi->reason == SKU_PLAN_DELETE ? nk_rgb(255, 165, 0)
				: pi->reason == SKU_PLAN_IGNORED ? nk_rgb(140, 140, 140)
				: !it->checked && pi->result == SKU_OK ? (ap->dark ? nk_rgb(64, 255, 64) : nk_rgb(0, 140, 0))   /* done by the last update */
				: ap->dark ? nk_rgb(240, 240, 240) : nk_rgb(30, 30, 30);
			nk_layout_row_template_begin(ctx, row - 4);
			nk_layout_row_template_push_static(ctx, 90);
			nk_layout_row_template_push_static(ctx, 100);
			nk_layout_row_template_push_dynamic(ctx);
			nk_layout_row_template_end(ctx);
			nk_label_colored(ctx, sku_reason_name(pi->reason), NK_TEXT_LEFT, c);
			if (pi->size >= 0) sku_nice_size(buf, sizeof buf, pi->size); else buf[0] = 0;
			nk_label(ctx, buf, NK_TEXT_RIGHT);
			nk_style_set_font(ctx, &ap->mono->handle);
			ui_left_truncate(ap, pi->path, nk_widget_width(ctx) - 8, buf, sizeof buf);
			if (pi->detail) { sku_strlcat(buf, "  # ", sizeof buf); sku_strlcat(buf, pi->detail, sizeof buf); }
			nk_label_colored(ctx, buf, NK_TEXT_LEFT, c);
			nk_style_set_font(ctx, &ap->font->handle);
		}
		nk_list_view_end(&view);
	}
}

static void report_tab(app *ap, int index, float height)
{
	struct nk_context *ctx = ap->ctx;
	load_report(ap, index);
	nk_layout_row_dynamic(ctx, height, 1);
	if (nk_group_begin(ctx, "report", NK_WINDOW_BORDER)) {
		char *cursor = ap->report_text, *line;
		char *copy = sku_xstrdup(ap->report_text);
		cursor = copy;
		nk_style_set_font(ctx, &ap->mono->handle);
		while ((line = sku_next_line(&cursor)) != NULL) {
			nk_layout_row_dynamic(ctx, (float)ap->prefs.text_size + 4, 1);
			nk_label(ctx, line, NK_TEXT_LEFT);
		}
		nk_style_set_font(ctx, &ap->font->handle);
		free(copy);
		nk_group_end(ctx);
	}
}

void ui_detail(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	float row = (float)ap->prefs.text_size + 10;
	app_item *it;
	bool busy;
	char buf[300];
	struct nk_color col;

	if (ap->selected < 0 || ap->selected >= ap->nitems) { ap->view = VIEW_LIST; return; }
	it = &ap->items[ap->selected];
	busy = atomic_load(&it->busy) != 0;

	/* actions */
	nk_layout_row_dynamic(ctx, row, 5);
	if (nk_button_label(ctx, "Back to list")) { ap->view = VIEW_LIST; return; }
	if (busy) {
		nk_label(ctx, "", NK_TEXT_LEFT); nk_label(ctx, "", NK_TEXT_LEFT); nk_label(ctx, "", NK_TEXT_LEFT);
		if (nk_button_label(ctx, "Cancel")) sku_addon_cancel(it->a);
	} else {
		if (nk_button_label(ctx, "Reload config")) jobs_submit(ap->jobs, it, JOB_RELOAD);
		if (nk_button_label(ctx, "Check updates")) jobs_submit(ap->jobs, it, JOB_CHECK);
		if (nk_button_label(ctx, "Verify integrity")) jobs_submit(ap->jobs, it, JOB_CHECK_FULL);
		if (nk_button_label(ctx, "Update / repair")) jobs_submit(ap->jobs, it, JOB_UPDATE);
	}

	/* configuration */
	form_row(ctx, row, "Name:", it->name, NULL);
	if (it->checked && strcmp(it->version, it->remote_version) != 0) sku_snprintf(buf, sizeof buf, "%s   (remote: %s)", it->version, it->remote_version);
	else sku_strlcpy(buf, it->version, sizeof buf);
	form_row(ctx, row, "Version:", buf, NULL);
	ui_left_truncate(ap, it->dir, nk_window_get_width(ctx) - 210, buf, sizeof buf);
	form_row(ctx, row, "Folder:", buf, NULL);
	form_row(ctx, row, "Source:", it->source, NULL);
	col = status_color(it->st, ap->dark);
	form_row(ctx, row, "Status:", it->st_text, &col);
	if (it->st_detail[0]) form_row(ctx, row, "Extra info:", it->st_detail, NULL);
	if (busy && it->prog_total > 0) {
		nk_size cur = (nk_size)it->prog_done, max = (nk_size)it->prog_total;
		nk_layout_row_begin(ctx, NK_STATIC, row, 2);
		nk_layout_row_push(ctx, 170); nk_label(ctx, "Progress:", NK_TEXT_RIGHT);
		nk_layout_row_push(ctx, nk_window_get_width(ctx) - 200); nk_progress(ctx, &cur, max, nk_false);
		nk_layout_row_end(ctx);
	}

	/* settings: only touchable when idle */
	nk_layout_row_begin(ctx, NK_STATIC, row, 3);
	nk_layout_row_push(ctx, 170); nk_label(ctx, "", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, 250);
	{
		nk_bool enabled = !it->disabled;
		if (nk_checkbox_label(ctx, "Enable online updates", &enabled) && !busy) { sku_addon_set_disabled(it->a, !enabled, false); app_refresh_item(it); }
	}
	nk_layout_row_push(ctx, 250);
	if (it->genre == SKU_GENRE_AIRCRAFT) {
		nk_bool liv = it->liveries;
		if (nk_checkbox_label(ctx, "Enable liveries updates", &liv) && !busy) { sku_addon_set_liveries(it->a, liv, false); app_refresh_item(it); }
	} else nk_label(ctx, "", NK_TEXT_LEFT);
	nk_layout_row_end(ctx);

	if (it->nchannels) {
		const char *names[APP_MAX_CHANNELS + 1];
		int sel = 0, n = 0;
		names[n++] = "stable";
		for (int i = 0; i < it->nchannels; i++) { if (strcmp(it->channels[i], it->channel) == 0) sel = n; names[n++] = it->channels[i]; }
		nk_layout_row_begin(ctx, NK_STATIC, row, 2);
		nk_layout_row_push(ctx, 170); nk_label(ctx, "Channel:", NK_TEXT_RIGHT);
		nk_layout_row_push(ctx, 250);
		{
			int now = nk_combo(ctx, names, n, sel, (int)row, nk_vec2(250, 200));
			if (now != sel && !busy) { sku_addon_set_channel(it->a, names[now], false); app_refresh_item(it); }
		}
		nk_layout_row_end(ctx);
	}

	/* counters */
	if (it->checked || it->n_new + it->n_changed + it->n_delete) {
		char nice[32];
		sku_nice_size(nice, sizeof nice, it->bytes);
		sku_snprintf(buf, sizeof buf, "%d missing, %d outdated, %d to delete, %d unchanged, %d ignored, %d error(s), %s to download",
			it->n_new, it->n_changed, it->n_delete, it->n_unchanged, it->n_ignored, it->n_error, nice);
		form_row(ctx, row, "Files:", buf, NULL);
	}

	/* sub tabs */
	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, 120);
	if (nk_selectable_label(ctx, "Files", NK_TEXT_CENTERED, &(nk_bool){ ap->detail_tab == 0 })) ap->detail_tab = 0;
	nk_layout_row_push(ctx, 120);
	if (nk_selectable_label(ctx, "Report", NK_TEXT_CENTERED, &(nk_bool){ ap->detail_tab == 1 })) ap->detail_tab = 1;
	nk_layout_row_end(ctx);
	{
		struct nk_rect r = nk_window_get_content_region(ctx);
		float used = nk_window_get_content_region(ctx).y;
		float height = r.h - (ctx->current->layout->at_y - used) - 10;
		if (height < 3 * row) height = 3 * row;
		if (ap->detail_tab == 0) {
			if (busy) { nk_layout_row_dynamic(ctx, row, 1); nk_label(ctx, "Working...", NK_TEXT_LEFT); }
			else files_tab(ap, it, row, height);
		} else report_tab(ap, ap->selected, height);
	}
}
