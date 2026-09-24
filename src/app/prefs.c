/* Global preferences: <user config dir>/prefs.cfg, key|value like everything else. */
#include "app.h"
#include "cfg.h"
#include "platform.h"
#include "paths.h"
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void prefs_defaults(app_prefs *p)
{
	memset(p, 0, sizeof *p);
	p->theme = THEME_DARK;
	p->relief = false;
	p->text_size = 16;
	p->downloads = 8;
	p->crc_threads = sku_cpu_count();
	p->startup = 0;
	p->verbose = false;
	p->window_w = 1100;
	p->window_h = 700;
	p->client_check = true;
	sku_strlcpy(p->client_url, SKU_CLIENT_DEPOT_URL, sizeof p->client_url);
}

int prefs_load(app *ap)
{
	sku_cfg c;
	app_prefs *p = &ap->prefs;
	const char *v;
	char dir[SKU_PATH_MAX];

	prefs_defaults(p);
	if (sku_user_config_dir(dir, sizeof dir) != SKU_OK) return SKU_ERR_IO;
	sku_path_join(ap->prefs_path, sizeof ap->prefs_path, dir, "prefs.cfg");
	sku_cfg_init(&c);
	if (sku_cfg_load(&c, ap->prefs_path) != SKU_OK) { sku_cfg_free(&c); return SKU_ERR_NOTFOUND; }

	v = sku_cfg_get_or(&c, "theme", "dark");
	p->theme = strcmp(v, "light") == 0 ? THEME_LIGHT : strcmp(v, "system") == 0 ? THEME_SYSTEM : THEME_DARK;
	p->relief      = sku_cfg_get_bool(&c, "relief", false);
	p->text_size   = clamp(atoi(sku_cfg_get_or(&c, "text_size", "16")), 10, 32);
	p->downloads   = clamp(atoi(sku_cfg_get_or(&c, "downloads", "8")), 1, 32);
	p->crc_threads = clamp(atoi(sku_cfg_get_or(&c, "crc_threads", "0")), 1, 64);
	p->startup     = clamp(atoi(sku_cfg_get_or(&c, "startup", "0")), 0, 2);
	p->verbose     = sku_cfg_get_bool(&c, "verbose", false);
	p->window_w    = clamp(atoi(sku_cfg_get_or(&c, "window_w", "1100")), 640, 8192);
	p->window_h    = clamp(atoi(sku_cfg_get_or(&c, "window_h", "700")), 400, 8192);
	sku_strlcpy(p->instance, sku_cfg_get_or(&c, "instance", ""), sizeof p->instance);
	p->client_check = sku_cfg_get_bool(&c, "client_check", true);
	sku_strlcpy(p->client_url, sku_cfg_get_or(&c, "client_url", SKU_CLIENT_DEPOT_URL), sizeof p->client_url);
	if (strncmp(p->client_url, "http", 4) != 0) sku_strlcpy(p->client_url, SKU_CLIENT_DEPOT_URL, sizeof p->client_url);
	p->client_last = strtoll(sku_cfg_get_or(&c, "client_last", "0"), NULL, 10);
	sku_strlcpy(p->client_skip, sku_cfg_get_or(&c, "client_skip", ""), sizeof p->client_skip);
	v = sku_cfg_get(&c, "instances");
	if (v) {
		char *copy = sku_xstrdup(v), *tok = strtok(copy, ";");
		while (tok) { if (*tok) sku_strlist_push(&p->instances, tok); tok = strtok(NULL, ";"); }
		free(copy);
	}
	sku_cfg_free(&c);
	return SKU_OK;
}

int prefs_save(app *ap)
{
	sku_cfg c;
	app_prefs *p = &ap->prefs;
	char num[32], *list = NULL;
	size_t len = 0;
	int rc;

	if (!ap->prefs_path[0]) return SKU_ERR_IO;
	sku_cfg_init(&c);
	sku_cfg_add_raw(&c, "# SkunkCrafts Updater preferences");
	sku_cfg_set(&c, "theme", p->theme == THEME_LIGHT ? "light" : p->theme == THEME_SYSTEM ? "system" : "dark");
	sku_cfg_set_bool(&c, "relief", p->relief);
	sku_snprintf(num, sizeof num, "%d", p->text_size);   sku_cfg_set(&c, "text_size", num);
	sku_snprintf(num, sizeof num, "%d", p->downloads);   sku_cfg_set(&c, "downloads", num);
	sku_snprintf(num, sizeof num, "%d", p->crc_threads); sku_cfg_set(&c, "crc_threads", num);
	sku_snprintf(num, sizeof num, "%d", p->startup);     sku_cfg_set(&c, "startup", num);
	sku_cfg_set_bool(&c, "verbose", p->verbose);
	sku_snprintf(num, sizeof num, "%d", p->window_w);    sku_cfg_set(&c, "window_w", num);
	sku_snprintf(num, sizeof num, "%d", p->window_h);    sku_cfg_set(&c, "window_h", num);
	sku_cfg_set(&c, "instance", p->instance);
	for (int i = 0; i < p->instances.n; i++) {
		size_t k = strlen(p->instances.v[i]);
		list = sku_xrealloc(list, len + k + 2);
		if (len) list[len++] = ';';
		memcpy(list + len, p->instances.v[i], k + 1);
		len += k;
	}
	sku_cfg_set(&c, "instances", list ? list : "");
	free(list);
	sku_cfg_set_bool(&c, "client_check", p->client_check);
	sku_cfg_set(&c, "client_url", p->client_url);
	sku_snprintf(num, sizeof num, "%lld", (long long)p->client_last); sku_cfg_set(&c, "client_last", num);
	sku_cfg_set(&c, "client_skip", p->client_skip);
	rc = sku_cfg_save(&c, ap->prefs_path);
	sku_cfg_free(&c);
	return rc;
}

/* Reads one numeric or boolean field of the 3.x JSON preferences ("Key": value). */
static bool json_field(const char *json, const char *key, char *out, size_t n)
{
	char pat[64];
	const char *p, *e;
	sku_snprintf(pat, sizeof pat, "\"%s\"", key);
	p = strstr(json, pat);
	if (!p) return false;
	p = strchr(p + strlen(pat), ':');
	if (!p) return false;
	p++;
	while (*p == ' ' || *p == '\t') p++;
	e = p;
	while (*e && *e != ',' && *e != '}' && *e != '\n' && *e != '\r') e++;
	while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
	if ((size_t)(e - p) >= n) return false;
	memcpy(out, p, (size_t)(e - p)); out[e - p] = 0;
	return true;
}

void prefs_migrate_legacy(app *ap)
{
	char json_path[SKU_PATH_MAX], log_path[SKU_PATH_MAX], *json, val[32];
	app_prefs *p = &ap->prefs;
	if (!ap->inst_open) return;
	sku_path_join(json_path, sizeof json_path, ap->inst.root, "Output/preferences/skunkcrafts_updater_go.json");
	if (sku_read_file(json_path, &json, NULL) == SKU_OK) {
		if (json_field(json, "Startup", val, sizeof val))    p->startup = clamp(atoi(val), 0, 2);
		if (json_field(json, "Verbose", val, sizeof val))    p->verbose = strcmp(val, "true") == 0;
		if (json_field(json, "TextSize", val, sizeof val))   p->text_size = clamp((int)atof(val), 10, 32);
		if (json_field(json, "Downloads", val, sizeof val))  p->downloads = clamp(atoi(val), 1, 32);
		if (json_field(json, "CRCWorkers", val, sizeof val)) p->crc_threads = clamp(atoi(val), 1, 64);
		free(json);
		prefs_save(ap);
		if (sku_remove_file(json_path) == SKU_OK) sku_info("migrated and removed 3.x preferences %s", json_path);
	}
	sku_path_join(log_path, sizeof log_path, ap->inst.root, "skunkcrafts_updater_log.txt");
	if (sku_is_file(log_path) && sku_remove_file(log_path) == SKU_OK) sku_info("removed 3.x log file %s", log_path);
}
