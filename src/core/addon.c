/* Addon: local configuration, v1 <-> v4 layouts, status, settings. */
#include "addon.h"
#include "platform.h"
#include "paths.h"
#include "log.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

const char *sku_status_name(sku_status s)
{
	static const char *names[] = {
		"local", "disabled", "checking", "up-to-date", "version-mismatch", "repair", "update",
		"locked", "updating", "updated", "cancelled", "error-config", "error-net", "error-files",
	};
	return (unsigned)s < sizeof names / sizeof *names ? names[s] : "?";
}

const char *sku_reason_name(sku_reason r)
{
	static const char *names[] = { "new", "changed", "delete", "ignored", "error" };
	return (unsigned)r < sizeof names / sizeof *names ? names[r] : "?";
}

void sku_addon_set_status(sku_addon *a, sku_status s, const char *text, const char *detail)
{
	pthread_mutex_lock(&a->lock);
	a->status = s;
	sku_strlcpy(a->status_text, text ? text : "", sizeof a->status_text);
	sku_strlcpy(a->status_detail, detail ? detail : "", sizeof a->status_detail);
	pthread_mutex_unlock(&a->lock);
}

sku_status sku_addon_status(sku_addon *a, char *text, size_t tn, char *detail, size_t dn)
{
	sku_status s;
	pthread_mutex_lock(&a->lock);
	s = a->status;
	if (text) sku_strlcpy(text, a->status_text, tn);
	if (detail) sku_strlcpy(detail, a->status_detail, dn);
	pthread_mutex_unlock(&a->lock);
	return s;
}

void sku_plan_clear(sku_plan *p)
{
	for (int i = 0; i < p->n; i++) { free(p->v[i].path); free(p->v[i].detail); }
	free(p->v);
	memset(p, 0, sizeof *p);
}

sku_plan_item *sku_plan_add(sku_plan *p, const char *path, sku_reason reason, int64_t size, uint32_t crc, bool check_crc)
{
	sku_plan_item *it;
	if (p->n == p->cap) {
		p->cap = p->cap ? p->cap * 2 : 64;
		p->v = sku_xrealloc(p->v, (size_t)p->cap * sizeof *p->v);
	}
	it = &p->v[p->n++];
	memset(it, 0, sizeof *it);
	it->path = sku_xstrdup(path);
	it->reason = reason; it->size = size; it->crc = crc; it->check_crc = check_crc;
	switch (reason) {
	case SKU_PLAN_NEW:     p->n_new++;     if (size > 0) p->bytes += size; break;
	case SKU_PLAN_CHANGED: p->n_changed++; if (size > 0) p->bytes += size; break;
	case SKU_PLAN_DELETE:  p->n_delete++;  break;
	case SKU_PLAN_IGNORED: p->n_ignored++; break;
	case SKU_PLAN_ERROR:   p->n_error++;   break;
	}
	return it;
}

/* base_url + rel, with every path component percent-encoded ('/' kept). */
void sku_addon_encode_url(char *out, size_t n, const char *base_url, const char *rel)
{
	const char *p = rel;
	sku_strlcpy(out, base_url, n);
	while (*p) {
		const char *slash = strchr(p, '/');
		size_t len = slash ? (size_t)(slash - p) : strlen(p);
		char comp[SKU_PATH_MAX], *enc;
		if (len >= sizeof comp) len = sizeof comp - 1;
		memcpy(comp, p, len); comp[len] = 0;
		enc = curl_easy_escape(NULL, comp, (int)len);
		if (enc) { sku_strlcat(out, enc, n); curl_free(enc); }
		if (slash) sku_strlcat(out, "/", n);
		p = slash ? slash + 1 : p + len;
	}
}

/* ------------------------------------------------------------ channels */

static void channels_clear(sku_addon *a)
{
	free(a->channels);
	a->channels = NULL; a->nchannels = 0;
}

static sku_channel *channel_get(sku_addon *a, const char *name)
{
	for (int i = 0; i < a->nchannels; i++) if (strcmp(a->channels[i].name, name) == 0) return &a->channels[i];
	return NULL;
}

static sku_channel *channel_put(sku_addon *a, const char *name)
{
	sku_channel *c = channel_get(a, name);
	if (c) return c;
	a->channels = sku_xrealloc(a->channels, (size_t)(a->nchannels + 1) * sizeof *a->channels);
	c = &a->channels[a->nchannels++];
	memset(c, 0, sizeof *c);
	sku_strlcpy(c->name, name, sizeof c->name);
	sku_strlcpy(c->zone, "custom", sizeof c->zone);
	return c;
}

/* Reads "channel.<name>.module|..." style keys from the unified cfg. */
static void channels_from_cfg(sku_addon *a)
{
	channels_clear(a);
	for (int i = 0; i < a->local.n; i++) {
		const char *k = a->local.v[i].key, *dot;
		char name[64];
		size_t len;
		if (!k || strncmp(k, "channel.", 8) != 0) continue;
		dot = strchr(k + 8, '.');
		if (!dot) continue;
		len = (size_t)(dot - (k + 8));
		if (len == 0 || len >= sizeof name) continue;
		memcpy(name, k + 8, len); name[len] = 0;
		if (strcmp(dot + 1, "module") == 0) sku_strlcpy(channel_put(a, name)->module, a->local.v[i].value, SKU_URL_MAX);
		else if (strcmp(dot + 1, "zone") == 0) sku_strlcpy(channel_put(a, name)->zone, a->local.v[i].value, 64);
	}
}

/* A 3.x beta file: skunkcrafts_updater_beta*.cfg -> channel keys, then the file is dropped on save. */
static void merge_beta_file(sku_addon *a, const char *file, const char *default_name)
{
	sku_cfg b;
	char key[128];
	const char *name, *module;
	sku_cfg_init(&b);
	if (sku_cfg_load(&b, file) != SKU_OK) { sku_cfg_free(&b); return; }
	name = sku_cfg_get_or(&b, "channel", default_name);
	module = sku_cfg_get(&b, "module");
	if (module && *name) {
		sku_snprintf(key, sizeof key, "channel.%s.module", name); sku_cfg_set(&a->local, key, module);
		sku_snprintf(key, sizeof key, "channel.%s.zone", name);   sku_cfg_set(&a->local, key, sku_cfg_get_or(&b, "zone", "custom"));
		/* the legacy "enabled" flag selects the active channel */
		if (sku_cfg_get_bool(&b, "enabled", true)) sku_cfg_set(&a->local, "channel", name);
		sku_info("%s: merged beta channel \"%s\" from %s", a->dir, name, sku_path_basename(file));
	} else {
		sku_warn("%s: beta file %s has no module, ignored", a->dir, sku_path_basename(file));
	}
	sku_cfg_free(&b);
	a->dirty = true;
}

struct beta_scan { sku_addon *a; };

static int beta_cb(const char *name, bool is_dir, bool is_symlink, void *user)
{
	struct beta_scan *s = user;
	char file[SKU_PATH_MAX], chan[64];
	(void)is_symlink;
	if (is_dir || strncmp(name, "skunkcrafts_updater_beta", 24) != 0 || !sku_str_ends(name, ".cfg")) return 0;
	sku_path_join(file, sizeof file, s->a->dir, name);
	if (strcmp(name, SKU_F_BETA_CFG) == 0) sku_strlcpy(chan, "beta", sizeof chan);
	else {
		/* skunkcrafts_updater_beta_<x>.cfg -> "<x>" */
		const char *p = name + 24;
		if (*p == '_') p++;
		sku_strlcpy(chan, p, sizeof chan);
		if (sku_str_ends(chan, ".cfg")) chan[strlen(chan) - 4] = 0;
		if (!*chan) sku_strlcpy(chan, "beta", sizeof chan);
	}
	merge_beta_file(s->a, file, chan);
	return 0;
}

/* ------------------------------------------------------------ load / save */

static void fields_from_cfg(sku_addon *a)
{
	const sku_channel *c;
	sku_strlcpy(a->name,    sku_cfg_get_or(&a->local, "name", ""), sizeof a->name);
	sku_strlcpy(a->version, sku_cfg_get_or(&a->local, "version", ""), sizeof a->version);
	sku_strlcpy(a->module,  sku_cfg_get_or(&a->local, "module", ""), sizeof a->module);
	sku_strlcpy(a->zone,    sku_cfg_get_or(&a->local, "zone", "custom"), sizeof a->zone);
	a->disabled = sku_cfg_get_bool(&a->local, "disabled", false);
	a->liveries = sku_cfg_get_bool(&a->local, "liveries", true);
	sku_strlcpy(a->channel, sku_cfg_get_or(&a->local, "channel", "stable"), sizeof a->channel);
	sku_strlcpy(a->pack_id,   sku_cfg_get_or(&a->local, "pack", ""), sizeof a->pack_id);
	sku_strlcpy(a->member_id, sku_cfg_get_or(&a->local, "member", ""), sizeof a->member_id);
	sku_strlcpy(a->pack_name, sku_cfg_get_or(&a->local, "pack_name", ""), sizeof a->pack_name);
	channels_from_cfg(a);
	/* an active channel that no longer exists falls back to stable */
	if (strcmp(a->channel, "stable") != 0 && !channel_get(a, a->channel)) {
		sku_warn("%s: channel \"%s\" is not defined, using stable", a->dir, a->channel);
		sku_strlcpy(a->channel, "stable", sizeof a->channel);
		sku_cfg_set(&a->local, "channel", "stable");
		a->dirty = true;
	}
	c = strcmp(a->channel, "stable") == 0 ? NULL : channel_get(a, a->channel);
	sku_protocol_base_url(a->base_url, sizeof a->base_url, c ? c->zone : a->zone, c ? c->module : a->module);
}

static void load_ignore(sku_addon *a, const char *file)
{
	char *text;
	int rejected = 0;
	if (sku_read_file(file, &text, NULL) != SKU_OK) return;
	sku_protocol_parse_pathlist(text, &a->ignore, &rejected);
	free(text);
	if (rejected) sku_warn("%s: %d unsafe line(s) in the ignore list", a->dir, rejected);
}

int sku_addon_reload(sku_addon *a)
{
	char p[SKU_PATH_MAX];
	int rc;

	sku_cfg_free(&a->local);
	sku_strlist_free(&a->ignore);
	channels_clear(a);
	a->dirty = false;
	a->checked = false;

	sku_path_join(p, sizeof p, a->dir, SKU_F_V4_CFG);
	a->v4 = sku_is_file(p);
	if (!a->v4) sku_path_join(p, sizeof p, a->dir, SKU_F_CFG);
	rc = sku_cfg_load(&a->local, p);
	if (rc != SKU_OK) {
		sku_addon_set_status(a, SKU_ST_ERROR_CONFIG, "Cannot read the local configuration", p);
		return rc;
	}
	if (!sku_cfg_has(&a->local, "name") || !sku_cfg_has(&a->local, "module") || !sku_cfg_has(&a->local, "version")) {
		sku_addon_set_status(a, SKU_ST_ERROR_CONFIG, "Local configuration is missing name, module or version", p);
		return SKU_ERR_FORMAT;
	}
	sku_cfg_set_default(&a->local, "zone", "custom");
	sku_cfg_set_default(&a->local, "disabled", "false");
	sku_cfg_set_default(&a->local, "channel", "stable");
	if (a->genre == SKU_GENRE_AIRCRAFT) sku_cfg_set_default(&a->local, "liveries", "true");

	if (a->v4) {
		sku_path_join(p, sizeof p, a->dir, SKU_F_V4_IGNORE);
		load_ignore(a, p);
	} else {
		sku_path_join(p, sizeof p, a->dir, SKU_F_IGNORE);
		load_ignore(a, p);
	}
	/* 3.x beta files are merged from both layouts: a tester can drop one later */
	{
		struct beta_scan s = { a };
		sku_list_dir(a->dir, beta_cb, &s);
	}
	fields_from_cfg(a);
	sku_addon_set_status(a, a->disabled ? SKU_ST_DISABLED : SKU_ST_LOCAL,
		a->disabled ? "Updates disabled by user" : "Information from local configuration", "");
	return SKU_OK;
}

sku_addon *sku_addon_open(const sku_instance *in, const char *dir, sku_genre genre)
{
	sku_addon *a = sku_xcalloc(1, sizeof *a);
	a->inst = in;
	sku_strlcpy(a->dir, dir, sizeof a->dir);
	sku_path_normalize(a->dir);
	a->genre = genre;
	pthread_mutex_init(&a->lock, NULL);
	sku_cfg_init(&a->local);
	sku_cfg_init(&a->remote);
	atomic_init(&a->cancel, 0);
	sku_addon_reload(a);
	return a;
}

void sku_addon_close(sku_addon *a)
{
	if (!a) return;
	sku_cfg_free(&a->local);
	sku_cfg_free(&a->remote);
	sku_strlist_free(&a->ignore);
	sku_strlist_free(&a->blacklist);
	sku_remote_files_free(&a->files);
	sku_plan_clear(&a->plan);
	channels_clear(a);
	pthread_mutex_destroy(&a->lock);
	free(a);
}

struct v1_cleanup { sku_addon *a; };

static int cleanup_cb(const char *name, bool is_dir, bool is_symlink, void *user)
{
	struct v1_cleanup *c = user;
	char file[SKU_PATH_MAX];
	(void)is_symlink;
	if (is_dir) return 0;
	if (strcmp(name, SKU_F_CFG) == 0 || strcmp(name, SKU_F_IGNORE) == 0 ||
	    (strncmp(name, "skunkcrafts_updater_beta", 24) == 0 && sku_str_ends(name, ".cfg"))) {
		sku_path_join(file, sizeof file, c->a->dir, name);
		if (sku_remove_file(file) == SKU_OK) sku_info("%s: removed 3.x file %s", c->a->dir, name);
	}
	return 0;
}

int sku_addon_save(sku_addon *a)
{
	char dir[SKU_PATH_MAX], p[SKU_PATH_MAX];
	int rc;
	sku_path_join(dir, sizeof dir, a->dir, SKU_DIR_V4);
	if (sku_mkdir_p(dir) != SKU_OK) return SKU_ERR_IO;
	sku_path_join(p, sizeof p, a->dir, SKU_F_V4_CFG);
	rc = sku_cfg_save(&a->local, p);
	if (rc != SKU_OK) { sku_error("%s: cannot write %s", a->dir, p); return rc; }
	if (!a->v4) {
		/* first save in v4: move the ignore list, drop the 3.x files */
		char old[SKU_PATH_MAX];
		struct v1_cleanup c = { a };
		sku_path_join(old, sizeof old, a->dir, SKU_F_IGNORE);
		sku_path_join(p, sizeof p, a->dir, SKU_F_V4_IGNORE);
		if (sku_is_file(old)) sku_rename(old, p);
		sku_list_dir(a->dir, cleanup_cb, &c);
		sku_info("%s: converted to the v4 layout", a->dir);
		a->v4 = true;
	} else {
		/* beta files dropped in later are merged on load: remove them now */
		struct v1_cleanup c = { a };
		sku_list_dir(a->dir, cleanup_cb, &c);
	}
	a->dirty = false;
	return SKU_OK;
}

void sku_addon_cancel(sku_addon *a)
{
	atomic_store(&a->cancel, 1);
}

/* ------------------------------------------------------------ settings */

static int apply(sku_addon *a, bool dry_run)
{
	fields_from_cfg(a);
	a->dirty = true;
	a->checked = false;
	return dry_run ? SKU_OK : sku_addon_save(a);
}

int sku_addon_set_disabled(sku_addon *a, bool disabled, bool dry_run)
{
	sku_cfg_set_bool(&a->local, "disabled", disabled);
	sku_info("%s: disabled = %s", a->dir, sku_bool_to_str(disabled));
	sku_addon_set_status(a, disabled ? SKU_ST_DISABLED : SKU_ST_LOCAL,
		disabled ? "Updates disabled by user" : "Information from local configuration", "");
	return apply(a, dry_run);
}

int sku_addon_set_liveries(sku_addon *a, bool liveries, bool dry_run)
{
	sku_cfg_set_bool(&a->local, "liveries", liveries);
	sku_info("%s: liveries = %s", a->dir, sku_bool_to_str(liveries));
	return apply(a, dry_run);
}

int sku_addon_set_channel(sku_addon *a, const char *channel, bool dry_run)
{
	if (strcmp(channel, "stable") != 0 && !channel_get(a, channel)) return SKU_ERR_NOTFOUND;
	sku_cfg_set(&a->local, "channel", channel);
	sku_info("%s: channel = %s", a->dir, channel);
	return apply(a, dry_run);
}
