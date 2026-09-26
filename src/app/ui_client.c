/* The updater's own update: daily check on a background thread, banner in
 * the addon list, download into the user's config folder, "what's new" in
 * About. Installing is left to the user for now (see client.h). */
#include "app.h"
#include "nuklear_config.h"
#include "platform.h"
#include "paths.h"
#include "changelog.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define CLIENT_CHECK_INTERVAL 3600   /* at every start, but not more than once an hour */

static void downloads_dir(char *out, size_t n)
{
	char dir[SKU_PATH_MAX];
	if (sku_user_config_dir(dir, sizeof dir) != SKU_OK) sku_strlcpy(dir, ".", sizeof dir);
	sku_path_join(out, n, dir, "downloads");
}

/* ------------------------------------------------------------ threads */

static void *check_thread(void *arg)
{
	app *ap = arg;
	sku_client_info info;
	char err[256];
	int rc = sku_client_check(ap->prefs.client_url, NULL, &info, err, sizeof err);
	sku_client_info_free(&ap->client);
	if (rc == SKU_OK) {
		ap->client = info;
		ap->client_err[0] = 0;
		atomic_store(&ap->client_state, CLIENT_CHECKED);
	} else {
		sku_strlcpy(ap->client_err, err, sizeof ap->client_err);
		atomic_store(&ap->client_state, CLIENT_FAILED);
	}
	return NULL;
}

static int on_progress(void *user, int64_t got, int64_t total)
{
	app *ap = user;
	(void)total;
	atomic_store(&ap->client_done, got);
	return 0;
}

/* Where the downloaded package can be installed in place, or NULL: the running AppImage,
 * or the running drop-in program (Linux or Windows, shipped inside the zip). The Windows
 * installer package is not installed in place: it is run (see the banner). */
static const char *install_target(app *ap, const char **zip_entry)
{
	static char exe[SKU_PATH_MAX];
	const char *ai = getenv("APPIMAGE"), *entry = NULL;
	if (zip_entry) *zip_entry = NULL;
	if (ai && *ai && sku_str_ends(ap->client.package, ".AppImage")) return ai;
	if (sku_str_ends(ap->client.package, "-linux.zip")) entry = "SkunkcraftsUpdater_lin";
	else if (sku_str_ends(ap->client.package, "-windows.zip")) entry = "SkunkcraftsUpdater.exe";
	if (!entry || (ai && *ai) || sku_exe_path(exe, sizeof exe) != SKU_OK) return NULL;
	if (zip_entry) *zip_entry = entry;
	if (getenv("SKU_INSTALL_TARGET")) return getenv("SKU_INSTALL_TARGET");   /* test hook: install elsewhere than over ourselves */
	return exe;
}

static bool is_installer(app *ap)
{
	return sku_str_ends(ap->client.package, "-setup.exe");
}

static void *download_thread(void *arg)
{
	app *ap = arg;
	char dir[SKU_PATH_MAX], dest[SKU_PATH_MAX], err[256];
	const char *target, *entry;
	int rc;
	downloads_dir(dir, sizeof dir);
	rc = sku_client_download(ap->prefs.client_url, &ap->client, dir, on_progress, ap, &ap->client_cancel, dest, sizeof dest, err, sizeof err);
	if (rc == SKU_OK && (target = install_target(ap, &entry)) != NULL) {
		rc = entry ? sku_client_install_from_zip(dest, entry, target, err, sizeof err) : sku_client_install_file(dest, target, err, sizeof err);
		if (rc == SKU_OK) { sku_strlcpy(ap->client_file, target, sizeof ap->client_file); atomic_store(&ap->client_state, CLIENT_INSTALLED); return NULL; }
	}
	if (rc == SKU_OK) {
		sku_strlcpy(ap->client_file, dest, sizeof ap->client_file);
		atomic_store(&ap->client_state, CLIENT_DOWNLOADED);
	} else {
		sku_strlcpy(ap->client_err, err, sizeof ap->client_err);
		atomic_store(&ap->client_state, CLIENT_FAILED);
	}
	return NULL;
}

static void start_thread(void *(*fn)(void *), app *ap)
{
	pthread_t t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if (pthread_create(&t, &attr, fn, ap) != 0) {
		sku_strlcpy(ap->client_err, "cannot start a thread", sizeof ap->client_err);
		atomic_store(&ap->client_state, CLIENT_FAILED);
	}
	pthread_attr_destroy(&attr);
}

/* ------------------------------------------------------------ API */

void app_client_check_start(app *ap)
{
	int st = atomic_load(&ap->client_state);
	if (st == CLIENT_CHECKING || st == CLIENT_DOWNLOADING) return;
	if (!ap->prefs.client_url[0]) sku_strlcpy(ap->prefs.client_url, SKU_CLIENT_DEPOT_URL, sizeof ap->prefs.client_url);
	ap->prefs.client_last = sku_time_now();
	prefs_save(ap);
	ap->client_seen = false;
	ap->client_dismissed = false;
	atomic_store(&ap->client_state, CLIENT_CHECKING);
	start_thread(check_thread, ap);
}

void app_client_maybe_check(app *ap)
{
	ap->client_manual = false;
	if (!ap->prefs.client_check) return;
	if (getenv("SKU_NO_CLIENT_CHECK")) return;   /* tests and screenshots: no network surprise */
	if (sku_time_now() - ap->prefs.client_last < CLIENT_CHECK_INTERVAL) return;
	app_client_check_start(ap);
}

void app_client_download_start(app *ap)
{
	if (atomic_load(&ap->client_state) != CLIENT_CHECKED || !ap->client.package[0]) return;
	atomic_store(&ap->client_done, 0);
	atomic_store(&ap->client_cancel, 0);
	atomic_store(&ap->client_state, CLIENT_DOWNLOADING);
	start_thread(download_thread, ap);
}

/* Called every frame on the UI thread: reacts once to a finished check. */
void app_client_poll(app *ap)
{
	if (atomic_load(&ap->client_state) != CLIENT_CHECKED || ap->client_seen) return;
	ap->client_seen = true;
	/* the depot may move: follow the module it announces (trailing slashes aside) */
	if (ap->client.module[0] && strncmp(ap->client.module, "http", 4) == 0) {
		char a[SKU_URL_MAX], b[SKU_URL_MAX];
		size_t k;
		sku_strlcpy(a, ap->client.module, sizeof a);   k = strlen(a); while (k && a[k - 1] == '/') a[--k] = 0;
		sku_strlcpy(b, ap->prefs.client_url, sizeof b); k = strlen(b); while (k && b[k - 1] == '/') b[--k] = 0;
		if (strcmp(a, b) != 0) {
			sku_info("client: depot moved to %s, following it", a);
			sku_strlcpy(ap->prefs.client_url, a, sizeof ap->prefs.client_url);
			prefs_save(ap);
		}
	}
	if (ap->client.newer && strcmp(ap->client.version, ap->prefs.client_skip) != 0)
		app_message(ap, "%s %s is available", APP_NAME, ap->client.version);
}

/* ------------------------------------------------------------ views */

static bool banner_wanted(app *ap)
{
	int st = atomic_load(&ap->client_state);
	if (ap->client_dismissed) return false;
	if (st == CLIENT_DOWNLOADING || st == CLIENT_DOWNLOADED || st == CLIENT_INSTALLED) return true;
	if (st == CLIENT_FAILED) return ap->client_err[0] && ap->client_manual;
	return st == CLIENT_CHECKED && ap->client.newer && (ap->client_manual || strcmp(ap->client.version, ap->prefs.client_skip) != 0);
}

bool ui_client_banner(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	float row = (float)ap->prefs.text_size + 10;
	int st = atomic_load(&ap->client_state);
	char buf[SKU_PATH_MAX + 128], nice[32], total[32];
	struct nk_color accent = ap->dark ? nk_rgb(255, 200, 80) : nk_rgb(160, 90, 0);

	if (!banner_wanted(ap)) return false;
	nk_layout_row_begin(ctx, NK_STATIC, row, 4);
	nk_layout_row_push(ctx, UI_W(ap, 520));
	switch (st) {
	case CLIENT_DOWNLOADING:
		sku_nice_size(nice, sizeof nice, atomic_load(&ap->client_done));
		sku_nice_size(total, sizeof total, ap->client.size);
		sku_snprintf(buf, sizeof buf, "Downloading %s: %s / %s", ap->client.package, nice, ap->client.size >= 0 ? total : "?");
		nk_label_colored(ctx, buf, NK_TEXT_LEFT, accent);
		nk_layout_row_push(ctx, UI_W(ap, 120));
		if (ui_button(ap, "Cancel")) atomic_store(&ap->client_cancel, 1);
		break;
	case CLIENT_DOWNLOADED:
		{
			char tail[200];
			ui_left_truncate(ap, ap->client_file, 270, tail, sizeof tail);
			sku_snprintf(buf, sizeof buf, is_installer(ap) ? "%s %s installer downloaded: %s" : "%s %s saved: %s", APP_NAME, ap->client.version, tail);
		}
		nk_label_colored(ctx, buf, NK_TEXT_LEFT, accent);
		nk_layout_row_push(ctx, UI_W(ap, 120));
		if (is_installer(ap)) {
			/* the installer replaces the program itself: start it and get out of its way */
			if (ui_button(ap, "Install now")) {
				char *noargs[2] = { (char *)ap->client_file, NULL };
				if (sku_spawn_detached(ap->client_file, noargs) == SKU_OK) { sku_info("client: installer started, quitting"); ap->running = false; }
				else app_message(ap, "Cannot start the installer: run it by hand from the downloads folder");
			}
			nk_layout_row_push(ctx, UI_W(ap, 120));
		}
		if (ui_button(ap, "Open folder")) {
			char dir[SKU_PATH_MAX], url[SKU_PATH_MAX + 8];
			sku_path_dirname(dir, sizeof dir, ap->client_file);
			sku_snprintf(url, sizeof url, "file://%s", dir);
			SDL_OpenURL(url);
		}
		nk_layout_row_push(ctx, UI_W(ap, 120));
		if (ui_button(ap, "Close")) ap->client_dismissed = true;
		break;
	case CLIENT_INSTALLED:
		sku_snprintf(buf, sizeof buf, "%s %s is installed: restart to use it", APP_NAME, ap->client.version);
		nk_label_colored(ctx, buf, NK_TEXT_LEFT, accent);
		nk_layout_row_push(ctx, UI_W(ap, 120));
		if (ui_button(ap, "Restart now")) app_relaunch(ap, ap->client_file);
		nk_layout_row_push(ctx, UI_W(ap, 120));
		if (ui_button(ap, "Later")) ap->client_dismissed = true;
		break;
	case CLIENT_FAILED:
		sku_snprintf(buf, sizeof buf, "Updater update: %s", ap->client_err);
		nk_label_colored(ctx, buf, NK_TEXT_LEFT, nk_rgb(255, 80, 80));
		nk_layout_row_push(ctx, UI_W(ap, 120));
		if (ui_button(ap, "Close")) ap->client_dismissed = true;
		break;
	default:
		sku_snprintf(buf, sizeof buf, "%s %s is available (you have %s)", APP_NAME, ap->client.version, SKU_VERSION);
		nk_label_colored(ctx, buf, NK_TEXT_LEFT, accent);
		nk_layout_row_push(ctx, UI_W(ap, 120));
		if (ap->client.changelog && ui_button(ap, "What's new")) { ap->about_mode = 2; ap->view = VIEW_ABOUT; }
		nk_layout_row_push(ctx, UI_W(ap, 120));
		if (ap->client.package[0] && !ap->client.locked && ui_button(ap, install_target(ap, NULL) || is_installer(ap) ? "Update" : "Download")) app_client_download_start(ap);
		nk_layout_row_push(ctx, UI_W(ap, 120));
		if (ui_button(ap, "Later")) { sku_strlcpy(ap->prefs.client_skip, ap->client.version, sizeof ap->prefs.client_skip); prefs_save(ap); ap->client_manual = false; }
		break;
	}
	nk_layout_row_end(ctx);
	return true;
}

void ui_client_options(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	app_prefs *p = &ap->prefs;
	float row = (float)ap->prefs.text_size + 10;
	int st = atomic_load(&ap->client_state);
	static const float r2[] = { 0.30f, 0.70f }, r3[] = { 0.30f, 0.50f, 0.20f };
	char buf[512], when[32];
	bool changed = false;

	nk_layout_row(ctx, NK_DYNAMIC, row, 3, r3);
	nk_label(ctx, "Updater updates:", NK_TEXT_RIGHT);
	{
		nk_bool on = p->client_check;
		if (nk_checkbox_label(ctx, "Check for a new version of the updater at startup", &on)) { p->client_check = on; changed = true; }
	}
	if (st == CLIENT_CHECKING || st == CLIENT_DOWNLOADING) nk_label(ctx, "Working...", NK_TEXT_CENTERED);
	else if (ui_button(ap, "Check now")) { ap->client_manual = true; app_client_check_start(ap); }

	nk_layout_row(ctx, NK_DYNAMIC, row, 2, r2);
	nk_label(ctx, "", NK_TEXT_RIGHT);
	if (st == CLIENT_CHECKED) {
		if (ap->client.newer) sku_snprintf(buf, sizeof buf, "Version %s is available (you have %s)", ap->client.version, SKU_VERSION);
		else sku_snprintf(buf, sizeof buf, "You have the latest version (%s, published %s)", SKU_VERSION, ap->client.version);
	} else if (st == CLIENT_FAILED) sku_snprintf(buf, sizeof buf, "Last check failed: %s", ap->client_err);
	else if (p->client_last) { sku_format_time(when, sizeof when, p->client_last); sku_snprintf(buf, sizeof buf, "Last check: %s", when); }
	else sku_strlcpy(buf, "Not checked yet", sizeof buf);
	nk_label(ctx, buf, NK_TEXT_LEFT);

	if (changed) prefs_save(ap);
}

/* The published changelog, sections newer than this version highlighted. */
void ui_client_news(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	float row = (float)ap->prefs.text_size + 6;
	sku_changelog_section *sec = NULL;
	int ns, lineno = 0;
	char *copy, *cursor, *line;
	struct nk_color hi = ap->dark ? nk_rgb(120, 220, 120) : nk_rgb(0, 130, 0);

	if (!ap->client.changelog) { nk_layout_row_dynamic(ctx, row, 1); nk_label(ctx, "No changelog available.", NK_TEXT_LEFT); return; }
	ns = sku_changelog_sections(ap->client.changelog, SKU_VERSION, &sec);
	/* newer = published after this build, by version order (this build may not be listed at all) */
	for (int s = 0; s < ns; s++) sec[s].newer = sec[s].version[0] && sku_client_version_cmp(sec[s].version, SKU_VERSION) > 0;
	copy = sku_xstrdup(ap->client.changelog); cursor = copy;
	while ((line = sku_next_line(&cursor)) != NULL) {
		bool newer = false, header = false;
		char tidy[1024], version[64];
		for (int s = 0; s < ns; s++) if (lineno >= sec[s].first_line && lineno <= sec[s].last_line) { newer = sec[s].newer; header = lineno == sec[s].first_line && sec[s].version[0]; }
		nk_layout_row_dynamic(ctx, row, 1);
		if (header && sku_changelog_is_header(line, version, sizeof version)) {
			struct nk_color c = newer ? hi : (ap->dark ? nk_rgb(220, 220, 220) : nk_rgb(60, 60, 60));
			nk_layout_row_begin(ctx, NK_STATIC, row, 2);
			nk_layout_row_push(ctx, UI_W(ap, 16));
			nk_label_colored(ctx, newer ? "\xE2\x96\xB6" : "\xE2\x80\xA2", NK_TEXT_LEFT, c);
			nk_layout_row_push(ctx, nk_window_get_content_region(ctx).w - UI_W(ap, 40));
			nk_style_set_font(ctx, &ap->bold->handle);
			nk_label_colored(ctx, version, NK_TEXT_LEFT, c);
			nk_style_set_font(ctx, &ap->font->handle);
			nk_layout_row_end(ctx);
		} else {
			line = (char *)ui_tidy_line(line, tidy, sizeof tidy);
			if (newer) nk_label_colored(ctx, line, NK_TEXT_LEFT, hi); else nk_label(ctx, line, NK_TEXT_LEFT);
		}
		lineno++;
	}
	free(copy); free(sec);
}
