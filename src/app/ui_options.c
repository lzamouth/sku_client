/* Options, About and the X-Plane instance chooser. */
#include "app.h"
#include "nuklear_config.h"
#include "platform.h"
#include "http.h"
#include <stdlib.h>
#include <string.h>

extern const unsigned char sku_notices[];
extern const size_t sku_notices_len;

static const char *about_text =
	"SkunkCrafts Updater, (c) 2025-2026 Lionel Zamouth\n"
	"\n"
	"License\n"
	"This Freeware License Agreement (the 'Agreement') is a legal agreement between you ('Licensee'), the end-user, and Lionel Zamouth for the use of this software ('Software'). Commercial as well as non-commercial use is allowed. By using this Software or storing this program or parts of it on a computer hard drive or any other media, you agree to be bound by the terms of this Agreement. Provided that you verify that you are handling the original freeware version you are hereby licensed to make as many copies of the freeware version of this Software and documentation as long as you do not charge money or request donations for such copies. You may not alter this Software in any way. You may not modify, rent or sell this Software, or create derivative works based upon this Software.\n"
	"\n"
	"Governing Law\n"
	"This agreement shall be governed by the laws of France. If any portion of this Agreement is deemed unenforceable by a court of competent jurisdiction, it shall not affect the forcibility of the other portions of this Agreement.\n"
	"\n"
	"Limited Warranty and Disclaimer of Warranty\n"
	"This software is provided by the author and contributors 'as is' and any express or implied warranties, including, but not limited to, the implied warranties of merchantability and fitness for a particular purpose are disclaimed. In no event shall the author or contributors be liable for any direct, indirect, incidental, special, exemplary, or consequential damages (including, but not limited to, procurement of substitute goods or services; loss of use, data, or profits; or business interruption) however caused and on any theory of liability, whether in contract, strict liability, or tort (including negligence or otherwise) arising in any way out of the use of this software, even if advised of the possibility of such damage. Roses are red, Violets are blue, licenses are boring.\n";

void ui_options(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	app_prefs *p = &ap->prefs;
	float row = (float)ap->prefs.text_size + 10;
	static const char *startup_items[] = { "Only show the list of managed addons", "Check all available updates", "Perform all available updates" };
	static const char *theme_items[] = { "Follow the system", "Dark", "Light" };
	int v;
	char buf[SKU_PATH_MAX + 64];
	bool changed = false;

	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, 260); nk_label(ctx, "Automatic action on startup:", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, 360);
	v = nk_combo(ctx, startup_items, 3, p->startup, (int)row, nk_vec2(360, 160));
	if (v != p->startup) { p->startup = v; changed = true; }
	nk_layout_row_end(ctx);

	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, 260); nk_label(ctx, "Theme:", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, 360);
	v = nk_combo(ctx, theme_items, 3, (int)p->theme, (int)row, nk_vec2(360, 160));
	if (v != (int)p->theme) { p->theme = (app_theme)v; app_apply_theme(ap); changed = true; }
	nk_layout_row_end(ctx);

	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, 260); nk_label(ctx, "", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, 360);
	{
		nk_bool verbose = p->verbose;
		if (nk_checkbox_label(ctx, "Verbose logging (for support only)", &verbose)) { p->verbose = verbose; changed = true; }
	}
	nk_layout_row_end(ctx);

	sku_snprintf(buf, sizeof buf, "Max concurrent downloads: %d", p->downloads);
	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, 260); nk_label(ctx, buf, NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, 360);
	v = p->downloads; if (nk_slider_int(ctx, 1, &v, 32, 1) && v != p->downloads) { p->downloads = v; changed = true; }
	nk_layout_row_end(ctx);

	sku_snprintf(buf, sizeof buf, "Max concurrent CRC checks: %d", p->crc_threads);
	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, 260); nk_label(ctx, buf, NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, 360);
	v = p->crc_threads; if (nk_slider_int(ctx, 1, &v, 64, 1) && v != p->crc_threads) { p->crc_threads = v; changed = true; }
	nk_layout_row_end(ctx);

	sku_snprintf(buf, sizeof buf, "Text size: %d", p->text_size);
	nk_layout_row_begin(ctx, NK_STATIC, row, 2);
	nk_layout_row_push(ctx, 260); nk_label(ctx, buf, NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, 360);
	v = p->text_size; if (nk_slider_int(ctx, 10, &v, 32, 1) && v != p->text_size) { app_set_text_size(ap, v); changed = true; }
	nk_layout_row_end(ctx);

	nk_layout_row_dynamic(ctx, row / 2, 1); nk_label(ctx, "", NK_TEXT_LEFT);

	nk_layout_row_begin(ctx, NK_STATIC, row, 3);
	nk_layout_row_push(ctx, 260); nk_label(ctx, "X-Plane installation:", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, 360);
	if (ap->inst_open) { ui_left_truncate(ap, ap->inst.root, 350, buf, sizeof buf); nk_label(ctx, buf, NK_TEXT_LEFT); } else nk_label(ctx, "(none)", NK_TEXT_LEFT);
	nk_layout_row_push(ctx, 160);
	if (nk_button_label(ctx, "Change...") && !app_any_busy(ap)) ap->view = VIEW_INSTANCE;
	nk_layout_row_end(ctx);

	nk_layout_row_begin(ctx, NK_STATIC, row, 3);
	nk_layout_row_push(ctx, 260); nk_label(ctx, "Custom addon folders:", NK_TEXT_RIGHT);
	nk_layout_row_push(ctx, 360); nk_label(ctx, "Any folder holding a skunkcrafts_updater.cfg", NK_TEXT_LEFT);
	nk_layout_row_push(ctx, 160);
	if (nk_button_label(ctx, "Add folder...") && ap->inst_open && !app_any_busy(ap)) app_browse_folder(ap, 2);
	nk_layout_row_end(ctx);

	if (ap->inst_open) {
		nk_layout_row_begin(ctx, NK_STATIC, row, 3);
		nk_layout_row_push(ctx, 260); nk_label(ctx, "Log file:", NK_TEXT_RIGHT);
		nk_layout_row_push(ctx, 360); ui_left_truncate(ap, ap->inst.log_path, 350, buf, sizeof buf); nk_label(ctx, buf, NK_TEXT_LEFT);
		nk_layout_row_push(ctx, 160);
		if (nk_button_label(ctx, "Open folder")) { sku_snprintf(buf, sizeof buf, "file://%s", ap->inst.data_dir); SDL_OpenURL(buf); }
		nk_layout_row_end(ctx);
	}

	if (changed) prefs_save(ap);
}

static void wrapped_text(app *ap, const char *text)
{
	struct nk_context *ctx = ap->ctx;
	char *copy = sku_xstrdup(text), *cursor = copy, *line;
	while ((line = sku_next_line(&cursor)) != NULL) {
		if (!*line) { nk_layout_row_dynamic(ctx, (float)ap->prefs.text_size / 2, 1); nk_label(ctx, "", NK_TEXT_LEFT); continue; }
		{
			/* estimate the wrapped height from the width */
			const struct nk_user_font *f = ctx->style.font;
			float w = f->width(f->userdata, f->height, line, (int)strlen(line));
			float avail = nk_window_get_content_region(ctx).w - 20;
			int lines = avail > 0 ? (int)(w / avail) + 1 : 1;
			nk_layout_row_dynamic(ctx, (float)lines * ((float)ap->prefs.text_size + 4), 1);
			nk_label_wrap(ctx, line);
		}
	}
	free(copy);
}

void ui_about(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	float row = (float)ap->prefs.text_size + 10;
	static bool show_notices;
	char buf[256];

	nk_layout_row_dynamic(ctx, row, 1);
	sku_snprintf(buf, sizeof buf, "%s %s  -  %s / SDL %d.%d.%d", APP_NAME, SKU_VERSION, sku_http_version(),
		SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()));
	nk_label(ctx, buf, NK_TEXT_LEFT);
	nk_layout_row_dynamic(ctx, row, 2);
	if (nk_button_label(ctx, show_notices ? "Show the license" : "Show third-party notices")) show_notices = !show_notices;
	if (nk_button_label(ctx, "Open skunkcrafts.com")) SDL_OpenURL("https://www.skunkcrafts.com/");

	nk_layout_row_dynamic(ctx, nk_window_get_height(ctx) - 3 * row, 1);
	if (nk_group_begin(ctx, "about", NK_WINDOW_BORDER)) {
		if (show_notices) {
			char *text = sku_xmalloc(sku_notices_len + 1);
			memcpy(text, sku_notices, sku_notices_len); text[sku_notices_len] = 0;
			nk_style_set_font(ctx, &ap->mono->handle);
			wrapped_text(ap, text);
			nk_style_set_font(ctx, &ap->font->handle);
			free(text);
		} else wrapped_text(ap, about_text);
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
		nk_group_end(ctx);
	}
	nk_layout_row_dynamic(ctx, row, 4);
	if (nk_button_label(ctx, "Use selected") && selected >= 0 && selected < ap->known_instances.n) { loaded = false; app_open_instance(ap, ap->known_instances.v[selected]); }
	if (nk_button_label(ctx, "Browse...")) { loaded = false; app_browse_folder(ap, 1); }
	if (ap->inst_open && nk_button_label(ctx, "Cancel")) { loaded = false; ap->view = VIEW_LIST; }
	if (!ap->inst_open && nk_button_label(ctx, "Quit")) ap->running = false;
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
	nk_layout_row_dynamic(ctx, row, 3);
	sku_snprintf(buf, sizeof buf, "Jobs running: %d, queued: %d", running, queued);
	nk_label(ctx, buf, NK_TEXT_LEFT);
	sku_snprintf(buf, sizeof buf, "Files remaining: %d", remaining);
	nk_label(ctx, buf, NK_TEXT_LEFT);
	if (ap->message[0] && sku_time_ms() < ap->message_until) nk_label(ctx, ap->message, NK_TEXT_LEFT);
	else if (running) nk_label(ctx, "Working...", NK_TEXT_LEFT);
	else nk_label(ctx, "", NK_TEXT_LEFT);
}
