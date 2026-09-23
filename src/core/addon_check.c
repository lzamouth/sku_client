/* Addon: check for updates = fetch the remote state and build the plan. */
#include "addon.h"
#include "platform.h"
#include "paths.h"
#include "http.h"
#include "crc32.h"
#include "crc_cache.h"
#include "worker.h"
#include "strmap.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

/* Fetches a protocol text file; rejects HTML pages and truncated bodies. */
static int fetch_text(sku_addon *a, sku_http *h, const char *file, bool required, char **out, sku_http_result *r)
{
	char url[SKU_URL_MAX];
	size_t len = 0;
	int rc;
	sku_addon_encode_url(url, sizeof url, a->base_url, file);
	rc = sku_http_get_mem(h, url, NULL, out, &len, r);
	if (rc != SKU_OK) {
		if (required || rc == SKU_ERR_CANCELLED) sku_warn("%s: GET %s failed: %s", a->name, url, r->error);
		else sku_debug("%s: optional %s not available (%s)", a->name, file, r->error);
		return rc;
	}
	if (sku_http_looks_like_html(*out, len, r->content_type)) {
		free(*out); *out = NULL;
		sku_snprintf(r->error, sizeof r->error, "server returned a web page instead of %s", file);
		sku_warn("%s: GET %s: %s", a->name, url, r->error);
		return SKU_ERR_FORMAT;
	}
	return SKU_OK;
}

/* One CRC job: hash a local file and compare with the depot. */
struct crc_job {
	int      file_index;   /* into a->files */
	uint32_t local_crc;
	int64_t  size, mtime;
	int      rc;
};

struct crc_ctx {
	sku_addon *a;
	struct crc_job *jobs;
	sku_crc_cache *cache;
};

static void crc_worker(int job, int thread, void *user)
{
	struct crc_ctx *c = user;
	struct crc_job *j = &c->jobs[job];
	char full[SKU_PATH_MAX];
	(void)thread;
	sku_path_join(full, sizeof full, c->a->dir, c->a->files.v[j->file_index].path);
	j->rc = sku_crc32_file(full, &j->local_crc, &c->a->cancel);
	if (j->rc == SKU_OK && c->cache) sku_crc_cache_store(c->cache, c->a->files.v[j->file_index].path, j->size, j->mtime, j->local_crc);
	atomic_fetch_add(&c->a->prog_done, 1);
}

static void set_check_result(sku_addon *a)
{
	sku_plan *p = &a->plan;
	char text[256], detail[256], nice[32];
	bool same_version = strcmp(a->version, a->remote_version) == 0;
	sku_nice_size(nice, sizeof nice, p->bytes);
	if (p->n_error) {
		sku_snprintf(text, sizeof text, "%d file(s) could not be read", p->n_error);
		sku_addon_set_status(a, SKU_ST_ERROR_FILES, text, "See the report for the offending files.");
	} else if (p->n_new + p->n_changed + p->n_delete == 0) {
		if (same_version) sku_addon_set_status(a, SKU_ST_UP_TO_DATE, "All files are up to date", "");
		else {
			sku_snprintf(detail, sizeof detail, "Local: %s  Remote: %s. An update/repair fixes this.", a->version, a->remote_version);
			sku_addon_set_status(a, SKU_ST_VERSION_MISMATCH, "Files up to date but local and remote versions differ", detail);
		}
	} else if (same_version) {
		sku_snprintf(text, sizeof text, "%d file(s) missing or changed, %s to download", p->n_new + p->n_changed, nice);
		sku_addon_set_status(a, SKU_ST_REPAIR, text, "Recommended action: update/repair.");
	} else {
		sku_snprintf(text, sizeof text, "Version %s available: %d file(s), %s to download", a->remote_version, p->n_new + p->n_changed, nice);
		sku_addon_set_status(a, SKU_ST_UPDATE, text, "Recommended action: update.");
	}
}

/* Compares the remote list with the local files and fills the plan. */
static int build_plan(sku_addon *a, const sku_check_opts *opts)
{
	sku_strmap ignore;
	struct crc_ctx ctx = { a, NULL, NULL };
	int njobs = 0, rc = SKU_OK, nthreads = opts->crc_threads > 0 ? opts->crc_threads : sku_cpu_count();
	char cache_path[SKU_PATH_MAX];
	bool skip_liveries = a->genre == SKU_GENRE_AIRCRAFT && !a->liveries;

	sku_plan_clear(&a->plan);
	sku_strmap_init(&ignore, (size_t)a->ignore.n);
	for (int i = 0; i < a->ignore.n; i++) sku_strmap_put(&ignore, a->ignore.v[i]);

	sku_instance_cache_path(a->inst, a->dir, cache_path, sizeof cache_path);
	ctx.cache = sku_crc_cache_open(cache_path);
	ctx.jobs = sku_xcalloc((size_t)a->files.n + 1, sizeof *ctx.jobs);

	atomic_store(&a->prog_done, 0);
	atomic_store(&a->prog_total, a->files.n);

	/* pass 1: cheap decisions (stat only), collect what needs hashing */
	for (int i = 0; i < a->files.n; i++) {
		sku_remote_file *f = &a->files.v[i];
		char full[SKU_PATH_MAX];
		sku_fileinfo st;
		uint32_t cached;
		if (sku_strmap_get(&ignore, f->path)) { sku_plan_add(&a->plan, f->path, SKU_PLAN_IGNORED, f->size, f->crc, f->check_crc); atomic_fetch_add(&a->prog_done, 1); continue; }
		if (skip_liveries && sku_str_istarts(f->path, "liveries")) { a->plan.n_ignored++; atomic_fetch_add(&a->prog_done, 1); continue; }
		sku_path_join(full, sizeof full, a->dir, f->path);
		if (sku_stat(full, &st) != SKU_OK || !st.is_file) { sku_plan_add(&a->plan, f->path, SKU_PLAN_NEW, f->size, f->crc, f->check_crc); atomic_fetch_add(&a->prog_done, 1); continue; }
		if (!f->check_crc) { a->plan.n_unchanged++; atomic_fetch_add(&a->prog_done, 1); continue; }
		if (f->size >= 0 && st.size != f->size) { sku_plan_add(&a->plan, f->path, SKU_PLAN_CHANGED, f->size, f->crc, f->check_crc); atomic_fetch_add(&a->prog_done, 1); continue; }
		if (!opts->full && sku_crc_cache_lookup(ctx.cache, f->path, st.size, st.mtime, &cached)) {
			a->plan.n_cached++;
			if (cached == f->crc) a->plan.n_unchanged++;
			else sku_plan_add(&a->plan, f->path, SKU_PLAN_CHANGED, f->size, f->crc, f->check_crc);
			atomic_fetch_add(&a->prog_done, 1);
			continue;
		}
		ctx.jobs[njobs].file_index = i;
		ctx.jobs[njobs].size = st.size;
		ctx.jobs[njobs].mtime = st.mtime;
		njobs++;
	}
	sku_strmap_free(&ignore);

	/* pass 2: hash in parallel */
	if (njobs) {
		char text[128];
		sku_snprintf(text, sizeof text, "Checking %d file(s)", njobs);
		sku_addon_set_status(a, SKU_ST_CHECKING, text, "");
		sku_info("%s: hashing %d file(s) on %d thread(s)", a->name, njobs, nthreads);
		rc = sku_pool_run(nthreads, njobs, crc_worker, &ctx, &a->cancel);
	}
	for (int k = 0; k < njobs && rc == SKU_OK; k++) {
		struct crc_job *j = &ctx.jobs[k];
		sku_remote_file *f = &a->files.v[j->file_index];
		if (j->rc != SKU_OK) {
			sku_plan_item *it = sku_plan_add(&a->plan, f->path, SKU_PLAN_ERROR, f->size, f->crc, f->check_crc);
			it->result = j->rc; it->detail = sku_xstrdup(sku_err_str(j->rc));
			sku_error("%s: cannot hash %s: %s", a->name, f->path, sku_err_str(j->rc));
		} else if (j->local_crc == f->crc) a->plan.n_unchanged++;
		else sku_plan_add(&a->plan, f->path, SKU_PLAN_CHANGED, f->size, f->crc, f->check_crc);
	}

	/* blacklist: only files that exist */
	for (int i = 0; i < a->blacklist.n && rc == SKU_OK; i++) {
		char full[SKU_PATH_MAX];
		sku_path_join(full, sizeof full, a->dir, a->blacklist.v[i]);
		if (sku_is_file(full)) sku_plan_add(&a->plan, a->blacklist.v[i], SKU_PLAN_DELETE, -1, 0, false);
	}

	if (!opts->dry_run) sku_crc_cache_save(ctx.cache);
	sku_crc_cache_close(ctx.cache);
	free(ctx.jobs);
	return rc;
}

int sku_addon_check(sku_addon *a, const sku_check_opts *opts)
{
	sku_check_opts def = {0};
	sku_http *h;
	sku_http_result r;
	char *text = NULL, err[256];
	int rc, rejected = 0;
	int64_t t0 = sku_time_ms();

	if (!opts) opts = &def;
	atomic_store(&a->cancel, 0);
	a->checked = false;
	if (!a->dirty && (rc = sku_addon_reload(a)) != SKU_OK) return rc;   /* unsaved settings win over the disk */
	if (a->disabled) { sku_addon_set_status(a, SKU_ST_DISABLED, "Updates disabled by user", ""); return SKU_OK; }

	sku_addon_set_status(a, SKU_ST_CHECKING, "Contacting the update server", "");
	sku_info("%s: check (%s), source %s", a->name, opts->full ? "full" : "fast", a->base_url);
	h = sku_http_new();
	if (!h) return SKU_ERR_NET;

	/* remote cfg */
	sku_cfg_free(&a->remote);
	rc = fetch_text(a, h, SKU_F_CFG, true, &text, &r);
	if (rc != SKU_OK) {
		sku_addon_set_status(a, rc == SKU_ERR_CANCELLED ? SKU_ST_CANCELLED : SKU_ST_ERROR_NET, "Cannot retrieve the remote configuration", r.error);
		goto out;
	}
	sku_cfg_parse(&a->remote, text); free(text); text = NULL;
	if (sku_protocol_check_remote_cfg(&a->remote, err, sizeof err) != SKU_OK) {
		sku_addon_set_status(a, SKU_ST_ERROR_CONFIG, err, "Contact the addon developer.");
		rc = SKU_ERR_FORMAT;
		goto out;
	}
	sku_cfg_set_default(&a->remote, "zone", "custom");
	a->level = sku_protocol_level(&a->remote);
	sku_strlcpy(a->remote_version, sku_cfg_get(&a->remote, "version"), sizeof a->remote_version);
	sku_strlcpy(a->changelog, sku_cfg_get_or(&a->remote, "changelog", ""), sizeof a->changelog);
	if (*a->changelog && sku_path_check_relative(a->changelog) != SKU_OK) a->changelog[0] = 0;

	/* the depot may move: name/zone/module follow the remote (stable channel only) */
	if (strcmp(a->channel, "stable") == 0) {
		const char *rn = sku_cfg_get(&a->remote, "name"), *rz = sku_cfg_get(&a->remote, "zone"), *rm = sku_cfg_get(&a->remote, "module");
		if (strcmp(a->name, rn) != 0 || strcmp(a->zone, rz) != 0 || strcmp(a->module, rm) != 0) {
			sku_info("%s: remote identity changed (name/zone/module), following it", a->name);
			sku_cfg_set(&a->local, "name", rn);
			sku_cfg_set(&a->local, "zone", rz);
			sku_cfg_set(&a->local, "module", rm);
			a->dirty = true;
			if (!opts->dry_run) sku_addon_save(a);
			sku_addon_reload(a);
		}
	}

	if (sku_cfg_get_bool(&a->remote, "locked", false)) {
		sku_addon_set_status(a, SKU_ST_LOCKED, "Update repository locked for maintenance", "Updates are not possible until the developer removes the lock.");
		rc = SKU_OK;
		goto out;
	}

	/* file lists */
	sku_remote_files_free(&a->files);
	sku_strlist_free(&a->blacklist);
	if (a->level >= 2) {
		const char *mf = sku_cfg_get_or(&a->remote, "manifest", SKU_F_MANIFEST);
		rc = fetch_text(a, h, mf, true, &text, &r);
		if (rc != SKU_OK) { sku_addon_set_status(a, rc == SKU_ERR_CANCELLED ? SKU_ST_CANCELLED : SKU_ST_ERROR_NET, "Cannot retrieve the file manifest", r.error); goto out; }
		sku_protocol_parse_manifest(text, &a->files, &rejected); free(text); text = NULL;
	} else {
		rc = fetch_text(a, h, SKU_F_WHITELIST, true, &text, &r);
		if (rc != SKU_OK) { sku_addon_set_status(a, rc == SKU_ERR_CANCELLED ? SKU_ST_CANCELLED : SKU_ST_ERROR_NET, "Cannot retrieve the remote file list", r.error); goto out; }
		sku_protocol_parse_whitelist(text, &a->files, &rejected); free(text); text = NULL;
		if (fetch_text(a, h, SKU_F_SIZES, false, &text, &r) == SKU_OK) { sku_protocol_apply_sizes(text, &a->files); free(text); text = NULL; }
	}
	if (fetch_text(a, h, SKU_F_BLACKLIST, false, &text, &r) == SKU_OK) {
		int rej = 0;
		sku_protocol_parse_pathlist(text, &a->blacklist, &rej); free(text); text = NULL;
		rejected += rej;
	}
	if (rejected) sku_warn("%s: %d unsafe or malformed line(s) rejected from the remote lists", a->name, rejected);
	sku_info("%s: remote version %s, protocol %d, %d file(s), %d blacklisted", a->name, a->remote_version, a->level, a->files.n, a->blacklist.n);

	rc = build_plan(a, opts);
	a->plan.duration_ms = sku_time_ms() - t0;
	if (rc == SKU_ERR_CANCELLED) { sku_addon_set_status(a, SKU_ST_CANCELLED, "Check cancelled", ""); goto out; }
	a->checked = true;
	set_check_result(a);
	sku_info("%s: check done in %lld ms: %d new, %d changed, %d to delete, %d unchanged (%d from cache), %d ignored, %d errors",
		a->name, (long long)a->plan.duration_ms, a->plan.n_new, a->plan.n_changed, a->plan.n_delete, a->plan.n_unchanged, a->plan.n_cached, a->plan.n_ignored, a->plan.n_error);
out:
	free(text);
	sku_http_free(h);
	return rc;
}
