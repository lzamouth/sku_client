#include "protocol.h"
#include "paths.h"
#include "strmap.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

/* "skunkcrafts/..." belongs to the client (config, CRC cache): a depot may not publish or delete it. */
static bool private_path(const char *rel)
{
	return sku_str_istarts(rel, SKU_DIR_V4 "/");
}

int sku_protocol_client_file(const char *rel)
{
	size_t n = strlen(rel);
	if (strchr(rel, '/') || strchr(rel, '\\')) return 0;
	if (strcasecmp(rel, SKU_F_CFG) == 0 || strcasecmp(rel, SKU_F_IGNORE) == 0) return 1;
	if (sku_str_istarts(rel, "skunkcrafts_updater_beta") && n > 4 && strcasecmp(rel + n - 4, ".cfg") == 0) return 2;
	return 0;
}


void sku_remote_files_free(sku_remote_files *f)
{
	for (int i = 0; i < f->n; i++) free(f->v[i].path);
	free(f->v);
	memset(f, 0, sizeof *f);
}

static sku_remote_file *push(sku_remote_files *f)
{
	if (f->n == f->cap) {
		f->cap = f->cap ? f->cap * 2 : 256;
		f->v = sku_xrealloc(f->v, (size_t)f->cap * sizeof *f->v);
	}
	memset(&f->v[f->n], 0, sizeof f->v[f->n]);
	return &f->v[f->n++];
}

/* "crc" field: decimal, unsigned 32-bit, or -1 meaning "do not check". */
static bool parse_crc(const char *s, uint32_t *crc, bool *check)
{
	char *end;
	long long v;
	errno = 0;
	v = strtoll(s, &end, 10);
	if (errno || end == s || *end) return false;
	if (v == -1) { *check = false; *crc = 0; return true; }
	if (v < 0 || v > 0xFFFFFFFFLL) return false;
	*check = true;
	*crc = (uint32_t)v;
	return true;
}

static bool parse_size(const char *s, int64_t *size)
{
	char *end;
	long long v;
	errno = 0;
	v = strtoll(s, &end, 10);
	if (errno || end == s || *end || v < -1) return false;
	*size = v;
	return true;
}

/* Splits "a|b|c" in place into at most max fields; returns the count. */
static int split_fields(char *line, char **fields, int max)
{
	int n = 0;
	char *p = line;
	while (n < max) {
		char *sep = strchr(p, '|');
		fields[n++] = p;
		if (!sep) break;
		*sep = 0;
		p = sep + 1;
	}
	return n;
}

typedef int (*line_fn)(char **fields, int nfields, void *user);

/* Runs fn on every non-empty, non-comment line. Rejections are counted by the callers. */
static int for_each_line(const char *text, int max_fields, line_fn fn, void *user)
{
	char *copy = sku_xstrdup(text), *cursor = sku_skip_bom(copy), *p;
	int rc = SKU_OK;
	while ((p = sku_next_line(&cursor)) != NULL) {
		char *fields[8];
		int nf;
		p = sku_str_trim(p);
		if (*p && *p != '#') {
			nf = split_fields(p, fields, max_fields);
			if (fn(fields, nf, user) != SKU_OK) rc = SKU_ERR_FORMAT;
		}
	}
	free(copy);
	return rc;
}

struct list_ctx { sku_remote_files *files; int rejected; };

static int whitelist_line(char **f, int nf, void *user)
{
	struct list_ctx *c = user;
	sku_remote_file *rf;
	uint32_t crc; bool check;
	if (nf != 2 || sku_path_check_relative(f[0]) != SKU_OK || private_path(f[0]) || !parse_crc(f[1], &crc, &check)) {
		c->rejected++;
		sku_warn("whitelist: rejected line \"%s\"", f[0]);
		return SKU_ERR_FORMAT;
	}
	rf = push(c->files);
	rf->path = sku_xstrdup(f[0]);
	rf->crc = crc; rf->check_crc = check; rf->size = -1;
	return SKU_OK;
}

int sku_protocol_parse_whitelist(const char *text, sku_remote_files *out, int *rejected)
{
	struct list_ctx c = { out, 0 };
	for_each_line(text, 2, whitelist_line, &c);
	if (rejected) *rejected = c.rejected;
	return SKU_OK;
}

static int manifest_line(char **f, int nf, void *user)
{
	struct list_ctx *c = user;
	sku_remote_file *rf;
	uint32_t crc; bool check; int64_t size;
	/* extra fields after size are reserved for later protocol levels and ignored */
	if (nf < 3 || sku_path_check_relative(f[0]) != SKU_OK || private_path(f[0]) || !parse_crc(f[1], &crc, &check) || !parse_size(f[2], &size)) {
		c->rejected++;
		sku_warn("manifest: rejected line \"%s\"", f[0]);
		return SKU_ERR_FORMAT;
	}
	rf = push(c->files);
	rf->path = sku_xstrdup(f[0]);
	rf->crc = crc; rf->check_crc = check; rf->size = size;
	return SKU_OK;
}

int sku_protocol_parse_manifest(const char *text, sku_remote_files *out, int *rejected)
{
	struct list_ctx c = { out, 0 };
	for_each_line(text, 4, manifest_line, &c);
	if (rejected) *rejected = c.rejected;
	return SKU_OK;
}

struct sizes_ctx { sku_strmap map; };

static int sizes_line(char **f, int nf, void *user)
{
	struct sizes_ctx *c = user;
	int64_t size;
	sku_strmap_node *n;
	if (nf != 2 || sku_path_check_relative(f[0]) != SKU_OK || !parse_size(f[1], &size)) return SKU_ERR_FORMAT;
	n = sku_strmap_put(&c->map, f[0]);
	n->num = size;
	return SKU_OK;
}

int sku_protocol_apply_sizes(const char *text, sku_remote_files *files)
{
	struct sizes_ctx c;
	sku_strmap_init(&c.map, (size_t)files->n);
	for_each_line(text, 2, sizes_line, &c);
	for (int i = 0; i < files->n; i++) {
		sku_strmap_node *n = sku_strmap_get(&c.map, files->v[i].path);
		if (n) files->v[i].size = n->num;
	}
	sku_strmap_free(&c.map);
	return SKU_OK;
}

struct paths_ctx { sku_strlist *out; int rejected; };

static int path_line(char **f, int nf, void *user)
{
	struct paths_ctx *c = user;
	(void)nf;
	if (sku_path_check_relative(f[0]) != SKU_OK || private_path(f[0])) {
		c->rejected++;
		sku_warn("path list: rejected line \"%s\"", f[0]);
		return SKU_ERR_FORMAT;
	}
	sku_strlist_push(c->out, f[0]);
	return SKU_OK;
}

int sku_protocol_parse_pathlist(const char *text, sku_strlist *out, int *rejected)
{
	struct paths_ctx c = { out, 0 };
	for_each_line(text, 1, path_line, &c);
	if (rejected) *rejected = c.rejected;
	return SKU_OK;
}

/* Replaces every occurrence of from in s (capacity n) by to. */
static void replace_all(char *s, size_t n, const char *from, const char *to)
{
	char tmp[SKU_URL_MAX * 2];
	size_t lf = strlen(from), k = 0;
	const char *p = s, *hit;
	if (!strstr(s, from)) return;
	tmp[0] = 0;
	while ((hit = strstr(p, from)) != NULL) {
		k += sku_snprintf(tmp + k, sizeof tmp - k, "%.*s%s", (int)(hit - p), p, to);
		if (k >= sizeof tmp) return;
		p = hit + lf;
	}
	sku_snprintf(tmp + k, sizeof tmp - k, "%s", p);
	sku_strlcpy(s, tmp, n);
}

void sku_protocol_base_url(char *out, size_t n, const char *zone, const char *module)
{
	char m[SKU_URL_MAX];
	size_t k;
	/* Thranda's former servers, still named in installed aircraft: rewritten as
	 * 3.x does (addon.go, select_data_source); the old addresses no longer answer */
	sku_strlcpy(m, module ? module : "", sizeof m);
	replace_all(m, sizeof m, "72.2.11.3:8080", "updates.thranda.org");
	replace_all(m, sizeof m, "72.2.11.34:8080", "updates.thranda.org");
	if (!zone || !*zone || strcmp(zone, "custom") == 0) sku_strlcpy(out, m, n);
	else sku_snprintf(out, n, "http://%s-ccd.kxcdn.com/%s", zone, m);
	k = strlen(out);
	while (k && out[k - 1] == '/') out[--k] = 0;
	sku_strlcat(out, "/", n);
}

int sku_protocol_check_remote_cfg(const sku_cfg *remote, char *err, size_t n)
{
	/* "locked" is optional: a depot published without it is unlocked (xp_depot --no-lock) */
	static const char *required[] = { "name", "module", "version" };
	for (size_t i = 0; i < sizeof required / sizeof *required; i++) {
		if (!sku_cfg_has(remote, required[i])) {
			sku_snprintf(err, n, "remote configuration is missing \"%s\"", required[i]);
			return SKU_ERR_FORMAT;
		}
	}
	return SKU_OK;
}

int sku_protocol_level(const sku_cfg *remote)
{
	const char *p = sku_cfg_get(remote, "protocol");
	int level = p ? atoi(p) : 1;
	return level < 1 ? 1 : level;
}
