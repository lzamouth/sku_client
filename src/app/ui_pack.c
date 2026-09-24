/* Pack view: the members of one pack, their aggregated status, actions on
 * all of them, and creation of declared-but-missing members (on request). */
#include "app.h"
#include "nuklear_config.h"
#include "pack.h"
#include <stdlib.h>
#include <string.h>

/* Worst-first ranking of member statuses for the aggregated line. */
static int rank(sku_status s)
{
	switch (s) {
	case SKU_ST_UPDATING: case SKU_ST_CHECKING: return 9;
	case SKU_ST_ERROR_CONFIG: case SKU_ST_ERROR_NET: case SKU_ST_ERROR_FILES: return 8;
	case SKU_ST_CANCELLED: return 7;
	case SKU_ST_UPDATE: return 6;
	case SKU_ST_REPAIR: return 5;
	case SKU_ST_VERSION_MISMATCH: return 4;
	case SKU_ST_LOCKED: return 3;
	case SKU_ST_UPDATED: case SKU_ST_UP_TO_DATE: return 2;
	case SKU_ST_DISABLED: return 1;
	case SKU_ST_LOCAL: return 0;
	}
	return 0;
}

void app_pack_summary(app *ap, int pack, char *version, size_t vn, sku_status *st, char *text, size_t tn)
{
	const sku_pack *p = &ap->packs.v[pack];
	int counts[16] = {0}, busy = 0;
	sku_status worst = SKU_ST_LOCAL;
	version[0] = 0;
	for (int m = 0; m < p->nmembers; m++) {
		app_item *it = &ap->items[p->members[m]];
		if (!version[0]) sku_strlcpy(version, it->version, vn);
		else if (strcmp(version, it->version) != 0) sku_strlcpy(version, "mixed", vn);
		if (rank(it->st) > rank(worst)) worst = it->st;
		counts[it->st]++;
		if (atomic_load(&it->busy)) busy++;
	}
	*st = worst;
	if (busy) sku_snprintf(text, tn, "%d of %d member(s) working", busy, p->nmembers);
	else if (counts[SKU_ST_UPDATE] + counts[SKU_ST_REPAIR] + counts[SKU_ST_VERSION_MISMATCH])
		sku_snprintf(text, tn, "%d of %d member(s) need an update or repair", counts[SKU_ST_UPDATE] + counts[SKU_ST_REPAIR] + counts[SKU_ST_VERSION_MISMATCH], p->nmembers);
	else if (counts[SKU_ST_ERROR_CONFIG] + counts[SKU_ST_ERROR_NET] + counts[SKU_ST_ERROR_FILES])
		sku_snprintf(text, tn, "%d member(s) in error", counts[SKU_ST_ERROR_CONFIG] + counts[SKU_ST_ERROR_NET] + counts[SKU_ST_ERROR_FILES]);
	else if (counts[SKU_ST_UP_TO_DATE] + counts[SKU_ST_UPDATED] == p->nmembers) sku_snprintf(text, tn, "All %d member(s) up to date", p->nmembers);
	else sku_snprintf(text, tn, "%d member(s), %d checked", p->nmembers, counts[SKU_ST_UP_TO_DATE] + counts[SKU_ST_UPDATED] + counts[SKU_ST_UPDATE] + counts[SKU_ST_REPAIR]);
}

void app_packs_rebuild(app *ap)
{
	sku_addon **addons = sku_xcalloc((size_t)ap->nitems + 1, sizeof *addons);
	sku_pack_list_free(&ap->packs);
	for (int i = 0; i < ap->nitems; i++) { addons[i] = ap->items[i].a; ap->items[i].pack = -1; }
	sku_packs_build(addons, ap->nitems, &ap->packs);
	for (int p = 0; p < ap->packs.n; p++)
		for (int m = 0; m < ap->packs.v[p].nmembers; m++) ap->items[ap->packs.v[p].members[m]].pack = p;
	free(addons);
	free(ap->missing); ap->missing = NULL; ap->nmissing = 0; ap->missing_for = -1;
}

static void load_missing(app *ap, int pack)
{
	sku_addon **addons;
	if (ap->missing_for == pack) return;
	addons = sku_xcalloc((size_t)ap->nitems + 1, sizeof *addons);
	for (int i = 0; i < ap->nitems; i++) addons[i] = ap->items[i].a;
	free(ap->missing);
	ap->nmissing = sku_pack_missing(&ap->packs.v[pack], addons, &ap->missing);
	ap->missing_for = pack;
	free(addons);
}

void ui_pack(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	float row = (float)ap->prefs.text_size + 10;
	const sku_pack *p;
	char version[64], text[256], buf[SKU_PATH_MAX];
	sku_status st;
	bool busy = false;
	static bool confirm;

	if (ap->selected_pack < 0 || ap->selected_pack >= ap->packs.n) { ap->view = VIEW_LIST; return; }
	p = &ap->packs.v[ap->selected_pack];
	for (int m = 0; m < p->nmembers; m++) if (atomic_load(&ap->items[p->members[m]].busy)) busy = true;
	app_pack_summary(ap, ap->selected_pack, version, sizeof version, &st, text, sizeof text);

	nk_layout_row_dynamic(ctx, row, 4);
	if (ui_button(ap, "Back to list")) { ap->view = VIEW_LIST; confirm = false; return; }
	if (busy) {
		nk_label(ctx, "", NK_TEXT_LEFT); nk_label(ctx, "", NK_TEXT_LEFT);
		if (ui_button(ap, "Cancel")) for (int m = 0; m < p->nmembers; m++) sku_addon_cancel(ap->items[p->members[m]].a);
	} else {
		if (ui_button(ap, "Check all members")) for (int m = 0; m < p->nmembers; m++) if (!ap->items[p->members[m]].disabled) jobs_submit(ap->jobs, &ap->items[p->members[m]], JOB_CHECK);
		if (ui_button(ap, "Verify integrity")) for (int m = 0; m < p->nmembers; m++) if (!ap->items[p->members[m]].disabled) jobs_submit(ap->jobs, &ap->items[p->members[m]], JOB_CHECK_FULL);
		if (ui_button(ap, "Update / repair all")) for (int m = 0; m < p->nmembers; m++) if (!ap->items[p->members[m]].disabled) jobs_submit(ap->jobs, &ap->items[p->members[m]], JOB_UPDATE);
	}

	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, UI_W(ap, 170)); nk_label(ctx, "Pack:", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, UI_W(ap, 600)); sku_snprintf(buf, sizeof buf, "%s   (id %s, %d member(s))", p->name, p->id, p->nmembers); nk_label(ctx, buf, NK_TEXT_LEFT);
	nk_layout_row_end(ctx);
	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, UI_W(ap, 170)); nk_label(ctx, "Version:", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, UI_W(ap, 600)); nk_label(ctx, version, NK_TEXT_LEFT);
	nk_layout_row_end(ctx);
	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, UI_W(ap, 170)); nk_label(ctx, "Status:", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, UI_W(ap, 600)); nk_label_colored(ctx, text, NK_TEXT_LEFT, status_color(st, ap->dark));
	nk_layout_row_end(ctx);

	/* declared but missing members */
	load_missing(ap, ap->selected_pack);
	if (ap->nmissing) {
		nk_layout_row_dynamic(ctx, row, 1);
		sku_snprintf(buf, sizeof buf, "%d member folder(s) declared by this pack are missing:", ap->nmissing);
		nk_label_colored(ctx, buf, NK_TEXT_LEFT, nk_rgb(255, 165, 0));
		for (int k = 0; k < ap->nmissing; k++) {
			nk_layout_row_dynamic(ctx, row - 4, 1);
			sku_snprintf(buf, sizeof buf, "      %s  ->  <X-Plane>/%s", ap->missing[k].member_id, ap->missing[k].path);
			nk_label(ctx, buf, NK_TEXT_LEFT);
		}
		nk_layout_row_dynamic(ctx, row, 3);
		if (!confirm) {
			if (ui_button(ap, "Create the missing folders...") && !busy) confirm = true;
		} else {
			nk_label_colored(ctx, "Create them now?", NK_TEXT_RIGHT, nk_rgb(255, 165, 0));
			if (ui_button(ap, "Yes, create")) {
				int created = 0;
				for (int k = 0; k < ap->nmissing; k++)
					if (sku_pack_create_member(&ap->inst, p, &ap->missing[k], NULL, 0) == SKU_OK) created++;
				app_message(ap, "%d member folder(s) created; run an update to fill them", created);
				confirm = false;
				app_rescan(ap);
				return;   /* items and packs were rebuilt */
			}
			if (ui_button(ap, "No")) confirm = false;
		}
	}

	/* members */
	nk_layout_row_dynamic(ctx, row, 1);
	nk_label(ctx, "Members (click one for its details):", NK_TEXT_LEFT);
	nk_layout_row_dynamic(ctx, nk_window_get_height(ctx) - (ap->nmissing ? 8 + ap->nmissing : 6) * row - 20, 1);
	if (nk_group_begin(ctx, "members", NK_WINDOW_BORDER)) {
		for (int m = 0; m < p->nmembers; m++) {
			app_item *it = &ap->items[p->members[m]];
			bool clicked = false;
			nk_bool sel = nk_false;
			nk_layout_row_template_begin(ctx, row - 4);
			nk_layout_row_template_push_static(ctx, 250);
			nk_layout_row_template_push_static(ctx, 100);
			nk_layout_row_template_push_dynamic(ctx);
			nk_layout_row_template_end(ctx);
			clicked |= nk_selectable_label(ctx, it->name, NK_TEXT_LEFT, &sel) != 0;
			clicked |= nk_widget_has_mouse_click_down(ctx, NK_BUTTON_LEFT, nk_true);
			nk_label(ctx, it->version, NK_TEXT_LEFT);
			clicked |= nk_widget_has_mouse_click_down(ctx, NK_BUTTON_LEFT, nk_true);
			nk_label_colored(ctx, it->st_text, NK_TEXT_LEFT, status_color(it->st, ap->dark));
			if (clicked) { ap->selected = p->members[m]; ap->view = VIEW_DETAIL; ap->detail_tab = 0; ap->detail_from_pack = true; }
		}
		nk_group_end(ctx);
	}
}
