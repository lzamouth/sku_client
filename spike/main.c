/* sku spike: proves the toolchain on the three targets.
 *   - SDL3 window + Nuklear UI (dark/light theme switch)
 *   - libcurl HTTPS GET in a background thread (mbedTLS on Linux/macOS, Schannel on Windows)
 *   - "--cli [url]": same GET without any window, prints the result
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <curl/curl.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_STANDARD_BOOL
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_COMMAND_USERDATA
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "nuklear.h"
#define NK_SDL3_RENDERER_IMPLEMENTATION
#include "nuklear_sdl3_renderer.h"

#define DEFAULT_URL "https://flyjsim.b-cdn.net/updater/Q4XP12/skunkcrafts_updater.cfg"

/* ---------------------------------------------------------------- fetch */

enum { FETCH_IDLE, FETCH_RUNNING, FETCH_DONE };

struct fetch {
	char url[512];
	SDL_AtomicInt state;
	/* results, valid once state == FETCH_DONE */
	long   status;
	size_t bytes;
	double secs;
	char   ctype[128];
	char   head[256];   /* first bytes of the body, one line */
	char   error[256];
};

static char  *g_cacert     = NULL;   /* PEM bundle loaded from disk, or NULL */
static size_t g_cacert_len = 0;

static size_t on_body(char *data, size_t sz, size_t n, void *user)
{
	struct fetch *f = user;
	size_t len = sz * n;
	if (f->bytes == 0) {
		size_t k = len < sizeof f->head - 1 ? len : sizeof f->head - 1;
		memcpy(f->head, data, k);
		f->head[k] = 0;
		f->head[strcspn(f->head, "\r\n")] = 0;
	}
	f->bytes += len;
	return len;
}

static void do_fetch(struct fetch *f)
{
	CURL *c = curl_easy_init();
	CURLcode rc;
	Uint64 t0 = SDL_GetTicksNS();

	f->status = 0; f->bytes = 0; f->ctype[0] = 0; f->head[0] = 0; f->error[0] = 0;
	if (!c) { SDL_strlcpy(f->error, "curl_easy_init failed", sizeof f->error); return; }

	curl_easy_setopt(c, CURLOPT_URL, f->url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");   /* transport gzip via zlib */
	curl_easy_setopt(c, CURLOPT_USERAGENT, "SkunkCraftsUpdater/4.0-spike");
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_body);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_ERRORBUFFER, f->error);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
	if (g_cacert) {
		struct curl_blob blob = { g_cacert, g_cacert_len, CURL_BLOB_NOCOPY };
		curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &blob);
	}

	rc = curl_easy_perform(c);
	if (rc != CURLE_OK) {
		if (!f->error[0]) SDL_strlcpy(f->error, curl_easy_strerror(rc), sizeof f->error);
	} else {
		const char *ct = NULL;
		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &f->status);
		curl_easy_getinfo(c, CURLINFO_CONTENT_TYPE, &ct);
		SDL_strlcpy(f->ctype, ct ? ct : "?", sizeof f->ctype);
	}
	curl_easy_cleanup(c);
	f->secs = (double)(SDL_GetTicksNS() - t0) / 1e9;
}

static int fetch_thread(void *user)
{
	struct fetch *f = user;
	do_fetch(f);
	SDL_SetAtomicInt(&f->state, FETCH_DONE);
	return 0;
}

static void load_cacert(void)
{
	const char *base = SDL_GetBasePath();
	char path[1024];
	size_t n;
	/* Schannel (Windows) uses the system certificate store: no bundle there. */
	if (strstr(curl_version_info(CURLVERSION_NOW)->ssl_version, "Schannel")) return;
	SDL_snprintf(path, sizeof path, "%scacert.pem", base ? base : "");
	g_cacert = SDL_LoadFile(path, &n);
	if (!g_cacert) g_cacert = SDL_LoadFile("/etc/ssl/certs/ca-certificates.crt", &n);
	g_cacert_len = g_cacert ? n : 0;
}

/* ---------------------------------------------------------------- theme */

static void apply_theme(struct nk_context *ctx, bool dark)
{
	struct nk_color t[NK_COLOR_COUNT];
	if (dark) { nk_style_default(ctx); return; }
	memcpy(t, nk_default_color_style, sizeof t);
	t[NK_COLOR_TEXT]                    = nk_rgba(40, 40, 40, 255);
	t[NK_COLOR_WINDOW]                  = nk_rgba(235, 235, 235, 255);
	t[NK_COLOR_HEADER]                  = nk_rgba(205, 205, 205, 255);
	t[NK_COLOR_BORDER]                  = nk_rgba(150, 150, 150, 255);
	t[NK_COLOR_BUTTON]                  = nk_rgba(200, 200, 200, 255);
	t[NK_COLOR_BUTTON_HOVER]            = nk_rgba(180, 180, 180, 255);
	t[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba(160, 160, 160, 255);
	t[NK_COLOR_TOGGLE]                  = nk_rgba(190, 190, 190, 255);
	t[NK_COLOR_TOGGLE_HOVER]            = nk_rgba(170, 170, 170, 255);
	t[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(60, 60, 60, 255);
	t[NK_COLOR_SELECT]                  = nk_rgba(210, 210, 210, 255);
	t[NK_COLOR_SELECT_ACTIVE]           = nk_rgba(170, 170, 170, 255);
	t[NK_COLOR_SLIDER]                  = nk_rgba(200, 200, 200, 255);
	t[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(80, 80, 80, 255);
	t[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(70, 70, 70, 255);
	t[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(60, 60, 60, 255);
	t[NK_COLOR_PROPERTY]                = nk_rgba(210, 210, 210, 255);
	t[NK_COLOR_EDIT]                    = nk_rgba(255, 255, 255, 255);
	t[NK_COLOR_EDIT_CURSOR]             = nk_rgba(0, 0, 0, 255);
	t[NK_COLOR_COMBO]                   = nk_rgba(210, 210, 210, 255);
	t[NK_COLOR_CHART]                   = nk_rgba(200, 200, 200, 255);
	t[NK_COLOR_CHART_COLOR]             = nk_rgba(45, 45, 45, 255);
	t[NK_COLOR_SCROLLBAR]               = nk_rgba(210, 210, 210, 255);
	t[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba(150, 150, 150, 255);
	t[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba(130, 130, 130, 255);
	t[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(110, 110, 110, 255);
	t[NK_COLOR_TAB_HEADER]              = nk_rgba(205, 205, 205, 255);
	nk_style_from_table(ctx, t);
}

/* ---------------------------------------------------------------- main */

static int run_cli(const char *url)
{
	struct fetch f;
	memset(&f, 0, sizeof f);
	SDL_strlcpy(f.url, url, sizeof f.url);
	printf("curl: %s\n", curl_version());
	printf("CA bundle: %s (%lu bytes)\n", g_cacert ? "loaded" : "none", (unsigned long)g_cacert_len);
	printf("GET %s\n", f.url);
	do_fetch(&f);
	if (f.error[0]) { printf("ERROR: %s\n", f.error); return 1; }
	printf("HTTP %ld, %lu bytes, %s, %.3fs\nfirst line: %s\n", f.status, (unsigned long)f.bytes, f.ctype, f.secs, f.head);
	return f.status == 200 ? 0 : 1;
}

int main(int argc, char **argv)
{
	SDL_Window *win; SDL_Renderer *ren; struct nk_context *ctx;
	struct fetch f; SDL_Thread *th = NULL;
	char url[512] = DEFAULT_URL;
	bool dark, running = true;
	float scale;
	int rc = 0;

	curl_global_init(CURL_GLOBAL_DEFAULT);
	load_cacert();

	if (argc >= 2 && strcmp(argv[1], "--cli") == 0) {
		rc = run_cli(argc >= 3 ? argv[2] : DEFAULT_URL);
		curl_global_cleanup();
		return rc;
	}

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
	if (!SDL_CreateWindowAndRenderer("SkunkCrafts Updater 4.0 - spike", 820, 460, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &win, &ren)) {
		fprintf(stderr, "window: %s\n", SDL_GetError()); return 1;
	}
	SDL_SetRenderVSync(ren, 1);
	scale = SDL_GetWindowDisplayScale(win);
	SDL_SetRenderScale(ren, scale, scale);

	ctx = nk_sdl_init(win, ren, nk_sdl_allocator());
	{
		struct nk_font_atlas *atlas = nk_sdl_font_stash_begin(ctx);
		struct nk_font_config cfg = nk_font_config(0);
		struct nk_font *font = nk_font_atlas_add_default(atlas, 16 * scale, &cfg);
		nk_sdl_font_stash_end(ctx);
		font->handle.height /= scale;
		nk_style_set_font(ctx, &font->handle);
	}
	dark = SDL_GetSystemTheme() != SDL_SYSTEM_THEME_LIGHT;
	apply_theme(ctx, dark);

	memset(&f, 0, sizeof f);
	SDL_SetAtomicInt(&f.state, FETCH_IDLE);

	while (running) {
		SDL_Event ev;
		int w, h;
		nk_input_begin(ctx);
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_EVENT_QUIT) running = false;
			SDL_ConvertEventToRenderCoordinates(ren, &ev);
			nk_sdl_handle_event(ctx, &ev);
		}
		nk_input_end(ctx);

		SDL_GetRenderOutputSize(ren, &w, &h);
		w = (int)(w / scale); h = (int)(h / scale);
		if (nk_begin(ctx, "spike", nk_rect(0, 0, (float)w, (float)h), NK_WINDOW_NO_SCROLLBAR)) {
			int state = SDL_GetAtomicInt(&f.state);
			char line[600];

			nk_layout_row_dynamic(ctx, 24, 1);
			SDL_snprintf(line, sizeof line, "platform %s | video %s | renderer %s | scale %.2f | theme %s",
				SDL_GetPlatform(), SDL_GetCurrentVideoDriver(), SDL_GetRendererName(ren), scale, dark ? "dark" : "light");
			nk_label(ctx, line, NK_TEXT_LEFT);
			nk_label(ctx, curl_version(), NK_TEXT_LEFT);
			SDL_snprintf(line, sizeof line, "CA bundle: %s (%lu bytes)", g_cacert ? "loaded" : "none", (unsigned long)g_cacert_len);
			nk_label(ctx, line, NK_TEXT_LEFT);

			nk_layout_row_dynamic(ctx, 30, 1);
			nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, url, sizeof url, nk_filter_default);

			nk_layout_row_dynamic(ctx, 30, 3);
			if (nk_button_label(ctx, state == FETCH_RUNNING ? "Fetching..." : "Fetch (HTTPS)") && state != FETCH_RUNNING) {
				if (th) { SDL_WaitThread(th, NULL); th = NULL; }
				SDL_strlcpy(f.url, url, sizeof f.url);
				SDL_SetAtomicInt(&f.state, FETCH_RUNNING);
				th = SDL_CreateThread(fetch_thread, "fetch", &f);
			}
			if (nk_button_label(ctx, dark ? "Switch to light theme" : "Switch to dark theme")) { dark = !dark; apply_theme(ctx, dark); }
			if (nk_button_label(ctx, "Quit")) running = false;

			nk_layout_row_dynamic(ctx, 24, 1);
			if (state == FETCH_DONE) {
				if (f.error[0]) {
					SDL_snprintf(line, sizeof line, "ERROR: %s", f.error);
					nk_label_colored(ctx, line, NK_TEXT_LEFT, nk_rgb(255, 80, 80));
				} else {
					SDL_snprintf(line, sizeof line, "HTTP %ld, %lu bytes, %s, %.3fs", f.status, (unsigned long)f.bytes, f.ctype, f.secs);
					nk_label_colored(ctx, line, NK_TEXT_LEFT, f.status == 200 ? nk_rgb(80, 220, 80) : nk_rgb(255, 200, 80));
					SDL_snprintf(line, sizeof line, "first line: %s", f.head);
					nk_label(ctx, line, NK_TEXT_LEFT);
				}
			} else if (state == FETCH_RUNNING) {
				nk_label(ctx, "working...", NK_TEXT_LEFT);
			}
		}
		nk_end(ctx);

		if (dark) SDL_SetRenderDrawColor(ren, 32, 32, 32, 255); else SDL_SetRenderDrawColor(ren, 235, 235, 235, 255);
		SDL_RenderClear(ren);
		nk_sdl_render(ctx, NK_ANTI_ALIASING_ON);
		nk_sdl_update_TextInput(ctx);
		SDL_RenderPresent(ren);
	}

	if (th) SDL_WaitThread(th, NULL);
	nk_sdl_shutdown(ctx);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	SDL_free(g_cacert);
	curl_global_cleanup();
	return 0;
}
