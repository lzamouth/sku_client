/* SkunkCrafts Updater, graphical client: SDL3 window, Nuklear UI, core jobs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define NK_IMPLEMENTATION
#define NK_SDL3_RENDERER_IMPLEMENTATION
#include "nuklear_config.h"

#include "app.h"
#include "platform.h"
#include "paths.h"
#include "log.h"
#include "http.h"
#include "report.h"

extern const unsigned char sku_font_sans[];
extern const size_t sku_font_sans_len;
extern const unsigned char sku_font_mono[];
extern const size_t sku_font_mono_len;

const struct nk_color *app_default_colors(void)
{
	return nk_default_color_style;
}

/* ------------------------------------------------------------ items */

void app_refresh_live(app_item *it)
{
	it->st = sku_addon_status(it->a, it->st_text, sizeof it->st_text, it->st_detail, sizeof it->st_detail);
	it->prog_done = atomic_load(&it->a->prog_done);
	it->prog_total = atomic_load(&it->a->prog_total);
	it->bytes_done = atomic_load(&it->a->bytes_done);
	it->bytes_total = atomic_load(&it->a->bytes_total);
}

void app_refresh_item(app_item *it)
{
	sku_addon *a = it->a;
	if (atomic_load(&it->busy)) { app_refresh_live(it); return; }
	sku_strlcpy(it->name, a->name, sizeof it->name);
	sku_strlcpy(it->version, a->version, sizeof it->version);
	sku_strlcpy(it->remote_version, a->remote_version, sizeof it->remote_version);
	sku_strlcpy(it->dir, a->dir, sizeof it->dir);
	sku_strlcpy(it->source, a->base_url, sizeof it->source);
	sku_strlcpy(it->channel, a->channel, sizeof it->channel);
	it->nchannels = 0;
	for (int i = 0; i < a->nchannels && it->nchannels < APP_MAX_CHANNELS; i++)
		sku_strlcpy(it->channels[it->nchannels++], a->channels[i].name, sizeof it->channels[0]);
	it->genre = a->genre;
	it->disabled = a->disabled;
	it->liveries = a->liveries;
	it->checked = a->checked;
	it->v4 = a->v4;
	it->n_new = a->plan.n_new; it->n_changed = a->plan.n_changed; it->n_delete = a->plan.n_delete;
	it->n_ignored = a->plan.n_ignored; it->n_error = a->plan.n_error; it->n_unchanged = a->plan.n_unchanged;
	it->bytes = a->plan.bytes;
	app_refresh_live(it);
}

static int item_cmp(const void *x, const void *y)
{
	const app_item *a = x, *b = y;
	return strcasecmp(a->name, b->name);
}

static void close_items(app *ap)
{
	for (int i = 0; i < ap->nitems; i++) sku_addon_close(ap->items[i].a);
	free(ap->items);
	ap->items = NULL; ap->nitems = 0; ap->selected = -1;
	free(ap->report_text); ap->report_text = NULL; ap->report_for = -1;
}

void app_rescan(app *ap)
{
	sku_found_list found = {0};
	if (!ap->inst_open || app_any_busy(ap)) return;
	close_items(ap);
	sku_scan_instance(&ap->inst, &found);
	ap->items = sku_xcalloc((size_t)found.n + 1, sizeof *ap->items);
	for (int i = 0; i < found.n; i++) {
		app_item *it = &ap->items[ap->nitems++];
		it->a = sku_addon_open(&ap->inst, found.v[i].dir, found.v[i].genre);
		atomic_init(&it->busy, 0);
		app_refresh_item(it);
	}
	sku_found_list_free(&found);
	qsort(ap->items, (size_t)ap->nitems, sizeof *ap->items, item_cmp);
	app_message(ap, "%d addon(s) found", ap->nitems);
}

bool app_any_busy(app *ap)
{
	for (int i = 0; i < ap->nitems; i++) if (atomic_load(&ap->items[i].busy)) return true;
	return false;
}

/* ------------------------------------------------------------ instance */

void app_close_instance(app *ap)
{
	if (!ap->inst_open) return;
	jobs_cancel_all(ap);
	while (jobs_running(ap->jobs)) sku_sleep_ms(50);
	close_items(ap);
	sku_instance_close(&ap->inst);
	ap->inst_open = false;
}

void app_open_instance(app *ap, const char *root)
{
	app_close_instance(ap);
	if (sku_instance_open(&ap->inst, root, false) != SKU_OK) {
		app_message(ap, "%s is not an X-Plane installation (or is not writable)", root);
		ap->view = VIEW_INSTANCE;
		return;
	}
	ap->inst_open = true;
	sku_log_set_level(ap->prefs.verbose ? SKU_LOG_DEBUG : SKU_LOG_INFO);
	sku_strlcpy(ap->prefs.instance, ap->inst.root, sizeof ap->prefs.instance);
	prefs_migrate_legacy(ap);
	prefs_save(ap);
	app_rescan(ap);
	ap->view = VIEW_LIST;
	switch (ap->prefs.startup) {
	case 1: for (int i = 0; i < ap->nitems; i++) if (!ap->items[i].disabled) jobs_submit(ap->jobs, &ap->items[i], JOB_CHECK); break;
	case 2: for (int i = 0; i < ap->nitems; i++) if (!ap->items[i].disabled) jobs_submit(ap->jobs, &ap->items[i], JOB_UPDATE); break;
	}
}

/* SDL calls this from an unspecified thread: only hand the result over. */
static void SDLCALL on_folder_chosen(void *user, const char * const *files, int filter)
{
	app *ap = user;
	(void)filter;
	if (files && files[0]) sku_strlcpy(ap->browse_result, files[0], sizeof ap->browse_result);
	else ap->browse_result[0] = 0;
	atomic_store(&ap->browse_pending, 3);   /* 3 = result ready */
}

void app_browse_folder(app *ap, int what)
{
	if (atomic_load(&ap->browse_pending)) return;
	ap->browse_result[0] = 0;
	atomic_store(&ap->browse_pending, what);
	SDL_ShowOpenFolderDialog(on_folder_chosen, ap, ap->win, ap->inst_open ? ap->inst.root : NULL, false);
}

static int browse_what;   /* remembered across the async dialog */

static void handle_browse_result(app *ap)
{
	char root[SKU_PATH_MAX];
	if (atomic_load(&ap->browse_pending) != 3) return;
	atomic_store(&ap->browse_pending, 0);
	if (!ap->browse_result[0]) return;
	sku_strlcpy(root, ap->browse_result, sizeof root);
	sku_path_normalize(root);
	if (browse_what == 1) {
		if (!sku_instance_is_root(root)) { app_message(ap, "%s is not an X-Plane folder", root); return; }
		if (!sku_strlist_has(&ap->prefs.instances, root)) sku_strlist_push(&ap->prefs.instances, root);
		app_open_instance(ap, root);
	} else if (browse_what == 2 && ap->inst_open) {
		if (sku_scan_custom_add(&ap->inst, root) != SKU_OK) { app_message(ap, "No skunkcrafts_updater.cfg in %s", root); return; }
		app_rescan(ap);
	}
}

/* ------------------------------------------------------------ misc */

void app_message(app *ap, const char *fmt, ...)
{
	va_list va;
	va_start(va, fmt);
	vsnprintf(ap->message, sizeof ap->message, fmt, va);
	va_end(va);
	ap->message_until = sku_time_ms() + 6000;
	sku_info("ui: %s", ap->message);
}

static void load_fonts(app *ap)
{
	struct nk_font_atlas *atlas = nk_sdl_font_stash_begin(ap->ctx);
	struct nk_font_config cfg = nk_font_config(0);
	float px = (float)ap->prefs.text_size * ap->scale;
	cfg.oversample_h = 2; cfg.oversample_v = 2;
	cfg.range = nk_font_default_glyph_ranges();
	ap->font = nk_font_atlas_add_from_memory(atlas, (void *)sku_font_sans, sku_font_sans_len, px, &cfg);
	ap->mono = nk_font_atlas_add_from_memory(atlas, (void *)sku_font_mono, sku_font_mono_len, px, &cfg);
	nk_sdl_font_stash_end(ap->ctx);
	ap->font->handle.height /= ap->scale;
	ap->mono->handle.height /= ap->scale;
	nk_style_set_font(ap->ctx, &ap->font->handle);
}

void app_set_text_size(app *ap, int size)
{
	if (size < 10) size = 10;
	if (size > 32) size = 32;
	if (size == ap->prefs.text_size) return;
	ap->prefs.text_size = size;
	load_fonts(ap);
	app_apply_theme(ap);
}

void app_apply_theme(app *ap)
{
	ap->dark = ap->prefs.theme == THEME_DARK || (ap->prefs.theme == THEME_SYSTEM && SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT);
	theme_apply(ap->ctx, ap->dark);
	nk_style_set_font(ap->ctx, &ap->font->handle);
}

/* ------------------------------------------------------------ test hook */

/* SKU_AUTOPILOT=<file>: lines "<seq> click <x> <y>" (pixels, as seen in a
 * --screenshot frame) are fed straight into Nuklear's input, once each:
 * press on one frame, release on the next (Nuklear triggers buttons on
 * press). Lets the test scripts drive the UI without a window manager in the
 * loop. Must be called between nk_input_begin() and nk_input_end(). */
static void autopilot(app *ap)
{
	static const char *file;
	static int last_seq = 0, press_x, press_y;
	static bool checked, pressed;
	char *text;
	if (!checked) { file = getenv("SKU_AUTOPILOT"); checked = true; }
	if (!file) return;
	if (pressed) {
		nk_input_button(ap->ctx, NK_BUTTON_LEFT, (int)(press_x / ap->scale), (int)(press_y / ap->scale), nk_false);
		pressed = false;
		return;
	}
	if (sku_read_file(file, &text, NULL) != SKU_OK) return;
	{
		char *cursor = text, *line;
		while ((line = sku_next_line(&cursor)) != NULL) {
			int seq, x, y;
			if (sscanf(line, "%d click %d %d", &seq, &x, &y) == 3 && seq > last_seq) {
				last_seq = seq; press_x = x; press_y = y; pressed = true;
				nk_input_motion(ap->ctx, (int)(x / ap->scale), (int)(y / ap->scale));
				nk_input_button(ap->ctx, NK_BUTTON_LEFT, (int)(x / ap->scale), (int)(y / ap->scale), nk_true);
				sku_info("autopilot: click %d %d", x, y);
				break;
			}
		}
	}
	free(text);
}

/* ------------------------------------------------------------ frame */

static void draw(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	int w, h;
	SDL_GetRenderOutputSize(ap->ren, &w, &h);
	w = (int)(w / ap->scale); h = (int)(h / ap->scale);

	if (nk_begin(ctx, "main", nk_rect(0, 0, (float)w, (float)h), NK_WINDOW_NO_SCROLLBAR)) {
		float row = (float)ap->prefs.text_size + 12;
		if (ap->inst_open) {
			/* tab bar, like the 3.x client: Addons / Options / About */
			bool in_addons = ap->view == VIEW_LIST || ap->view == VIEW_DETAIL;
			nk_layout_row_begin(ctx, NK_STATIC, row, 4);
			nk_layout_row_push(ctx, 110);
			if (nk_selectable_label(ctx, "Addons", NK_TEXT_CENTERED, &(nk_bool){ in_addons }) && !in_addons) ap->view = VIEW_LIST;
			nk_layout_row_push(ctx, 110);
			if (nk_selectable_label(ctx, "Options", NK_TEXT_CENTERED, &(nk_bool){ ap->view == VIEW_OPTIONS })) ap->view = VIEW_OPTIONS;
			nk_layout_row_push(ctx, 110);
			if (nk_selectable_label(ctx, "About", NK_TEXT_CENTERED, &(nk_bool){ ap->view == VIEW_ABOUT })) ap->view = VIEW_ABOUT;
			nk_layout_row_end(ctx);
		}
		/* content area: everything but the status bar */
		nk_layout_row_dynamic(ctx, (float)h - (ap->inst_open ? 2 * row : row) - 30, 1);
		if (nk_group_begin(ctx, "content", NK_WINDOW_NO_SCROLLBAR)) {
			switch (ap->view) {
			case VIEW_LIST:     ui_list(ap); break;
			case VIEW_DETAIL:   ui_detail(ap); break;
			case VIEW_OPTIONS:  ui_options(ap); break;
			case VIEW_ABOUT:    ui_about(ap); break;
			case VIEW_INSTANCE: ui_instance(ap); break;
			}
			nk_group_end(ctx);
		}
		ui_statusbar(ap);
	}
	nk_end(ctx);

	if (ap->dark) SDL_SetRenderDrawColor(ap->ren, 32, 32, 32, 255); else SDL_SetRenderDrawColor(ap->ren, 240, 240, 240, 255);
	SDL_RenderClear(ap->ren);
	nk_sdl_render(ctx, NK_ANTI_ALIASING_ON);
	nk_sdl_update_TextInput(ctx);
	if (ap->shot_dir[0] && sku_time_ms() - ap->last_shot > 1000) {
		SDL_Surface *s = SDL_RenderReadPixels(ap->ren, NULL);
		char path[SKU_PATH_MAX];
		if (s) {
			sku_snprintf(path, sizeof path, "%s/frame_%03d.bmp", ap->shot_dir, ap->shot_index++);
			SDL_SaveBMP(s, path);
			SDL_DestroySurface(s);
		}
		ap->last_shot = sku_time_ms();
	}
	SDL_RenderPresent(ap->ren);
}

static void choose_startup_instance(app *ap, const char *forced)
{
	char root[SKU_PATH_MAX];
	sku_strlist known = {0};
	if (forced) { app_open_instance(ap, forced); return; }
	if (sku_instance_detect(root, sizeof root) == SKU_OK) { app_open_instance(ap, root); return; }
	if (ap->prefs.instance[0] && sku_instance_is_root(ap->prefs.instance)) { app_open_instance(ap, ap->prefs.instance); return; }
	sku_instance_known(&known);
	if (known.n == 1 && ap->prefs.instances.n == 0) { app_open_instance(ap, known.v[0]); sku_strlist_free(&known); return; }
	sku_strlist_free(&known);
	ap->view = VIEW_INSTANCE;
}

int main(int argc, char **argv)
{
	static app ap;
	const char *forced = NULL;
	int64_t last_size_save = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--xp") == 0 && i + 1 < argc) forced = argv[++i];
		else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) sku_strlcpy(ap.shot_dir, argv[++i], sizeof ap.shot_dir);
		else if (strcmp(argv[i], "--version") == 0) { printf("%s %s\n", APP_NAME, SKU_VERSION); return 0; }
	}
	ap.selected = -1; ap.report_for = -1;
	ap.filter_aircraft = ap.filter_scenery = ap.filter_plugins = ap.filter_other = true;
	prefs_load(&ap);
	sku_log_set_level(ap.prefs.verbose ? SKU_LOG_DEBUG : SKU_LOG_INFO);
	sku_http_global_init_default(NULL);

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
	if (!SDL_CreateWindowAndRenderer(APP_NAME " " SKU_VERSION, ap.prefs.window_w, ap.prefs.window_h,
			SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &ap.win, &ap.ren)) {
		fprintf(stderr, "window: %s\n", SDL_GetError()); return 1;
	}
	SDL_SetWindowMinimumSize(ap.win, 640, 400);
	SDL_SetRenderVSync(ap.ren, 1);
	ap.scale = SDL_GetWindowDisplayScale(ap.win);
	if (ap.scale <= 0) ap.scale = 1;
	SDL_SetRenderScale(ap.ren, ap.scale, ap.scale);
	{
		int ww, wh, pw, ph, ow, oh;
		SDL_GetWindowSize(ap.win, &ww, &wh);
		SDL_GetWindowSizeInPixels(ap.win, &pw, &ph);
		SDL_GetRenderOutputSize(ap.ren, &ow, &oh);
		sku_info("window %dx%d, pixels %dx%d, render output %dx%d, display scale %.3f, video %s, renderer %s",
			ww, wh, pw, ph, ow, oh, ap.scale, SDL_GetCurrentVideoDriver(), SDL_GetRendererName(ap.ren));
	}
	ap.ctx = nk_sdl_init(ap.win, ap.ren, nk_sdl_allocator());
	load_fonts(&ap);
	app_apply_theme(&ap);
	ap.jobs = jobs_create(&ap, 3);
	ap.running = true;

	choose_startup_instance(&ap, forced);

	while (ap.running) {
		SDL_Event ev;
		bool busy = jobs_running(ap.jobs) > 0 || jobs_queued(ap.jobs) > 0 || atomic_load(&ap.browse_pending);
		nk_input_begin(ap.ctx);
		if (SDL_WaitEventTimeout(&ev, busy ? 100 : 500)) {
			do {
				if (ev.type == SDL_EVENT_QUIT) ap.running = false;
				if (ev.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
					ap.scale = SDL_GetWindowDisplayScale(ap.win);
					SDL_SetRenderScale(ap.ren, ap.scale, ap.scale);
					load_fonts(&ap);
					app_apply_theme(&ap);
				}
				if (ev.type == SDL_EVENT_WINDOW_RESIZED) {
					SDL_GetWindowSize(ap.win, &ap.prefs.window_w, &ap.prefs.window_h);
					last_size_save = sku_time_ms();
				}
				if (ev.type == SDL_EVENT_SYSTEM_THEME_CHANGED && ap.prefs.theme == THEME_SYSTEM) app_apply_theme(&ap);
				SDL_ConvertEventToRenderCoordinates(ap.ren, &ev);
				nk_sdl_handle_event(ap.ctx, &ev);
			} while (SDL_PollEvent(&ev));
		}
		autopilot(&ap);
		nk_input_end(ap.ctx);

		handle_browse_result(&ap);
		if (jobs_done(ap.jobs) != ap.last_jobs_done) {
			ap.last_jobs_done = jobs_done(ap.jobs);
			for (int i = 0; i < ap.nitems; i++) app_refresh_item(&ap.items[i]);
		} else {
			for (int i = 0; i < ap.nitems; i++) app_refresh_live(&ap.items[i]);
		}
		if (last_size_save && sku_time_ms() - last_size_save > 1000) { prefs_save(&ap); last_size_save = 0; }
		if (browse_what != atomic_load(&ap.browse_pending) && atomic_load(&ap.browse_pending) && atomic_load(&ap.browse_pending) != 3) browse_what = atomic_load(&ap.browse_pending);

		draw(&ap);
	}

	prefs_save(&ap);
	app_close_instance(&ap);
	jobs_destroy(ap.jobs);
	nk_sdl_shutdown(ap.ctx);
	SDL_DestroyRenderer(ap.ren);
	SDL_DestroyWindow(ap.win);
	SDL_Quit();
	sku_http_global_cleanup();
	sku_strlist_free(&ap.prefs.instances);
	sku_strlist_free(&ap.known_instances);
	return 0;
}
