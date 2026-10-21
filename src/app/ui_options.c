/* Options, About and the X-Plane instance chooser. */
#include "app.h"
#include "nuklear_config.h"
#include "platform.h"
#include "http.h"
#include <stdlib.h>
#include <string.h>

extern const unsigned char sku_notices[];
extern const size_t sku_notices_len;

/* The MIT licence, as in LICENSE at the root of the sources. */
static const char *about_text =
	"SkunkCrafts Updater, (c) 2025-2026 Lionel Zamouth\n"
	"Source code: https://github.com/lzamouth/sku_client\n"
	"\n"
	"MIT License\n"
	"\n"
	"Copyright (c) 2025-2026 Lionel Zamouth\n"
	"\n"
	"Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the \"Software\"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\n"
	"\n"
	"The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\n"
	"\n"
	"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n"
	"\n"
	"The third-party components and their licences are listed under \"Third-party notices\".\n";

void ui_options(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	app_prefs *p = &ap->prefs;
	float row = (float)ap->prefs.text_size + 10;
	static const char *startup_items[] = { "Only show the list of managed addons", "Check all available updates", "Perform all available updates" };
	static const char *theme_items[] = { "Follow the system", "Dark", "Light" };
	static const float r2[] = { 0.30f, 0.70f }, r3[] = { 0.30f, 0.50f, 0.20f };
	float avail = nk_window_get_content_region(ctx).w * 0.48f;
	int v;
	char buf[SKU_PATH_MAX + 64];
	bool changed = false;

	nk_layout_row(ctx, NK_DYNAMIC, row, 2, r2);
	nk_label(ctx, "Automatic action on startup:", NK_TEXT_RIGHT);
	v = ui_combo(ap, startup_items, 3, p->startup, (int)row, avail, 4 * row);
	if (v != p->startup) { p->startup = v; changed = true; }

	nk_layout_row(ctx, NK_DYNAMIC, row, 2, r2);
	nk_label(ctx, "Theme:", NK_TEXT_RIGHT);
	v = ui_combo(ap, theme_items, 3, (int)p->theme, (int)row, avail, 4 * row);
	if (v != (int)p->theme) { p->theme = (app_theme)v; app_apply_theme(ap); changed = true; }

	nk_layout_row(ctx, NK_DYNAMIC, row, 2, r2);
	nk_label(ctx, "", NK_TEXT_RIGHT);
	{
		nk_bool relief = p->relief;
		if (nk_checkbox_label(ctx, "Bevelled widgets (relief)", &relief)) { p->relief = relief; app_apply_theme(ap); changed = true; }
	}
#if defined(__linux__)
	{
		static const char *video_items[] = { "Automatic (X11 when available, else Wayland)", "Wayland", "X11" };
		nk_layout_row(ctx, NK_DYNAMIC, row, 3, r3);
		nk_label(ctx, "Window system (next start):", NK_TEXT_RIGHT);
		v = ui_combo(ap, video_items, 3, p->video, (int)row, avail, 4 * row);
		if (v != p->video) { p->video = v; changed = true; }
		sku_snprintf(buf, sizeof buf, "now: %s", SDL_GetCurrentVideoDriver());
		nk_label(ctx, buf, NK_TEXT_LEFT);
	}
#endif

	nk_layout_row(ctx, NK_DYNAMIC, row, 2, r2);
	nk_label(ctx, "", NK_TEXT_RIGHT);
	{
		nk_bool verbose = p->verbose;
		if (nk_checkbox_label(ctx, "Verbose logging (for support only)", &verbose)) { p->verbose = verbose; changed = true; }
	}

	sku_snprintf(buf, sizeof buf, "Max concurrent downloads: %d", p->downloads);
	nk_layout_row(ctx, NK_DYNAMIC, row, 3, r3);
	nk_label(ctx, buf, NK_TEXT_RIGHT);
	v = p->downloads; if (nk_slider_int(ctx, 1, &v, 32, 1) && v != p->downloads) { p->downloads = v; changed = true; }
	nk_label(ctx, "", NK_TEXT_LEFT);

	sku_snprintf(buf, sizeof buf, "Max concurrent CRC checks: %d", p->crc_threads);
	nk_layout_row(ctx, NK_DYNAMIC, row, 3, r3);
	nk_label(ctx, buf, NK_TEXT_RIGHT);
	v = p->crc_threads; if (nk_slider_int(ctx, 1, &v, 64, 1) && v != p->crc_threads) { p->crc_threads = v; changed = true; }
	nk_label(ctx, "", NK_TEXT_LEFT);

	sku_snprintf(buf, sizeof buf, "Text size: %d", p->text_size);
	nk_layout_row(ctx, NK_DYNAMIC, row, 3, r3);
	nk_label(ctx, buf, NK_TEXT_RIGHT);
	v = p->text_size; if (nk_slider_int(ctx, 10, &v, 32, 1) && v != p->text_size) { app_set_text_size(ap, v); changed = true; }
	nk_label(ctx, "", NK_TEXT_LEFT);

	nk_layout_row_dynamic(ctx, row / 2, 1); nk_label(ctx, "", NK_TEXT_LEFT);

	nk_layout_row(ctx, NK_DYNAMIC, row, 3, r3);
	nk_label(ctx, "X-Plane installation:", NK_TEXT_RIGHT);
	if (ap->inst_open) { ui_left_truncate(ap, ap->inst.root, avail, buf, sizeof buf); nk_label(ctx, buf, NK_TEXT_LEFT); } else nk_label(ctx, "(none)", NK_TEXT_LEFT);
	if (ui_button(ap, "Change...") && !app_any_busy(ap)) ap->view = VIEW_INSTANCE;

	nk_layout_row(ctx, NK_DYNAMIC, row, 3, r3);
	nk_label(ctx, "Custom addon folders:", NK_TEXT_RIGHT);
	nk_label(ctx, "Any folder holding a skunkcrafts_updater.cfg", NK_TEXT_LEFT);
	if (ui_button(ap, "Add folder...") && ap->inst_open && !app_any_busy(ap)) app_browse_folder(ap, 2);

	if (ap->inst_open) {
		nk_layout_row(ctx, NK_DYNAMIC, row, 3, r3);
		nk_label(ctx, "Log file:", NK_TEXT_RIGHT);
		ui_left_truncate(ap, ap->inst.log_path, avail, buf, sizeof buf); nk_label(ctx, buf, NK_TEXT_LEFT);
		if (ui_button(ap, "Open folder")) { sku_snprintf(buf, sizeof buf, "file://%s", ap->inst.data_dir); SDL_OpenURL(buf); }
	}

	nk_layout_row_dynamic(ctx, row / 2, 1); nk_label(ctx, "", NK_TEXT_LEFT);
	ui_client_options(ap);

	if (changed) prefs_save(ap);
}

static void wrapped_text(app *ap, const char *text)
{
	struct nk_context *ctx = ap->ctx;
	char *copy = sku_xstrdup(text), *cursor = copy, *line;
	while ((line = sku_next_line(&cursor)) != NULL) {
		if (!*line) { nk_layout_row_dynamic(ctx, (float)ap->prefs.text_size / 2, 1); nk_label(ctx, "", NK_TEXT_LEFT); continue; }
		ui_text_wrapped(ap, line, (float)ap->prefs.text_size + 4, NULL);
	}
	free(copy);
}

void ui_about(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	float row = (float)ap->prefs.text_size + 10;
	char buf[256];
	bool news = atomic_load(&ap->client_state) == CLIENT_CHECKED && ap->client.changelog;

	nk_layout_row_dynamic(ctx, row, 1);
	sku_snprintf(buf, sizeof buf, "%s %s  -  %s / SDL %d.%d.%d", APP_NAME, SKU_VERSION, sku_http_version(),
		SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
	nk_label(ctx, buf, NK_TEXT_LEFT);
	nk_layout_row_dynamic(ctx, row, news ? 4 : 3);
	if (ui_button(ap, "License")) ap->about_mode = 0;
	if (ui_button(ap, "Third-party notices")) ap->about_mode = 1;
	if (news) {
		sku_snprintf(buf, sizeof buf, "What's new (%s)", ap->client.version);
		if (ui_button(ap, buf)) ap->about_mode = 2;
	}
	if (ui_button(ap, "Open skunkcrafts.com")) SDL_OpenURL("https://www.skunkcrafts.com/");
	if (ap->about_mode == 2 && !news) ap->about_mode = 0;

	nk_layout_row_dynamic(ctx, ui_fill_height(ap, 8, 3 * row), 1);
	if (nk_group_begin(ctx, "about", NK_WINDOW_BORDER)) {
		if (ap->about_mode == 2) ui_client_news(ap);
		else if (ap->about_mode == 1) {
			char *text = sku_xmalloc(sku_notices_len + 1);
			memcpy(text, sku_notices, sku_notices_len); text[sku_notices_len] = 0;
			nk_style_set_font(ctx, &ap->mono->handle);
			wrapped_text(ap, text);
			nk_style_set_font(ctx, &ap->font->handle);
			free(text);
		} else wrapped_text(ap, about_text);
		ui_group_tail(ap);
		nk_group_end(ctx);
	}
}

void ui_instance(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	float row = (float)ap->prefs.text_size + 10;
	static bool loaded;
	static int selected = -1;

	if (!loaded) {
		sku_strlist_free(&ap->known_instances);
		sku_instance_known(&ap->known_instances);
		for (int i = 0; i < ap->prefs.instances.n; i++)
			if (sku_instance_is_root(ap->prefs.instances.v[i]) && !sku_strlist_has(&ap->known_instances, ap->prefs.instances.v[i]))
				sku_strlist_push(&ap->known_instances, ap->prefs.instances.v[i]);
		loaded = true;
		selected = -1;
		for (int i = 0; i < ap->known_instances.n; i++) if (strcmp(ap->known_instances.v[i], ap->prefs.instance) == 0) selected = i;
	}

	nk_layout_row_dynamic(ctx, row, 1);
	nk_label(ctx, "Choose the X-Plane installation to manage:", NK_TEXT_LEFT);
	nk_layout_row_dynamic(ctx, row * 8, 1);
	if (nk_group_begin(ctx, "instances", NK_WINDOW_BORDER)) {
		if (!ap->known_instances.n) { nk_layout_row_dynamic(ctx, row, 1); nk_label(ctx, "No installation found: use Browse...", NK_TEXT_LEFT); }
		for (int i = 0; i < ap->known_instances.n; i++) {
			nk_bool sel = selected == i;
			nk_layout_row_dynamic(ctx, row - 4, 1);
			if (nk_selectable_label(ctx, ap->known_instances.v[i], NK_TEXT_LEFT, &sel)) selected = i;
		}
		ui_group_tail(ap);
		nk_group_end(ctx);
	}
	nk_layout_row_dynamic(ctx, row, 4);
	if (ui_button(ap, "Use selected") && selected >= 0 && selected < ap->known_instances.n) { loaded = false; app_open_instance(ap, ap->known_instances.v[selected]); }
	if (ui_button(ap, "Browse...")) { loaded = false; app_browse_folder(ap, 1); }
	if (ap->inst_open && ui_button(ap, "Cancel")) { loaded = false; ap->view = VIEW_LIST; }
	if (!ap->inst_open && ui_button(ap, "Quit")) ap->running = false;
	if (ap->message[0]) { nk_layout_row_dynamic(ctx, row, 1); nk_label_colored(ctx, ap->message, NK_TEXT_LEFT, nk_rgb(255, 165, 0)); }
}

void ui_statusbar(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	float row = (float)ap->prefs.text_size + 8;
	int running = jobs_running(ap->jobs), queued = jobs_queued(ap->jobs), remaining = 0;
	char buf[256];
	for (int i = 0; i < ap->nitems; i++) {
		app_item *it = &ap->items[i];
		if (atomic_load(&it->busy) && it->prog_total > it->prog_done) remaining += it->prog_total - it->prog_done;
	}
	bool message = ap->message[0] && sku_time_ms() < ap->message_until;
	if (running || queued) {
		nk_layout_row_dynamic(ctx, row, 3);
		sku_snprintf(buf, sizeof buf, "Jobs running: %d, queued: %d", running, queued);
		nk_label(ctx, buf, NK_TEXT_LEFT);
		sku_snprintf(buf, sizeof buf, "Files remaining: %d", remaining);
		nk_label(ctx, buf, NK_TEXT_LEFT);
		nk_label(ctx, message ? ap->message : "Working...", NK_TEXT_LEFT);
		return;
	}
	/* idle: no file count to show, the message gets the room (long ones are cut, whole on hover) */
	{
		static const float ratio[] = { 0.25f, 0.75f };
		char cut[sizeof ap->message];
		struct nk_rect r;
		nk_layout_row(ctx, NK_DYNAMIC, row, 2, ratio);
		sku_snprintf(buf, sizeof buf, "Jobs running: %d, queued: %d", running, queued);
		nk_label(ctx, buf, NK_TEXT_LEFT);
		if (!message) { nk_label(ctx, "", NK_TEXT_LEFT); return; }
		r = nk_widget_bounds(ctx);
		ui_right_truncate(ap, ap->message, r.w - 8, cut, sizeof cut);
		if (strcmp(cut, ap->message) != 0 && nk_widget_is_hovered(ctx)) nk_tooltip(ctx, ap->message);
		nk_label(ctx, cut, NK_TEXT_LEFT);
	}
}
