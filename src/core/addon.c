/* Addon: local configuration (skunkcrafts_updater.cfg, kept where 3.x, aircraft and other
 * tools read it; the beta channels in the addon's private skunkcrafts/ folder), status, settings. */
#include "addon.h"
#include "platform.h"
#include "paths.h"
#include "log.h"
#include <curl/curl.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

void sku_addon_crc_cache_path(const sku_addon *a, bool dry_run, char *out, size_t n)
{
	char dir[SKU_PATH_MAX];
	sku_path_join(dir, sizeof dir, a->dir, SKU_DIR_V4);
	if (!dry_run) sku_mkdir_p(dir);
	sku_path_join(out, n, a->dir, SKU_F_V4_CRC);
}

void sku_url_add_query(char *url, size_t n, const char *key, const char *value)
{
	char *enc = curl_easy_escape(NULL, value, 0);
	sku_strlcat(url, strchr(url, '?') ? "&" : "?", n);
	sku_strlcat(url, key, n);
	sku_strlcat(url, "=", n);
	if (enc) { sku_strlcat(url, enc, n); curl_free(enc); }
}

/* ------------------------------------------------------------ channels */

/* Beta channels are the client's own business and live in the addon's private
 * folder: one file per channel, skunkcrafts/channels/<file>.cfg (channel|,
 * module|, zone|, the tester file's format), and the active channel in
 * skunkcrafts/channel.cfg. skunkcrafts_updater.cfg stays the public file that
 * 3.x, aircraft and publishing tools read. Saving never deletes a channel file:
 * only sku_addon_remove_channel does, and it keeps the file as <file>.removed. */

static int channels_save(sku_addon *a);

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

static bool valid_channel_name(const char *name)
{
	return name && *name && strlen(name) < sizeof ((sku_channel *)0)->name && strcmp(name, "stable") != 0
		&& !strchr(name, '|') && !strchr(name, '\n') && !strchr(name, '\r');
}

/* Same depot: compared on the base URL the client would use (zone, rewrites, trailing slashes). */
static bool same_depot(const char *zone_a, const char *module_a, const char *zone_b, const char *module_b)
{
	char ua[SKU_URL_MAX], ub[SKU_URL_MAX];
	sku_protocol_base_url(ua, sizeof ua, zone_a, module_a);
	sku_protocol_base_url(ub, sizeof ub, zone_b, module_b);
	return strcmp(ua, ub) == 0;
}

static int channel_cmp(const void *x, const void *y)
{
	return strcmp(((const sku_channel *)x)->name, ((const sku_channel *)y)->name);
}

/* A file name for a channel saved for the first time: its name with the unsafe
 * characters replaced, never the name of another channel's file or of a file
 * already there (an unreadable channel file is left alone, not overwritten). */
static void channel_file_assign(sku_addon *a, sku_channel *c)
{
	char base[48], dir[SKU_PATH_MAX], p[SKU_PATH_MAX];
	size_t k = 0;
	if (c->file[0]) return;
	for (const char *s = c->name; *s && k < 40; s++) base[k++] = (isalnum((unsigned char)*s) || *s == '-' || *s == '_') ? *s : '_';
	base[k] = 0;
	if (!k) sku_strlcpy(base, "channel", sizeof base);
	sku_path_join(dir, sizeof dir, a->dir, SKU_D_CHANNELS);
	for (int n = 1; ; n++) {
		bool taken = false;
		if (n == 1) sku_snprintf(c->file, sizeof c->file, "%s.cfg", base);
		else sku_snprintf(c->file, sizeof c->file, "%s-%d.cfg", base, n);
		for (int i = 0; i < a->nchannels; i++) if (&a->channels[i] != c && strcmp(a->channels[i].file, c->file) == 0) taken = true;
		sku_path_join(p, sizeof p, dir, c->file);
		if (!taken && !sku_exists(p)) return;
	}
}

struct chan_scan { sku_addon *a; char dir[SKU_PATH_MAX]; };

static int chan_cb(const char *name, bool is_dir, bool is_symlink, void *user)
{
	struct chan_scan *s = user;
	char file[SKU_PATH_MAX], stem[64];
	const char *cname, *module;
	size_t len = strlen(name);
	sku_cfg c;
	(void)is_symlink;
	if (is_dir || !sku_str_ends(name, ".cfg") || len >= sizeof ((sku_channel *)0)->file) return 0;
	sku_path_join(file, sizeof file, s->dir, name);
	sku_strlcpy(stem, name, sizeof stem);
	if (len - 4 < sizeof stem) stem[len - 4] = 0;
	sku_cfg_init(&c);
	if (sku_cfg_load(&c, file) == SKU_OK) {
		cname = sku_cfg_get_or(&c, "channel", stem);
		module = sku_cfg_get(&c, "module");
		if (!module || !*module || !valid_channel_name(cname)) sku_warn("%s: channel file %s has no module or an unusable name, ignored", s->a->dir, name);
		else if (channel_get(s->a, cname)) sku_warn("%s: channel file %s repeats the channel \"%s\", ignored", s->a->dir, name, cname);
		else {
			sku_channel *ch = channel_put(s->a, cname);
			sku_strlcpy(ch->module, module, sizeof ch->module);
			sku_strlcpy(ch->zone, sku_cfg_get_or(&c, "zone", "custom"), sizeof ch->zone);
			sku_strlcpy(ch->file, name, sizeof ch->file);
			ch->is_public = sku_cfg_get_bool(&c, "public", false);
		}
	}
	sku_cfg_free(&c);
	return 0;
}

/* The channels and the active one, from the private folder. Returns whether
 * an active channel was recorded there. */
static bool channels_load(sku_addon *a)
{
	struct chan_scan s;
	char p[SKU_PATH_MAX];
	sku_cfg c;
	bool known = false;
	s.a = a;
	sku_path_join(s.dir, sizeof s.dir, a->dir, SKU_D_CHANNELS);
	if (sku_is_dir(s.dir)) sku_list_dir(s.dir, chan_cb, &s);
	sku_strlcpy(a->channel, "stable", sizeof a->channel);
	sku_path_join(p, sizeof p, a->dir, SKU_F_CHANNEL);
	sku_cfg_init(&c);
	if (sku_is_file(p) && sku_cfg_load(&c, p) == SKU_OK && sku_cfg_has(&c, "channel")) {
		sku_strlcpy(a->channel, sku_cfg_get(&c, "channel"), sizeof a->channel);
		known = true;
	}
	sku_cfg_free(&c);
	return known;
}

/* 4.0.0 to 4.0.2 kept the channels in skunkcrafts_updater.cfg ("channel|<active>",
 * "channel.<name>.module|", "channel.<name>.zone|"): they move to the private
 * folder, the keys leave the public file. A channel already in the folder wins. */
static void channels_from_root_cfg(sku_addon *a, bool active_known)
{
	sku_strlist drop = {0};
	for (int i = 0; i < a->local.n; i++) {
		const char *k = a->local.v[i].key, *v = a->local.v[i].value, *dot;
		char name[64];
		size_t len;
		if (!k) continue;
		if (strcmp(k, "channel") == 0) {
			if (!active_known && v) sku_strlcpy(a->channel, v, sizeof a->channel);
			sku_strlist_push(&drop, k);
			continue;
		}
		if (strncmp(k, "channel.", 8) != 0 || !(dot = strrchr(k, '.')) || dot < k + 8) continue;
		sku_strlist_push(&drop, k);
		len = (size_t)(dot - (k + 8));
		if (len == 0 || len >= sizeof name || !v) continue;
		memcpy(name, k + 8, len); name[len] = 0;
		if (!valid_channel_name(name)) continue;
		if (strcmp(dot + 1, "module") == 0) {
			sku_channel *c = channel_get(a, name);
			if (!c) { c = channel_put(a, name); c->migrated = true; }
			if (c->migrated) sku_strlcpy(c->module, v, sizeof c->module);
		} else if (strcmp(dot + 1, "zone") == 0) {
			sku_channel *c = channel_get(a, name);
			if (!c) { c = channel_put(a, name); c->migrated = true; }
			if (c->migrated) sku_strlcpy(c->zone, v, sizeof c->zone);
		}
	}
	if (drop.n) {
		for (int i = 0; i < drop.n; i++) sku_cfg_remove(&a->local, drop.v[i]);
		for (int i = 0; i < a->nchannels; ) {   /* a zone without a module is no channel */
			if (!a->channels[i].module[0]) { memmove(&a->channels[i], &a->channels[i + 1], (size_t)(a->nchannels - i - 1) * sizeof *a->channels); a->nchannels--; }
			else i++;
		}
		{
			int moved = 0;
			for (int i = 0; i < a->nchannels; i++) moved += a->channels[i].migrated;
			if (moved) sku_info("%s: %d beta channel(s) moved from %s to %s/", a->dir, moved, SKU_F_CFG, SKU_D_CHANNELS);
			else sku_debug("%s: channel key removed from %s", a->dir, SKU_F_CFG);
		}
		a->dirty = true;
	}
	sku_strlist_free(&drop);
}

/* A tester file, skunkcrafts_updater_beta*.cfg, dropped into the addon: it becomes a
 * channel, then the file is absorbed at the next save. Users put the same file back
 * when they see it gone, so a file never adds a second channel for a depot already
 * followed: that channel is updated, and renamed only when the file names it
 * explicitly ("channel|<name>"). A file pointing at the main depot adds nothing. */
static void merge_beta_file(sku_addon *a, const char *file, const char *default_name)
{
	sku_cfg b;
	const char *name, *module, *zone, *base = sku_path_basename(file);
	bool explicit_name;
	sku_channel *c = NULL;
	sku_cfg_init(&b);
	if (sku_cfg_load(&b, file) != SKU_OK) { sku_cfg_free(&b); return; }
	a->dirty = true;   /* absorbed at the next save, whatever it holds */
	explicit_name = sku_cfg_has(&b, "channel") && *sku_cfg_get(&b, "channel");
	name = explicit_name ? sku_cfg_get(&b, "channel") : default_name;
	module = sku_cfg_get(&b, "module");
	zone = sku_cfg_get_or(&b, "zone", "custom");
	if (!module || !*module || !valid_channel_name(name)) {
		sku_warn("%s: beta file %s has no module or an unusable channel name, ignored", a->dir, base);
		sku_cfg_free(&b);
		return;
	}
	if (same_depot(zone, module, a->zone, a->module)) {
		sku_info("%s: %s points at the main depot, no channel added", a->dir, base);
		sku_cfg_free(&b);
		return;
	}
	for (int i = 0; i < a->nchannels && !c; i++) if (same_depot(a->channels[i].zone, a->channels[i].module, zone, module)) c = &a->channels[i];
	if (c) {
		/* this depot is already followed: the same channel */
		if (explicit_name && strcmp(c->name, name) != 0 && !channel_get(a, name)) {
			sku_info("%s: channel \"%s\" renamed \"%s\" by %s", a->dir, c->name, name, base);
			if (strcmp(a->channel, c->name) == 0) sku_strlcpy(a->channel, name, sizeof a->channel);
			sku_strlcpy(c->name, name, sizeof c->name);
		} else sku_info("%s: %s: channel \"%s\" already follows this depot, nothing added", a->dir, base, c->name);
		sku_snprintf(a->notice, sizeof a->notice, "%s: the beta file is the channel \"%s\", pick it in the Channel column", sku_cfg_get_or(&a->local, "name", base), c->name);
	} else {
		/* a new depot: the channel of that name moves to it, as does the one the file would
		 * have created without "channel|" (3.x: one file, one beta); otherwise a new channel */
		c = channel_get(a, name);
		if (!c && explicit_name && strcmp(name, default_name) != 0 && (c = channel_get(a, default_name)) != NULL) {
			sku_info("%s: channel \"%s\" renamed \"%s\" by %s", a->dir, c->name, name, base);
			if (strcmp(a->channel, c->name) == 0) sku_strlcpy(a->channel, name, sizeof a->channel);
			sku_strlcpy(c->name, name, sizeof c->name);
		}
		if (c) sku_info("%s: channel \"%s\" now follows %s (from %s)", a->dir, c->name, module, base);
		else { c = channel_put(a, name); sku_info("%s: new beta channel \"%s\" from %s", a->dir, name, base); }
		sku_strlcpy(c->module, module, sizeof c->module);
		sku_snprintf(a->notice, sizeof a->notice, "%s: the beta file became the channel \"%s\", pick it in the Channel column", sku_cfg_get_or(&a->local, "name", base), c->name);
	}
	sku_strlcpy(c->zone, zone, sizeof c->zone);
	/* the legacy "enabled" flag selects the active channel: true = follow this channel, false = production */
	if (sku_cfg_get_bool(&b, "enabled", true)) sku_strlcpy(a->channel, c->name, sizeof a->channel);
	else if (strcmp(a->channel, c->name) == 0) sku_strlcpy(a->channel, "stable", sizeof a->channel);
	sku_cfg_free(&b);
}

/* The channel a tester file names when it has no "channel|" line: "beta" for
 * skunkcrafts_updater_beta.cfg, "<x>" for skunkcrafts_updater_beta_<x>.cfg. */
static void beta_default_name(const char *file, char *out, size_t n)
{
	const char *p = file + 24;
	if (*p == '_') p++;
	sku_strlcpy(out, p, n);
	if (strlen(out) >= 4 && strcasecmp(out + strlen(out) - 4, ".cfg") == 0) out[strlen(out) - 4] = 0;
	if (!*out) sku_strlcpy(out, "beta", n);
}

struct beta_scan { sku_addon *a; };

static int beta_cb(const char *name, bool is_dir, bool is_symlink, void *user)
{
	struct beta_scan *s = user;
	char file[SKU_PATH_MAX], chan[64];
	(void)is_symlink;
	if (is_dir || strncmp(name, "skunkcrafts_updater_beta", 24) != 0 || !sku_str_ends(name, ".cfg")) return 0;
	sku_path_join(file, sizeof file, s->a->dir, name);
	beta_default_name(name, chan, sizeof chan);
	merge_beta_file(s->a, file, chan);
	return 0;
}

/* Whether the user removed a channel following that depot (a <file>.removed is kept). */
struct removed_scan { const char *dir, *zone, *module; bool found; };

static int removed_cb(const char *name, bool is_dir, bool is_symlink, void *user)
{
	struct removed_scan *s = user;
	char file[SKU_PATH_MAX];
	sku_cfg c;
	(void)is_symlink;
	if (is_dir || s->found || !sku_str_ends(name, ".cfg.removed")) return 0;
	sku_path_join(file, sizeof file, s->dir, name);
	sku_cfg_init(&c);
	if (sku_cfg_load(&c, file) == SKU_OK && sku_cfg_has(&c, "module")
	    && same_depot(sku_cfg_get_or(&c, "zone", "custom"), sku_cfg_get(&c, "module"), s->zone, s->module)) s->found = true;
	sku_cfg_free(&c);
	return 0;
}

void sku_addon_channel_offer(sku_addon *a, const char *text, const char *file, bool dry_run)
{
	sku_cfg b;
	char def[64], dir[SKU_PATH_MAX];
	const char *name, *module, *zone;
	struct removed_scan rs;
	sku_channel *c;
	sku_cfg_init(&b);
	sku_cfg_parse(&b, text);
	beta_default_name(file, def, sizeof def);
	name = sku_cfg_has(&b, "channel") && *sku_cfg_get(&b, "channel") ? sku_cfg_get(&b, "channel") : def;
	module = sku_cfg_get(&b, "module");
	zone = sku_cfg_get_or(&b, "zone", "custom");
	if (!module || !*module || !valid_channel_name(name) || same_depot(zone, module, a->zone, a->module)) { sku_cfg_free(&b); return; }
	for (int i = 0; i < a->nchannels; i++)
		if (same_depot(a->channels[i].zone, a->channels[i].module, zone, module)) { sku_cfg_free(&b); return; }   /* already there */
	if (channel_get(a, name)) {
		sku_debug("%s: the depot offers the channel \"%s\" (%s), a channel of that name exists: left as it is", a->name, name, module);
		sku_cfg_free(&b);
		return;
	}
	sku_path_join(dir, sizeof dir, a->dir, SKU_D_CHANNELS);
	rs.dir = dir; rs.zone = zone; rs.module = module; rs.found = false;
	if (sku_is_dir(dir)) sku_list_dir(dir, removed_cb, &rs);
	if (rs.found) {
		sku_debug("%s: the depot offers the channel \"%s\", removed by the user: not offered again", a->name, name);
		sku_cfg_free(&b);
		return;
	}
	c = channel_put(a, name);   /* offered, not followed: "enabled" is the tester's business */
	sku_strlcpy(c->module, module, sizeof c->module);
	sku_strlcpy(c->zone, zone, sizeof c->zone);
	qsort(a->channels, (size_t)a->nchannels, sizeof *a->channels, channel_cmp);
	sku_info("%s: the depot offers the beta channel \"%s\" (%s, from %s): added, not followed", a->name, name, module, file);
	sku_snprintf(a->notice, sizeof a->notice, "%s: new beta channel \"%s\" offered by the developer, pick it in the Channel column to follow it", a->name, name);
	if (!dry_run) channels_save(a);
	sku_cfg_free(&b);
}

/* Writes every channel file and the active channel. Nothing is deleted here. */
static int channels_save(sku_addon *a)
{
	char dir[SKU_PATH_MAX], p[SKU_PATH_MAX];
	sku_path_join(p, sizeof p, a->dir, SKU_F_CHANNEL);
	if (!a->nchannels && !sku_is_file(p)) return SKU_OK;   /* no channel ever: nothing to write */
	sku_path_join(dir, sizeof dir, a->dir, SKU_D_CHANNELS);
	if (sku_mkdir_p(dir) != SKU_OK) { sku_error("%s: cannot create %s", a->dir, dir); return SKU_ERR_IO; }
	for (int i = 0; i < a->nchannels; i++) {
		sku_channel *c = &a->channels[i];
		char text[SKU_URL_MAX + 256];
		int n;
		channel_file_assign(a, c);
		n = sku_snprintf(text, sizeof text, "channel|%s\nmodule|%s\nzone|%s\n%s", c->name, c->module, c->zone, c->is_public ? "public|true\n" : "");
		sku_path_join(p, sizeof p, dir, c->file);
		if (sku_write_file_atomic(p, text, (size_t)n) != SKU_OK) { sku_error("%s: cannot write %s", a->dir, p); return SKU_ERR_IO; }
		c->migrated = false;
	}
	{
		char text[128];
		int n = sku_snprintf(text, sizeof text, "channel|%s\n", a->channel);
		sku_path_join(p, sizeof p, a->dir, SKU_F_CHANNEL);
		if (sku_write_file_atomic(p, text, (size_t)n) != SKU_OK) { sku_error("%s: cannot write %s", a->dir, p); return SKU_ERR_IO; }
	}
	return SKU_OK;
}

/* ------------------------------------------------------------ load / save */

static void fields_from_cfg(sku_addon *a)
{
	sku_strlcpy(a->name,    sku_cfg_get_or(&a->local, "name", ""), sizeof a->name);
	sku_strlcpy(a->version, sku_cfg_get_or(&a->local, "version", ""), sizeof a->version);
	sku_strlcpy(a->module,  sku_cfg_get_or(&a->local, "module", ""), sizeof a->module);
	sku_strlcpy(a->zone,    sku_cfg_get_or(&a->local, "zone", "custom"), sizeof a->zone);
	a->disabled = sku_cfg_get_bool(&a->local, "disabled", false);
	a->liveries = sku_cfg_get_bool(&a->local, "liveries", true);
	sku_strlcpy(a->pack_id,   sku_cfg_get_or(&a->local, "pack", ""), sizeof a->pack_id);
	sku_strlcpy(a->member_id, sku_cfg_get_or(&a->local, "member", ""), sizeof a->member_id);
	sku_strlcpy(a->pack_name, sku_cfg_get_or(&a->local, "pack_name", ""), sizeof a->pack_name);
	/* an active channel that no longer exists falls back to stable */
	if (strcmp(a->channel, "stable") != 0 && !channel_get(a, a->channel)) {
		sku_warn("%s: channel \"%s\" is not defined, using stable", a->dir, a->channel);
		sku_strlcpy(a->channel, "stable", sizeof a->channel);
		a->dirty = true;
	}
	sku_addon_reset_source(a);
}

void sku_addon_reset_source(sku_addon *a)
{
	const sku_channel *c = strcmp(a->channel, "stable") == 0 ? NULL : channel_get(a, a->channel);
	sku_protocol_base_url(a->base_url, sizeof a->base_url, c ? c->zone : a->zone, c ? c->module : a->module);
	a->fallback[0] = 0;
}

const sku_channel *sku_addon_fallback_channel(const sku_addon *a)
{
	for (int i = 0; i < a->nchannels; i++) if (a->channels[i].is_public) return &a->channels[i];   /* sorted by name */
	return NULL;
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

	/* the configuration is skunkcrafts_updater.cfg; an install converted by a 4.0 beta
	 * (skunkcrafts/updater.cfg, the root file removed) is read from there and migrated back */
	{
		char legacy[SKU_PATH_MAX];
		sku_path_join(p, sizeof p, a->dir, SKU_F_CFG);
		sku_path_join(legacy, sizeof legacy, a->dir, SKU_F_V4_CFG);
		a->legacy_v4 = sku_is_file(legacy);
		if (a->legacy_v4 && !sku_is_file(p)) { sku_strlcpy(p, legacy, sizeof p); sku_info("%s: 4.0-beta layout found, migrating back to %s", a->dir, SKU_F_CFG); }
		else if (a->legacy_v4) sku_info("%s: leftover %s ignored, %s is the configuration", a->dir, SKU_F_V4_CFG, SKU_F_CFG);
	}
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
	if (a->genre == SKU_GENRE_AIRCRAFT) sku_cfg_set_default(&a->local, "liveries", "true");

	sku_path_join(p, sizeof p, a->dir, SKU_F_IGNORE);
	load_ignore(a, p);
	sku_path_join(p, sizeof p, a->dir, SKU_F_V4_IGNORE);
	if (sku_is_file(p)) { load_ignore(a, p); a->legacy_v4 = true; }   /* folded into the 3.x file at the next save */
	if (a->legacy_v4) a->dirty = true;
	/* channels: the private folder, then the keys of 4.0.0-4.0.2 still in the public file,
	 * then the tester files (skunkcrafts_updater_beta*.cfg) dropped into the addon */
	{
		struct beta_scan s = { a };
		bool active_known = channels_load(a);
		sku_strlcpy(a->zone, sku_cfg_get_or(&a->local, "zone", "custom"), sizeof a->zone);     /* main depot, for the comparisons */
		sku_strlcpy(a->module, sku_cfg_get_or(&a->local, "module", ""), sizeof a->module);
		channels_from_root_cfg(a, active_known);
		sku_list_dir(a->dir, beta_cb, &s);
		/* the public channels a developer's configuration declares, read from the local copy
		 * too: a product whose main depot is still empty can be handed out as that one file */
		sku_strlcpy(a->name, sku_cfg_get_or(&a->local, "name", ""), sizeof a->name);
		sku_addon_public_channels(a, &a->local, false, a->inst->read_only);
		qsort(a->channels, (size_t)a->nchannels, sizeof *a->channels, channel_cmp);
		fields_from_cfg(a);
		/* allowed to write: absorb beta files and the legacy layout now rather than at the next save */
		if (a->dirty && !a->inst->read_only) sku_addon_save(a);
	}
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

struct absorb { sku_addon *a; };

/* Beta files were merged into the configuration on load: remove them. */
static int absorb_cb(const char *name, bool is_dir, bool is_symlink, void *user)
{
	struct absorb *c = user;
	char file[SKU_PATH_MAX];
	(void)is_symlink;
	if (is_dir) return 0;
	if (strncmp(name, "skunkcrafts_updater_beta", 24) == 0 && sku_str_ends(name, ".cfg")) {
		sku_path_join(file, sizeof file, c->a->dir, name);
		if (sku_remove_file(file) == SKU_OK) sku_info("%s: absorbed %s", c->a->dir, name);
	}
	return 0;
}

/* Folds the lines of one ignore list into another (union, order kept), then removes the source. */
static void fold_ignore_file(const sku_addon *a, const char *from, const char *into)
{
	char *text;
	sku_strlist lines = {0}, all = {0};
	size_t cap = 1024, n = 0;
	char *buf;
	if (sku_read_file(into, &text, NULL) == SKU_OK) {
		sku_split_lines(text, &lines);
		for (int i = 0; i < lines.n; i++) if (*sku_str_trim(lines.v[i])) sku_strlist_push(&all, sku_str_trim(lines.v[i]));
		sku_strlist_free(&lines); free(text);
	}
	if (sku_read_file(from, &text, NULL) == SKU_OK) {
		sku_split_lines(text, &lines);
		for (int i = 0; i < lines.n; i++) {
			char *l = sku_str_trim(lines.v[i]);
			if (*l && !sku_strlist_has(&all, l)) sku_strlist_push(&all, l);
		}
		sku_strlist_free(&lines); free(text);
	}
	buf = sku_xmalloc(cap);
	for (int i = 0; i < all.n; i++) {
		size_t need = strlen(all.v[i]) + 2;
		if (n + need > cap) { while (n + need > cap) cap *= 2; buf = sku_xrealloc(buf, cap); }
		n += (size_t)sprintf(buf + n, "%s\n", all.v[i]);
	}
	if (sku_write_file_atomic(into, buf, n) == SKU_OK && sku_remove_file(from) == SKU_OK)
		sku_info("%s: %s folded into %s", a->dir, sku_path_basename(from), sku_path_basename(into));
	free(buf);
	sku_strlist_free(&all);
}

int sku_addon_save(sku_addon *a)
{
	char p[SKU_PATH_MAX], legacy[SKU_PATH_MAX];
	struct absorb c = { a };
	int rc;

	/* key|value lines only: 3.x clients, aircraft and publishing tools keep reading it */
	sku_path_join(p, sizeof p, a->dir, SKU_F_CFG);
	rc = sku_cfg_save_v1(&a->local, p);
	if (rc != SKU_OK) { sku_error("%s: cannot write %s", a->dir, p); return rc; }
	/* the channels first: a tester file is only absorbed once its channel is on disk */
	rc = channels_save(a);
	if (rc != SKU_OK) return rc;
	sku_list_dir(a->dir, absorb_cb, &c);
	if (a->legacy_v4) {
		/* the 4.0-beta layout: configuration now rewritten above, ignore list folded, files removed */
		sku_path_join(legacy, sizeof legacy, a->dir, SKU_F_V4_CFG);
		if (sku_is_file(legacy) && sku_remove_file(legacy) == SKU_OK) sku_info("%s: removed %s", a->dir, SKU_F_V4_CFG);
		sku_path_join(legacy, sizeof legacy, a->dir, SKU_F_V4_IGNORE);
		sku_path_join(p, sizeof p, a->dir, SKU_F_IGNORE);
		if (sku_is_file(legacy)) fold_ignore_file(a, legacy, p);
		a->legacy_v4 = false;
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
	sku_strlcpy(a->channel, channel, sizeof a->channel);
	sku_info("%s: channel = %s", a->dir, channel);
	return apply(a, dry_run);
}

/* A channel file is never deleted: set aside as <file>.<suffix> ("removed" by the user,
 * "withdrawn" by the developer; the previous one of that name is replaced), then the
 * channel leaves the list. */
static int channel_set_aside(sku_addon *a, sku_channel *c, const char *suffix, bool dry_run)
{
	int i;
	if (!dry_run && c->file[0]) {
		char from[SKU_PATH_MAX], to[SKU_PATH_MAX], rel[SKU_PATH_MAX];
		sku_snprintf(rel, sizeof rel, "%s/%s", SKU_D_CHANNELS, c->file);
		sku_path_join(from, sizeof from, a->dir, rel);
		sku_snprintf(to, sizeof to, "%s.%s", from, suffix);
		if (sku_is_file(from) && sku_rename(from, to) != SKU_OK) { sku_error("%s: cannot set %s aside", a->dir, from); return SKU_ERR_IO; }
	}
	if (strcmp(a->channel, c->name) == 0) sku_strlcpy(a->channel, "stable", sizeof a->channel);
	i = (int)(c - a->channels);
	memmove(&a->channels[i], &a->channels[i + 1], (size_t)(a->nchannels - i - 1) * sizeof *a->channels);
	a->nchannels--;
	return SKU_OK;
}

int sku_addon_remove_channel(sku_addon *a, const char *channel, bool dry_run)
{
	sku_channel *c = channel_get(a, channel);
	char module[SKU_URL_MAX];
	if (!c) return SKU_ERR_NOTFOUND;
	sku_strlcpy(module, c->module, sizeof module);
	if (channel_set_aside(a, c, "removed", dry_run) != SKU_OK) return SKU_ERR_IO;
	sku_info("%s: channel \"%s\" removed (%s)", a->dir, channel, module);
	return apply(a, dry_run);
}

/* A public channel name: "channel.<name>" with the spaces at both ends trimmed; not empty,
 * not one of the main channel's names, not the "channel.<n>.module" keys of 4.0.0-4.0.2. */
static bool public_channel_name(const char *key, char *name, size_t n)
{
	const char *p = key + 8, *e = key + strlen(key);
	while (*p == ' ' || *p == '\t') p++;
	while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
	if ((size_t)(e - p) >= n) return false;
	memcpy(name, p, (size_t)(e - p)); name[e - p] = 0;
	if (!valid_channel_name(name) || strcasecmp(name, "main") == 0) return false;
	if (sku_str_ends(name, ".module") || sku_str_ends(name, ".zone")) return false;
	return true;
}

void sku_addon_public_channels(sku_addon *a, const sku_cfg *remote, bool from_main, bool dry_run)
{
	sku_strlist names = {0};
	bool changed = false;
	for (int i = 0; i < remote->n; i++) {
		const char *k = remote->v[i].key, *v = remote->v[i].value;
		char name[64], url[SKU_URL_MAX], dir[SKU_PATH_MAX];
		sku_channel *c = NULL;
		if (!k || !v || strncmp(k, "channel.", 8) != 0) continue;
		if (!public_channel_name(k, name, sizeof name)) { sku_warn("%s: the depot declares the channel key \"%s\", not a usable channel name: ignored", a->name, k); continue; }
		sku_strlcpy(url, v, sizeof url);
		{
			char *t = sku_str_trim(url);
			if (t != url) memmove(url, t, strlen(t) + 1);
		}
		if (!*url || same_depot("custom", url, a->zone, a->module) || sku_strlist_has(&names, name)) continue;
		sku_strlist_push(&names, name);
		/* a channel already following that depot: a public one takes the declared name,
		 * a private one (a tester file) stays as it is */
		for (int j = 0; j < a->nchannels && !c; j++) if (same_depot(a->channels[j].zone, a->channels[j].module, "custom", url)) c = &a->channels[j];
		if (c) {
			if (c->is_public && strcmp(c->name, name) != 0 && !channel_get(a, name)) {
				sku_info("%s: public channel \"%s\" renamed \"%s\" by the depot", a->name, c->name, name);
				if (strcmp(a->channel, c->name) == 0) sku_strlcpy(a->channel, name, sizeof a->channel);
				sku_strlcpy(c->name, name, sizeof c->name);
				changed = true;
			}
			continue;
		}
		/* a public channel of that name moved by the developer */
		c = channel_get(a, name);
		if (c) {
			if (c->is_public) {
				sku_info("%s: public channel \"%s\" now follows %s", a->name, name, url);
				sku_strlcpy(c->module, url, sizeof c->module);
				sku_strlcpy(c->zone, "custom", sizeof c->zone);
				changed = true;
			} else sku_debug("%s: the depot declares the channel \"%s\", a private channel of that name exists: left as it is", a->name, name);
			continue;
		}
		/* new to this addon, unless the user removed it */
		{
			struct removed_scan rs;
			sku_path_join(dir, sizeof dir, a->dir, SKU_D_CHANNELS);
			rs.dir = dir; rs.zone = "custom"; rs.module = url; rs.found = false;
			if (sku_is_dir(dir)) sku_list_dir(dir, removed_cb, &rs);
			if (rs.found) { sku_debug("%s: public channel \"%s\" removed by the user: not offered again", a->name, name); continue; }
		}
		c = channel_put(a, name);
		sku_strlcpy(c->module, url, sizeof c->module);
		c->is_public = true;
		sku_info("%s: the depot offers the public channel \"%s\" (%s): added, not followed", a->name, name, url);
		sku_snprintf(a->notice, sizeof a->notice, "%s: new beta channel \"%s\" offered by the developer, pick it in the Channel column to follow it", a->name, name);
		changed = true;
	}
	/* withdrawn: only the main depot's cfg says which public channels exist; one still
	 * followed stays until the user goes back to Main */
	for (int j = a->nchannels - 1; from_main && j >= 0; j--) {
		sku_channel *c = &a->channels[j];
		if (!c->is_public || sku_strlist_has(&names, c->name) || strcmp(a->channel, c->name) == 0) continue;
		sku_info("%s: public channel \"%s\" withdrawn by the depot", a->name, c->name);
		channel_set_aside(a, c, "withdrawn", dry_run);
		changed = true;
	}
	sku_strlist_free(&names);
	if (!changed) return;
	qsort(a->channels, (size_t)a->nchannels, sizeof *a->channels, channel_cmp);
	if (!dry_run) channels_save(a);
}
