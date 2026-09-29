/* Detail view of one addon: actions, configuration, counters, files, report. */
#include "app.h"
#include "nuklear_config.h"
#include "platform.h"
#include "report.h"
#include "changelog.h"
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

static void form_row(app *ap, float row, const char *label, const char *value, struct nk_color *color)
{
	struct nk_context *ctx = ap->ctx;
	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, UI_W(ap, 170));
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
	if (nk_list_view_begin(ctx, &view, "files", NK_WINDOW_BORDER, (int)(row - 4), p->n ? p->n : 1)) {
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
		ui_group_tail(ap);
		nk_group_end(ctx);
	}
}

/* Changelog (with newer sections highlighted) or readme, wrapped. */
static void doc_tab(app *ap, int index, int kind, float height)
{
	struct nk_context *ctx = ap->ctx;
	app_item *it = &ap->items[index];
	const char *name = kind == 2 ? "changelog" : "readme";
	char path[SKU_PATH_MAX];
	sku_changelog_section *sec = NULL;
	int ns = 0, lineno = 0;
	char *copy, *cursor, *line;

	if (ap->doc_for != index || ap->doc_kind != kind || !ap->doc_text) {
		free(ap->doc_text); ap->doc_text = NULL;
		ap->doc_for = index; ap->doc_kind = kind;
		if (sku_addon_doc_path(it->a, name, path, sizeof path) != SKU_OK || sku_read_file(path, &ap->doc_text, NULL) != SKU_OK)
			ap->doc_text = sku_xstrdup(kind == 2 ? "No changelog available: run a check first, or this addon does not publish one."
			                                     : "No readme available: run a check first, or this addon does not publish one.");
	}
	if (kind == 2) ns = sku_changelog_sections(ap->doc_text, it->version, &sec);
	nk_layout_row_dynamic(ctx, height, 1);
	if (nk_group_begin(ctx, name, NK_WINDOW_BORDER)) {
		copy = sku_xstrdup(ap->doc_text); cursor = sku_skip_bom(copy);
		while ((line = sku_next_line(&cursor)) != NULL) {
			bool newer = false, header = false;
			char version[64];
			for (int s = 0; s < ns; s++) if (lineno >= sec[s].first_line && lineno <= sec[s].last_line) { newer = sec[s].newer; header = lineno == sec[s].first_line && sec[s].version[0]; }
			if (!*line) { nk_layout_row_dynamic(ctx, (float)ap->prefs.text_size / 2, 1); nk_label(ctx, "", NK_TEXT_LEFT); lineno++; continue; }
			if (header && sku_changelog_is_header(line, version, sizeof version)) {
				char title[128];
				bool installed = strcmp(version, it->version) == 0 || (it->version[0] == 'v' && strcmp(version, it->version + 1) == 0);
				struct nk_color c = newer ? (ap->dark ? nk_rgb(0, 255, 255) : nk_rgb(0, 130, 160)) : (ap->dark ? nk_rgb(220, 220, 220) : nk_rgb(60, 60, 60));
				sku_snprintf(title, sizeof title, "%s%s", version, newer ? "   (new)" : installed ? "   (installed)" : "");
				/* the marker comes from the regular font (the symbols are merged there), the version is bold */
				nk_layout_row_begin(ctx, NK_STATIC, (float)ap->prefs.text_size + 8, 2);
				nk_layout_row_push(ctx, UI_W(ap, 16));
				nk_label_colored(ctx, newer ? "\xE2\x96\xB6" : "\xE2\x80\xA2", NK_TEXT_LEFT, c);
				nk_layout_row_push(ctx, nk_window_get_content_region(ctx).w - UI_W(ap, 40));
				nk_style_set_font(ctx, &ap->bold->handle);
				nk_label_colored(ctx, title, NK_TEXT_LEFT, c);
				nk_style_set_font(ctx, &ap->font->handle);
				nk_layout_row_end(ctx);
			} else {
				const struct nk_user_font *f = ctx->style.font;
				char tidy[1024];
				float w;
				line = (char *)ui_tidy_line(line, tidy, sizeof tidy);   /* tab-indented 3.x changelogs */
				w = f->width(f->userdata, f->height, line, (int)strlen(line));
				float avail = nk_window_get_content_region(ctx).w - 30;
				int lines = avail > 0 ? (int)(w / avail) + 1 : 1;
				nk_layout_row_dynamic(ctx, (float)lines * ((float)ap->prefs.text_size + 4), 1);
				/* changelog: older sections dimmed; readme: plain text */
				if (newer || kind != 2) nk_label_colored_wrap(ctx, line, ap->dark ? nk_rgb(240, 240, 240) : nk_rgb(30, 30, 30));
				else nk_label_colored_wrap(ctx, line, ap->dark ? nk_rgb(160, 160, 160) : nk_rgb(110, 110, 110));
			}
			lineno++;
		}
		free(copy);
		ui_group_tail(ap);
		nk_group_end(ctx);
	}
	free(sec);
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
	if (ui_button(ap, ap->detail_from_pack ? "Back to pack" : "Back to list")) { ap->view = ap->detail_from_pack ? VIEW_PACK : VIEW_LIST; ap->detail_from_pack = false; return; }
	if (busy) {
		nk_label(ctx, "", NK_TEXT_LEFT); nk_label(ctx, "", NK_TEXT_LEFT); nk_label(ctx, "", NK_TEXT_LEFT);
		if (ui_button(ap, "Cancel")) sku_addon_cancel(it->a);
	} else {
		if (ui_button(ap, "Reload config")) jobs_submit(ap->jobs, it, JOB_RELOAD);
		if (ui_button(ap, "Check updates")) jobs_submit(ap->jobs, it, JOB_CHECK);
		if (ui_button(ap, "Verify integrity")) jobs_submit(ap->jobs, it, JOB_CHECK_FULL);
		if (ui_button_hl(ap, "Update / repair", ui_needs_update(it->st))) jobs_submit(ap->jobs, it, JOB_UPDATE);
	}

	/* configuration */
	form_row(ap, row, "Name:", it->name, NULL);
	if (it->checked && strcmp(it->version, it->remote_version) != 0) sku_snprintf(buf, sizeof buf, "%s   (remote: %s)", it->version, it->remote_version);
	else sku_strlcpy(buf, it->version, sizeof buf);
	form_row(ap, row, "Version:", buf, NULL);
	ui_left_truncate(ap, it->dir, nk_window_get_width(ctx) - 210, buf, sizeof buf);
	form_row(ap, row, "Folder:", buf, NULL);
	form_row(ap, row, "Source:", it->source, NULL);
	col = status_color(it->st, ap->dark);
	form_row(ap, row, "Status:", it->st_text, &col);
	if (it->st_detail[0]) form_row(ap, row, "Extra info:", it->st_detail, NULL);
	if (busy && it->prog_total > 0) {
		nk_size cur = (nk_size)it->prog_done, max = (nk_size)it->prog_total;
		nk_layout_row_begin(ctx, NK_STATIC, row, 2);
		nk_layout_row_push(ctx, UI_W(ap, 170)); nk_label(ctx, "Progress:", NK_TEXT_RIGHT);
		nk_layout_row_push(ctx, nk_window_get_width(ctx) - 200); nk_progress(ctx, &cur, max, nk_false);
		nk_layout_row_end(ctx);
	}

	/* settings: only touchable when idle */
	nk_layout_row_begin(ctx, NK_STATIC, row, 3);
	nk_layout_row_push(ctx, UI_W(ap, 170)); nk_label(ctx, "", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, UI_W(ap, 250));
	{
		nk_bool enabled = !it->disabled;
		if (nk_checkbox_label(ctx, "Enable online updates", &enabled) && !busy) { sku_addon_set_disabled(it->a, !enabled, false); app_refresh_item(it); }
	}
	nk_layout_row_push(ctx, UI_W(ap, 250));
	if (it->genre == SKU_GENRE_AIRCRAFT) {
		nk_bool liv = it->liveries;
		if (nk_checkbox_label(ctx, "Enable liveries updates", &liv) && !busy) { sku_addon_set_liveries(it->a, liv, false); app_refresh_item(it); }
	} else nk_label(ctx, "", NK_TEXT_LEFT);
	nk_layout_row_end(ctx);

	if (it->nchannels) {
		const char *names[APP_MAX_CHANNELS + 1], *labels[APP_MAX_CHANNELS + 1];
		struct nk_color colors[APP_MAX_CHANNELS + 1];
		int sel = 0, n = 0;
		names[n] = "stable"; labels[n] = UI_MAIN_CHANNEL; colors[n] = ui_channel_color(ap, "stable"); n++;
		for (int i = 0; i < it->nchannels; i++) { if (strcmp(it->channels[i], it->channel) == 0) sel = n; colors[n] = ui_channel_color(ap, it->channels[i]); labels[n] = it->channels[i]; names[n++] = it->channels[i]; }
		nk_layout_row_begin(ctx, NK_STATIC, row, 2);
		nk_layout_row_push(ctx, UI_W(ap, 170)); nk_label(ctx, "Channel:", NK_TEXT_RIGHT);
		nk_layout_row_push(ctx, UI_W(ap, 250));
		{
			int now = ui_combo_ex(ap, labels, colors, n, sel, (int)row, UI_W(ap, 250), (float)(n + 1) * row);
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
		form_row(ap, row, "Files:", buf, NULL);
	}

	/* sub tabs */
	nk_layout_row_begin(ctx, NK_STATIC, row, 4);
	nk_layout_row_push(ctx, UI_W(ap, 120));
	if (nk_selectable_label(ctx, "Files", NK_TEXT_CENTERED, &(nk_bool){ ap->detail_tab == 0 })) ap->detail_tab = 0;
	nk_layout_row_push(ctx, UI_W(ap, 120));
	if (nk_selectable_label(ctx, "Report", NK_TEXT_CENTERED, &(nk_bool){ ap->detail_tab == 1 })) ap->detail_tab = 1;
	nk_layout_row_push(ctx, UI_W(ap, 120));
	if (nk_selectable_label(ctx, "Changelog", NK_TEXT_CENTERED, &(nk_bool){ ap->detail_tab == 2 })) ap->detail_tab = 2;
	nk_layout_row_push(ctx, UI_W(ap, 120));
	if (nk_selectable_label(ctx, "Readme", NK_TEXT_CENTERED, &(nk_bool){ ap->detail_tab == 3 })) ap->detail_tab = 3;
	nk_layout_row_end(ctx);
	{
		float height = ui_fill_height(ap, 8, 3 * row);
		if (ap->detail_tab == 0) {
			if (busy) { nk_layout_row_dynamic(ctx, row, 1); nk_label(ctx, "Working...", NK_TEXT_LEFT); }
			else files_tab(ap, it, row, height);
		} else if (ap->detail_tab == 1) report_tab(ap, ap->selected, height);
		else doc_tab(ap, ap->selected, ap->detail_tab, height);
	}
}
