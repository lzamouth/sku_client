/* Addon: execute the plan = delete blacklisted files, download the rest. */
#include "addon.h"
#include "platform.h"
#include "paths.h"
#include "http.h"
#include "crc32.h"
#include "crc_cache.h"
#include "worker.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

struct dl_ctx {
	sku_addon *a;
	sku_http **handles;      /* one per thread */
	sku_crc_cache *cache;
	int retries;
	atomic_int errors;
};

struct dl_progress { sku_addon *a; int64_t last; };

static int on_progress(void *user, int64_t got, int64_t total)
{
	struct dl_progress *p = user;
	(void)total;
	if (got > p->last) { atomic_fetch_add(&p->a->bytes_done, got - p->last); p->last = got; }
	return 0;
}

static void fail(struct dl_ctx *c, sku_plan_item *it, int rc, const char *detail)
{
	it->result = rc;
	free(it->detail);
	it->detail = sku_xstrdup(detail);
	atomic_fetch_add(&c->errors, 1);
	sku_error("%s: %s %s: %s", c->a->name, sku_reason_name(it->reason), it->path, detail);
}

static void dl_worker(int job, int thread, void *user)
{
	struct dl_ctx *c = user;
	sku_addon *a = c->a;
	sku_plan_item *it = &a->plan.v[job];
	char full[SKU_PATH_MAX], url[SKU_URL_MAX];
	sku_http_result r;
	int rc = SKU_ERR_NET;

	if (it->reason != SKU_PLAN_NEW && it->reason != SKU_PLAN_CHANGED) return;
	sku_path_join(full, sizeof full, a->dir, it->path);
	sku_addon_encode_url(url, sizeof url, a->base_url, it->path);

	for (int attempt = 0; attempt <= c->retries; attempt++) {
		struct dl_progress p = { a, 0 };
		uint32_t crc;
		if (atomic_load(&a->cancel)) { it->result = SKU_ERR_CANCELLED; return; }
		if (attempt) { sku_warn("%s: retrying %s (%d/%d)", a->name, it->path, attempt, c->retries); sku_sleep_ms(1000 * attempt); }
		rc = sku_http_get_file(c->handles[thread], url, full, it->size, on_progress, &p, &a->cancel, &r);
		if (rc == SKU_ERR_CANCELLED) { it->result = rc; return; }
		if (rc != SKU_OK) { atomic_fetch_sub(&a->bytes_done, p.last); continue; }
		if (it->check_crc) {
			if (sku_crc32_file(full, &crc, NULL) != SKU_OK || crc != it->crc) {
				sku_snprintf(r.error, sizeof r.error, "CRC mismatch after download (got %lu, expected %lu)", (unsigned long)crc, (unsigned long)it->crc);
				sku_remove_file(full);
				atomic_fetch_sub(&a->bytes_done, p.last);
				rc = SKU_ERR_NET;
				continue;
			}
			{
				sku_fileinfo st;
				if (sku_stat(full, &st) == SKU_OK) sku_crc_cache_store(c->cache, it->path, st.size, st.mtime, crc);
			}
		}
		it->result = SKU_OK;
		atomic_fetch_add(&a->prog_done, 1);
		sku_debug("%s: downloaded %s (%lld bytes)", a->name, it->path, (long long)r.bytes);
		return;
	}
	fail(c, it, rc, r.error);
	atomic_fetch_add(&a->prog_done, 1);
}

int sku_addon_update(sku_addon *a, const sku_update_opts *opts)
{
	sku_update_opts def = {0};
	struct dl_ctx c = { a, NULL, NULL, 2, 0 };
	int nthreads, ndl = 0, rc = SKU_OK, deleted = 0;
	char cache_path[SKU_PATH_MAX], text[256];
	int64_t t0 = sku_time_ms();

	if (!opts) opts = &def;
	if (!a->checked) {
		sku_check_opts co = { false, opts->dry_run, 0 };
		rc = sku_addon_check(a, &co);
		if (rc != SKU_OK || !a->checked) return rc != SKU_OK ? rc : SKU_ERR_NET;
	}
	if (a->disabled) return SKU_OK;
	nthreads = opts->download_threads > 0 ? opts->download_threads : 8;
	if (opts->retries > 0) c.retries = opts->retries;
	atomic_init(&c.errors, 0);
	atomic_store(&a->cancel, 0);

	for (int i = 0; i < a->plan.n; i++) if (a->plan.v[i].reason == SKU_PLAN_NEW || a->plan.v[i].reason == SKU_PLAN_CHANGED) ndl++;
	if (ndl + a->plan.n_delete == 0 && strcmp(a->version, a->remote_version) == 0 && !a->dirty) {
		sku_addon_set_status(a, SKU_ST_UP_TO_DATE, "All files are up to date", "");
		return SKU_OK;
	}
	if (opts->dry_run) {
		sku_snprintf(text, sizeof text, "Dry run: %d file(s) would be downloaded, %d deleted", ndl, a->plan.n_delete);
		sku_addon_set_status(a, a->status, text, "");
		sku_info("%s: %s", a->name, text);
		return SKU_OK;
	}

	sku_info("%s: update %s -> %s: %d download(s) on %d thread(s), %d deletion(s)", a->name, a->version, a->remote_version, ndl, nthreads, a->plan.n_delete);
	atomic_store(&a->prog_done, 0);
	atomic_store(&a->prog_total, ndl);
	atomic_store(&a->bytes_done, 0);
	atomic_store(&a->bytes_total, a->plan.bytes);
	sku_snprintf(text, sizeof text, "Updating: %d file(s) to download", ndl);
	sku_addon_set_status(a, SKU_ST_UPDATING, text, "");

	/* deletions first (blacklist), as 3.x does */
	for (int i = 0; i < a->plan.n; i++) {
		sku_plan_item *it = &a->plan.v[i];
		char full[SKU_PATH_MAX];
		if (it->reason != SKU_PLAN_DELETE) continue;
		sku_path_join(full, sizeof full, a->dir, it->path);
		it->result = sku_remove_file(full);
		if (it->result == SKU_OK || it->result == SKU_ERR_NOTFOUND) { it->result = SKU_OK; deleted++; sku_info("%s: deleted %s", a->name, it->path); }
		else fail(&c, it, it->result, "cannot delete");
	}

	/* downloads */
	sku_instance_cache_path(a->inst, a->dir, cache_path, sizeof cache_path);
	c.cache = sku_crc_cache_open(cache_path);
	if (ndl) {
		if (nthreads > ndl) nthreads = ndl;
		c.handles = sku_xcalloc((size_t)nthreads, sizeof *c.handles);
		for (int i = 0; i < nthreads; i++) c.handles[i] = sku_http_new();
		rc = sku_pool_run(nthreads, a->plan.n, dl_worker, &c, &a->cancel);
		for (int i = 0; i < nthreads; i++) sku_http_free(c.handles[i]);
		free(c.handles);
	}
	sku_crc_cache_save(c.cache, false);
	sku_crc_cache_close(c.cache);
	a->plan.duration_ms = sku_time_ms() - t0;

	if (rc == SKU_ERR_CANCELLED) {
		sku_addon_set_status(a, SKU_ST_CANCELLED, "Update cancelled", "Files already downloaded are kept; run a check to see what remains.");
		sku_warn("%s: update cancelled by user", a->name);
		a->checked = false;
		return rc;
	}
	if (atomic_load(&c.errors) > 0) {
		sku_snprintf(text, sizeof text, "%d file(s) failed, see the report", atomic_load(&c.errors));
		sku_addon_set_status(a, SKU_ST_ERROR_FILES, text, "The addon may be unusable until fully updated. Run the update again.");
		a->checked = false;
		return SKU_ERR_NET;
	}

	/* everything landed: adopt the remote identity and version */
	sku_cfg_set(&a->local, "name",    sku_cfg_get(&a->remote, "name"));
	sku_cfg_set(&a->local, "version", a->remote_version);
	if (strcmp(a->channel, "stable") == 0) {
		sku_cfg_set(&a->local, "zone",   sku_cfg_get(&a->remote, "zone"));
		sku_cfg_set(&a->local, "module", sku_cfg_get(&a->remote, "module"));
	}
	rc = sku_addon_save(a);
	sku_snprintf(text, sizeof text, "Updated to %s: %d file(s) downloaded, %d deleted", a->remote_version, ndl, deleted);
	sku_addon_set_status(a, rc == SKU_OK ? SKU_ST_UPDATED : SKU_ST_ERROR_CONFIG, rc == SKU_OK ? text : "Files updated but the configuration could not be saved", "");
	sku_info("%s: %s in %lld ms", a->name, text, (long long)a->plan.duration_ms);
	sku_addon_reload(a);
	sku_addon_set_status(a, rc == SKU_OK ? SKU_ST_UPDATED : SKU_ST_ERROR_CONFIG, rc == SKU_OK ? text : "Files updated but the configuration could not be saved", "");
	a->checked = false;
	return rc;
}
