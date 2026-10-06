#include "scan.h"
#include "platform.h"
#include "paths.h"
#include "protocol.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

const char *sku_genre_name(sku_genre g)
{
	static const char *names[] = { "Aircraft", "Scenery", "Plugin", "Other", "Custom" };
	return (unsigned)g < sizeof names / sizeof *names ? names[g] : "?";
}

void sku_found_list_free(sku_found_list *l)
{
	free(l->v);
	memset(l, 0, sizeof *l);
}

static void add(sku_found_list *l, const char *dir, sku_genre genre, bool v4)
{
	for (int i = 0; i < l->n; i++) if (strcmp(l->v[i].dir, dir) == 0) return;   /* reachable twice through symlinks */
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 32;
		l->v = sku_xrealloc(l->v, (size_t)l->cap * sizeof *l->v);
	}
	sku_strlcpy(l->v[l->n].dir, dir, sizeof l->v[l->n].dir);
	l->v[l->n].genre = genre;
	l->v[l->n].v4 = v4;
	l->n++;
}

bool sku_scan_is_addon(const char *dir, bool *v4)
{
	char p[SKU_PATH_MAX];
	sku_path_join(p, sizeof p, dir, SKU_F_CFG);
	if (sku_is_file(p)) { if (v4) *v4 = false; return true; }
	sku_path_join(p, sizeof p, dir, SKU_F_V4_CFG);   /* converted by a 4.0 beta: migrated back on load */
	if (sku_is_file(p)) { if (v4) *v4 = true; return true; }
	return false;
}

struct walk {
	sku_found_list *out;
	sku_genre genre;
	const char *dir;
	int depth;
	bool found_acf;
	sku_strlist subdirs;
};

static int walk_cb(const char *name, bool is_dir, bool is_symlink, void *user)
{
	struct walk *w = user;
	if (!is_dir && !is_symlink) {
		if (sku_str_ends(name, ".acf") || sku_str_ends(name, ".ACF")) w->found_acf = true;
		return 0;
	}
	if (is_dir) sku_strlist_push(&w->subdirs, name);
	else if (is_symlink) {
		char full[SKU_PATH_MAX], real[SKU_PATH_MAX];
		sku_path_join(full, sizeof full, w->dir, name);
		if (sku_realpath(full, real, sizeof real) == SKU_OK && sku_is_dir(real)) sku_strlist_push(&w->subdirs, name);
	}
	return 0;
}

/* Every reason to stop exploring is logged: at info level under Aircraft
 * (where users ask why their plane was not found), at debug level elsewhere. */
static void stop_reason(sku_genre genre, const char *dir, const char *why)
{
	sku_log(genre == SKU_GENRE_AIRCRAFT ? SKU_LOG_INFO : SKU_LOG_DEBUG, "scan: %s: %s", dir, why);
}

/* " (...)" after "managed ... found" for a folder still in the layout of betas 1 to 9 */
static const char *layout_note(bool v4)
{
	return v4 ? " (4.0 beta 1-9 layout, to migrate back to " SKU_F_CFG ")" : "";
}

/* depth counts the levels examined, dir included, as in 3.x: 11 for Aircraft,
 * 2 for Custom Scenery and Resources/plugins (the folder and its children). */
static void walk(sku_found_list *out, const char *dir, int depth, sku_genre genre)
{
	struct walk w = { out, genre, dir, depth, false, {0} };
	bool v4;
	if (sku_scan_is_addon(dir, &v4)) {
		add(out, dir, genre, v4);
		sku_info("scan: %s: managed %s found%s", dir, genre == SKU_GENRE_AIRCRAFT ? "aircraft" : "addon", layout_note(v4));
		return;
	}
	if (depth <= 1) {
		/* outside Aircraft the last level is a scenery or plugin folder of its own */
		stop_reason(genre, dir, genre == SKU_GENRE_AIRCRAFT ? "max depth reached, not explored" : "not managed by the updater");
		return;
	}
	if (sku_list_dir(dir, walk_cb, &w) != SKU_OK) { sku_warn("scan: cannot read %s", dir); return; }
	if (w.found_acf) stop_reason(genre, dir, "unmanaged aircraft found (.acf without updater config), not explored further");
	else {
		for (int i = 0; i < w.subdirs.n; i++) {
			char sub[SKU_PATH_MAX];
			sku_path_join(sub, sizeof sub, dir, w.subdirs.v[i]);
			walk(out, sub, depth - 1, genre);
		}
	}
	sku_strlist_free(&w.subdirs);
}

/* Root level: only direct children that are addons, skipping the standard folders. */
static int root_cb(const char *name, bool is_dir, bool is_symlink, void *user)
{
	struct walk *w = user;
	static const char *skip[] = { "Aircraft", "Custom Scenery", "Custom Data", "Global Scenery", "Resources", "Output", "Airfoils", "Instructions", "Weapons" };
	char full[SKU_PATH_MAX];
	bool v4;
	if (!is_dir && !is_symlink) return 0;
	for (size_t i = 0; i < sizeof skip / sizeof *skip; i++) if (strcmp(name, skip[i]) == 0) return 0;
	sku_path_join(full, sizeof full, w->dir, name);
	if (sku_is_dir(full) && sku_scan_is_addon(full, &v4)) {
		add(w->out, full, SKU_GENRE_OTHER, v4);
		sku_info("scan: %s: managed addon found at the X-Plane root%s", full, layout_note(v4));
	}
	return 0;
}

int sku_scan_instance(const sku_instance *in, sku_found_list *out)
{
	char p[SKU_PATH_MAX];
	struct walk w = { out, SKU_GENRE_OTHER, in->root, 1, false, {0} };
	sku_strlist custom = {0};
	int64_t t0 = sku_time_ms();

	sku_path_join(p, sizeof p, in->root, "Aircraft");          walk(out, p, 11, SKU_GENRE_AIRCRAFT);
	sku_path_join(p, sizeof p, in->root, "Custom Scenery");    walk(out, p, 2, SKU_GENRE_SCENERY);
	sku_path_join(p, sizeof p, in->root, "Resources/plugins"); walk(out, p, 2, SKU_GENRE_PLUGIN);
	sku_list_dir(in->root, root_cb, &w);

	sku_scan_custom_load(in, &custom);
	for (int i = 0; i < custom.n; i++) {
		bool v4;
		if (sku_scan_is_addon(custom.v[i], &v4)) { add(out, custom.v[i], SKU_GENRE_CUSTOM, v4); sku_info("scan: %s: custom addon folder%s", custom.v[i], layout_note(v4)); }
		else sku_warn("custom addon folder not found or no longer an addon: %s", custom.v[i]);
	}
	sku_strlist_free(&custom);
	sku_info("scan: %d addon(s) found in %lld ms", out->n, (long long)(sku_time_ms() - t0));
	return SKU_OK;
}

static void custom_file(const sku_instance *in, char *out, size_t n)
{
	sku_path_join(out, n, in->data_dir, "custom_addons.txt");
}

int sku_scan_custom_load(const sku_instance *in, sku_strlist *out)
{
	char file[SKU_PATH_MAX], *text;
	sku_strlist lines = {0};
	custom_file(in, file, sizeof file);
	if (sku_read_file(file, &text, NULL) != SKU_OK) return SKU_OK;
	sku_split_lines(text, &lines);
	for (int i = 0; i < lines.n; i++) {
		char *p = sku_str_trim(lines.v[i]);
		if (*p && *p != '#') { sku_path_normalize(p); if (!sku_strlist_has(out, p)) sku_strlist_push(out, p); }
	}
	sku_strlist_free(&lines);
	free(text);
	return SKU_OK;
}

static int custom_save(const sku_instance *in, const sku_strlist *l)
{
	char file[SKU_PATH_MAX];
	size_t cap = 1024, n = 0;
	char *buf = sku_xmalloc(cap);
	int rc;
	custom_file(in, file, sizeof file);
	for (int i = 0; i < l->n; i++) {
		size_t need = strlen(l->v[i]) + 2;
		if (n + need > cap) { while (n + need > cap) cap *= 2; buf = sku_xrealloc(buf, cap); }
		n += (size_t)sprintf(buf + n, "%s\n", l->v[i]);
	}
	rc = sku_write_file_atomic(file, buf, n);
	free(buf);
	return rc;
}

int sku_scan_custom_add(const sku_instance *in, const char *dir)
{
	sku_strlist l = {0};
	char norm[SKU_PATH_MAX];
	int rc;
	if (sku_realpath(dir, norm, sizeof norm) != SKU_OK) sku_strlcpy(norm, dir, sizeof norm);
	sku_path_normalize(norm);
	if (!sku_scan_is_addon(norm, NULL)) return SKU_ERR_NOTFOUND;
	sku_scan_custom_load(in, &l);
	if (!sku_strlist_has(&l, norm)) sku_strlist_push(&l, norm);
	rc = custom_save(in, &l);
	sku_strlist_free(&l);
	return rc;
}

int sku_scan_custom_remove(const sku_instance *in, const char *dir)
{
	sku_strlist l = {0}, kept = {0};
	char norm[SKU_PATH_MAX];
	int rc;
	sku_strlcpy(norm, dir, sizeof norm);
	sku_path_normalize(norm);
	sku_scan_custom_load(in, &l);
	for (int i = 0; i < l.n; i++) if (strcmp(l.v[i], norm) != 0) sku_strlist_push(&kept, l.v[i]);
	rc = custom_save(in, &kept);
	sku_strlist_free(&l);
	sku_strlist_free(&kept);
	return rc;
}
