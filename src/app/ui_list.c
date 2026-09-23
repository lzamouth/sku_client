/* Addons tab: filters, global actions, the addon list. */
#include "app.h"
#include "nuklear_config.h"
#include <string.h>

static bool visible(app *ap, const app_item *it)
{
	switch (it->genre) {
	case SKU_GENRE_AIRCRAFT: return ap->filter_aircraft;
	case SKU_GENRE_SCENERY:  return ap->filter_scenery;
	case SKU_GENRE_PLUGIN:   return ap->filter_plugins;
	default:                 return ap->filter_other;
	}
}

static void submit_all(app *ap, job_kind kind)
{
	for (int i = 0; i < ap->nitems; i++)
		if (visible(ap, &ap->items[i]) && !ap->items[i].disabled) jobs_submit(ap->jobs, &ap->items[i], kind);
}

/* Status cell text: live progress while busy, else the status text. */
static void status_text(const app_item *it, char *out, size_t n)
{
	if (it->st == SKU_ST_CHECKING && it->prog_total > 0)
		sku_snprintf(out, n, "Checking %d/%d files", it->prog_done, it->prog_total);
	else if (it->st == SKU_ST_UPDATING && it->prog_total > 0) {
		char done[32], total[32];
		sku_nice_size(done, sizeof done, it->bytes_done);
		sku_nice_size(total, sizeof total, it->bytes_total);
		sku_snprintf(out, n, "Updating %d/%d files, %s / %s", it->prog_done, it->prog_total, done, total);
	} else sku_strlcpy(out, it->st_text, n);
}

void ui_list(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	float row = (float)ap->prefs.text_size + 10;
	int nvisible = 0, *idx;
	bool busy = app_any_busy(ap);
	char buf[300];

	/* row 1: filters + rescan */
	nk_layout_row_begin(ctx, NK_STATIC, row, 6);
	nk_layout_row_push(ctx, 60);  nk_label(ctx, "Filters:", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, 100); nk_checkbox_label(ctx, "Aircraft", &(nk_bool){ ap->filter_aircraft }) ? (ap->filter_aircraft = !ap->filter_aircraft) : 0;
	nk_layout_row_push(ctx, 100); nk_checkbox_label(ctx, "Sceneries", &(nk_bool){ ap->filter_scenery }) ? (ap->filter_scenery = !ap->filter_scenery) : 0;
	nk_layout_row_push(ctx, 100); nk_checkbox_label(ctx, "Plugins", &(nk_bool){ ap->filter_plugins }) ? (ap->filter_plugins = !ap->filter_plugins) : 0;
	nk_layout_row_push(ctx, 100); nk_checkbox_label(ctx, "Other", &(nk_bool){ ap->filter_other }) ? (ap->filter_other = !ap->filter_other) : 0;
	nk_layout_row_push(ctx, 200);
	if (busy) nk_label(ctx, "", NK_TEXT_LEFT);
	else if (nk_button_label(ctx, "Check for new addons")) app_rescan(ap);
	nk_layout_row_end(ctx);

	/* row 2: counts + global actions */
	for (int i = 0; i < ap->nitems; i++) if (visible(ap, &ap->items[i])) nvisible++;
	nk_layout_row_dynamic(ctx, row, 4);
	sku_snprintf(buf, sizeof buf, "Managed addons: %d", nvisible);
	nk_label(ctx, buf, NK_TEXT_LEFT);
	if (nk_button_label(ctx, "Reload all configs")) submit_all(ap, JOB_RELOAD);
	if (nk_button_label(ctx, "Check all updates")) submit_all(ap, JOB_CHECK);
	if (busy) { if (nk_button_label(ctx, "Cancel all")) jobs_cancel_all(ap); }
	else if (nk_button_label(ctx, "Update / repair all")) submit_all(ap, JOB_UPDATE);

	/* the list: standalone addons, then one row per pack (index >= nitems) */
	idx = alloca((size_t)(ap->nitems + ap->packs.n + 1) * sizeof *idx);
	nvisible = 0;
	for (int i = 0; i < ap->nitems; i++) if (ap->items[i].pack < 0 && visible(ap, &ap->items[i])) idx[nvisible++] = i;
	for (int p = 0; p < ap->packs.n; p++) {
		bool any = false;
		for (int m = 0; m < ap->packs.v[p].nmembers && !any; m++) any = visible(ap, &ap->items[ap->packs.v[p].members[m]]);
		if (any) idx[nvisible++] = ap->nitems + p;
	}

	nk_layout_row_dynamic(ctx, nk_window_get_height(ctx) - 3 * row - 40, 1);
	{
		struct nk_list_view view;
		if (nk_list_view_begin(ctx, &view, "addons", NK_WINDOW_BORDER, (int)row, nvisible)) {
			for (int k = view.begin; k < view.end && k < nvisible; k++) {
				app_item *it;
				bool clicked = false;
				nk_bool sel;
				if (idx[k] >= ap->nitems) {
					/* a pack row */
					int pi = idx[k] - ap->nitems;
					char version[64], text[256];
					sku_status st;
					app_pack_summary(ap, pi, version, sizeof version, &st, text, sizeof text);
					sel = ap->selected_pack == pi;
					nk_layout_row_template_begin(ctx, row - 4);
					nk_layout_row_template_push_static(ctx, 56);
					nk_layout_row_template_push_static(ctx, 250);
					nk_layout_row_template_push_static(ctx, 100);
					nk_layout_row_template_push_dynamic(ctx);
					nk_layout_row_template_end(ctx);
					clicked |= nk_widget_has_mouse_click_down(ctx, NK_BUTTON_LEFT, nk_true);
					nk_label_colored(ctx, "pack", NK_TEXT_CENTERED, nk_rgb(255, 200, 80));
					clicked |= nk_selectable_label(ctx, ap->packs.v[pi].name, NK_TEXT_LEFT, &sel) != 0;
					clicked |= nk_widget_has_mouse_click_down(ctx, NK_BUTTON_LEFT, nk_true);
					nk_label(ctx, version, NK_TEXT_LEFT);
					clicked |= nk_widget_has_mouse_click_down(ctx, NK_BUTTON_LEFT, nk_true);
					nk_label_colored(ctx, text, NK_TEXT_LEFT, status_color(st, ap->dark));
					if (clicked) { ap->selected_pack = pi; ap->view = VIEW_PACK; }
					continue;
				}
				it = &ap->items[idx[k]];
				sel = ap->selected == idx[k];
				nk_layout_row_template_begin(ctx, row - 4);
				nk_layout_row_template_push_static(ctx, 56);
				nk_layout_row_template_push_static(ctx, 250);
				nk_layout_row_template_push_static(ctx, 100);
				nk_layout_row_template_push_dynamic(ctx);
				nk_layout_row_template_end(ctx);

				/* a press anywhere on the row opens the addon (Nuklear widgets trigger on press) */
				clicked |= nk_widget_has_mouse_click_down(ctx, NK_BUTTON_LEFT, nk_true);
				if (strcmp(it->channel, "stable") != 0) nk_label_colored(ctx, it->channel, NK_TEXT_CENTERED, nk_rgb(255, 120, 60));
				else if (it->nchannels) nk_label_colored(ctx, "beta?", NK_TEXT_CENTERED, ap->dark ? nk_rgb(110, 110, 110) : nk_rgb(160, 160, 160));
				else nk_label(ctx, "", NK_TEXT_CENTERED);

				clicked |= nk_selectable_label(ctx, it->name, NK_TEXT_LEFT, &sel) != 0;
				clicked |= nk_widget_has_mouse_click_down(ctx, NK_BUTTON_LEFT, nk_true);
				nk_label(ctx, it->version, NK_TEXT_LEFT);
				clicked |= nk_widget_has_mouse_click_down(ctx, NK_BUTTON_LEFT, nk_true);
				status_text(it, buf, sizeof buf);
				nk_label_colored(ctx, buf, NK_TEXT_LEFT, status_color(it->st, ap->dark));

				if (clicked) {
					ap->selected = idx[k];
					ap->view = VIEW_DETAIL;
					ap->detail_tab = 0;
				}
			}
			nk_list_view_end(&view);
		}
	}
}
