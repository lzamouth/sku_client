/* The updater's own update: see client.h. */
#include "client.h"
#include "platform.h"
#include "paths.h"
#include "protocol.h"
#include "addon.h"
#include "crc32.h"
#include "zip.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------ versions */

/* "4.0.12-beta3" -> num = {4, 0, 12}, letters = "beta", tail = 3 */
struct version {
	long num[4];
	int  nnum;
	char letters[32];
	long tail;
	bool has_suffix;
};

static void parse_version(const char *s, struct version *v)
{
	const char *p = s;
	memset(v, 0, sizeof *v);
	while (*p == ' ' || *p == '\t') p++;
	if ((*p == 'v' || *p == 'V') && isdigit((unsigned char)p[1])) p++;
	while (v->nnum < 4 && isdigit((unsigned char)*p)) {
		char *end;
		v->num[v->nnum++] = strtol(p, &end, 10);
		p = end;
		if (*p == '.' && isdigit((unsigned char)p[1])) p++; else break;
	}
	if (*p == '-' || *p == '_' || *p == ' ' || isalpha((unsigned char)*p)) {
		size_t k = 0;
		if (!isalpha((unsigned char)*p)) p++;
		v->has_suffix = *p != 0;
		while (isalpha((unsigned char)*p) || *p == '.' || *p == '-') {
			if (k < sizeof v->letters - 1 && isalpha((unsigned char)*p)) v->letters[k++] = (char)tolower((unsigned char)*p);
			p++;
		}
		v->letters[k] = 0;
		if (isdigit((unsigned char)*p)) v->tail = strtol(p, NULL, 10);
	}
}

int sku_client_version_cmp(const char *a, const char *b)
{
	struct version va, vb;
	int c;
	parse_version(a, &va);
	parse_version(b, &vb);
	for (int i = 0; i < 4; i++) {
		if (va.num[i] < vb.num[i]) return -1;
		if (va.num[i] > vb.num[i]) return 1;
	}
	if (va.has_suffix != vb.has_suffix) return va.has_suffix ? -1 : 1;   /* "4.0.0" > "4.0.0-beta2" */
	if (!va.has_suffix) return 0;
	c = strcmp(va.letters, vb.letters);                                   /* alpha < beta < rc */
	if (c) return c < 0 ? -1 : 1;
	return va.tail < vb.tail ? -1 : va.tail > vb.tail ? 1 : 0;
}

/* ------------------------------------------------------------ platform */

const char *sku_client_package(void)
{
	const char *os = sku_os_name();
	if (strcmp(os, "windows") == 0) {
		char exe[SKU_PATH_MAX], dir[SKU_PATH_MAX], p[SKU_PATH_MAX];
		if (sku_exe_path(exe, sizeof exe) == SKU_OK) {
			sku_path_dirname(dir, sizeof dir, exe);
			sku_path_join(p, sizeof p, dir, "Uninstall.exe");
			if (sku_is_file(p)) return SKU_PKG_WINDOWS_SETUP;
		}
		return SKU_PKG_WINDOWS_ZIP;
	}
	if (strcmp(os, "macos") == 0) return SKU_PKG_MACOS_ZIP;
	{
		const char *ai = getenv("APPIMAGE");
		if (ai && *ai) return SKU_PKG_LINUX_APPIMAGE;
	}
	return SKU_PKG_LINUX_ZIP;
}

void sku_client_versioned_name(const char *package, const char *version, char *out, size_t n)
{
	static const char prefix[] = "SkunkcraftsUpdater-";
	if (strncmp(package, prefix, sizeof prefix - 1) == 0) sku_snprintf(out, n, "%s%s-%s", prefix, version, package + sizeof prefix - 1);
	else if (strcmp(package, SKU_PKG_CLI_ZIP) == 0) sku_snprintf(out, n, "sku-cli-%s.zip", version);
	else sku_strlcpy(out, package, n);
}

/* ------------------------------------------------------------ depot */

static void base_url(char *out, size_t n, const char *depot_url)
{
	size_t k;
	sku_strlcpy(out, depot_url, n);
	k = strlen(out);
	while (k && out[k - 1] == '/') out[--k] = 0;
	sku_strlcat(out, "/", n);
}

/* GET a protocol text file; HTML pages are refused. */
static int get_text(sku_http *h, const char *base, const char *file, const char *qkey, const char *qvalue, char **out, sku_http_result *r)
{
	char url[SKU_URL_MAX];
	size_t len = 0;
	int rc;
	sku_addon_encode_url(url, sizeof url, base, file);
	if (qkey) sku_url_add_query(url, sizeof url, qkey, qvalue);   /* see sku_url_add_query: no stale CDN copy */
	rc = sku_http_get_mem(h, url, NULL, NULL, out, &len, r);
	if (rc != SKU_OK) return rc;
	if (sku_http_looks_like_html(*out, len, r->content_type)) {
		free(*out); *out = NULL;
		sku_snprintf(r->error, sizeof r->error, "server returned a web page instead of %s", file);
		return SKU_ERR_FORMAT;
	}
	return SKU_OK;
}

int sku_client_check(const char *depot_url, const char *package, sku_client_info *out, char *err, size_t n)
{
	char base[SKU_URL_MAX], *text = NULL;
	sku_http *h;
	sku_http_result r;
	sku_cfg cfg;
	sku_remote_files files = {0};
	int rc, rejected = 0, level;

	memset(out, 0, sizeof *out);
	out->size = -1;
	if (err && n) err[0] = 0;
	if (!package) package = sku_client_package();
	base_url(base, sizeof base, depot_url);
	sku_info("client: checking %s for a newer version than %s (package %s)", base, SKU_VERSION, package);

	h = sku_http_new();
	if (!h) { sku_snprintf(err, n, "cannot create an HTTP handle"); return SKU_ERR_NET; }
	sku_cfg_init(&cfg);

	{
		char now[32];
		sku_snprintf(now, sizeof now, "%lld", (long long)sku_time_now());
		rc = get_text(h, base, SKU_F_CFG, "t", now, &text, &r);
	}
	if (rc != SKU_OK) { sku_snprintf(err, n, "%s", r.error); goto out; }
	sku_cfg_parse(&cfg, text); free(text); text = NULL;
	if (sku_protocol_check_remote_cfg(&cfg, err, n) != SKU_OK) { rc = SKU_ERR_FORMAT; goto out; }
	sku_cfg_set_default(&cfg, "zone", "custom");
	sku_strlcpy(out->version, sku_cfg_get(&cfg, "version"), sizeof out->version);
	sku_protocol_base_url(out->module, sizeof out->module, sku_cfg_get(&cfg, "zone"), sku_cfg_get(&cfg, "module"));
	out->newer = sku_client_version_cmp(out->version, SKU_VERSION) > 0;
	out->locked = sku_cfg_get_bool(&cfg, "locked", false);
	level = sku_protocol_level(&cfg);

	/* the package entry: crc and size */
	if (level >= 2) {
		rc = get_text(h, base, sku_cfg_get_or(&cfg, "manifest", SKU_F_MANIFEST), "v", out->version, &text, &r);
		if (rc != SKU_OK) { sku_snprintf(err, n, "%s", r.error); goto out; }
		sku_protocol_parse_manifest(text, &files, &rejected); free(text); text = NULL;
	} else {
		rc = get_text(h, base, SKU_F_WHITELIST, "v", out->version, &text, &r);
		if (rc != SKU_OK) { sku_snprintf(err, n, "%s", r.error); goto out; }
		sku_protocol_parse_whitelist(text, &files, &rejected); free(text); text = NULL;
		if (get_text(h, base, SKU_F_SIZES, "v", out->version, &text, &r) == SKU_OK) { sku_protocol_apply_sizes(text, &files); free(text); text = NULL; }
	}
	{
		/* the versioned name first, the plain one for older depots */
		char versioned[128];
		const char *candidates[2] = { versioned, package };
		sku_client_versioned_name(package, out->version, versioned, sizeof versioned);
		for (int c = 0; c < 2 && !out->package[0]; c++)
			for (int i = 0; i < files.n; i++) {
				if (strcmp(files.v[i].path, candidates[c]) != 0) continue;
				sku_strlcpy(out->package, candidates[c], sizeof out->package);
				out->crc = files.v[i].crc;
				out->check_crc = files.v[i].check_crc;
				out->size = files.v[i].size;
				break;
			}
		if (!out->package[0]) sku_warn("client: the depot publishes neither %s nor %s", versioned, package);
	}

	/* what's new: declared file, else the default name (check_relative normalizes in place: copy first) */
	{
		char cl[SKU_PATH_MAX];
		sku_strlcpy(cl, sku_cfg_get_or(&cfg, "changelog", SKU_F_CHANGELOG), sizeof cl);
		if (sku_path_check_relative(cl) == SKU_OK && get_text(h, base, cl, "v", out->version, &text, &r) == SKU_OK) { out->changelog = text; text = NULL; }
	}
	sku_info("client: published %s (%s), package %s%s", out->version, out->newer ? "newer" : "not newer",
		out->package[0] ? out->package : "none", out->locked ? ", depot locked" : "");
	rc = SKU_OK;
out:
	if (rc != SKU_OK) sku_warn("client: check failed: %s", err && err[0] ? err : sku_err_str(rc));
	free(text);
	sku_remote_files_free(&files);
	sku_cfg_free(&cfg);
	sku_http_free(h);
	return rc;
}

int sku_client_download(const char *depot_url, const sku_client_info *info, const char *dest_dir,
                        sku_progress_cb cb, void *user, atomic_int *cancel, char *dest, size_t n, char *err, size_t en)
{
	char base[SKU_URL_MAX], url[SKU_URL_MAX];
	sku_http *h;
	sku_http_result r;
	int rc;

	if (err && en) err[0] = 0;
	if (!info->package[0]) { sku_snprintf(err, en, "the depot publishes no package for this platform"); return SKU_ERR_NOTFOUND; }
	if (sku_mkdir_p(dest_dir) != SKU_OK) { sku_snprintf(err, en, "cannot create %s", dest_dir); return SKU_ERR_IO; }
	base_url(base, sizeof base, depot_url);
	sku_addon_encode_url(url, sizeof url, base, info->package);
	sku_path_join(dest, n, dest_dir, info->package);
	sku_info("client: downloading %s to %s", url, dest);

	h = sku_http_new();
	if (!h) { sku_snprintf(err, en, "cannot create an HTTP handle"); return SKU_ERR_NET; }
	rc = sku_http_get_file(h, url, dest, info->size, cb, user, cancel, &r);
	sku_http_free(h);
	if (rc != SKU_OK) { sku_snprintf(err, en, "%s", rc == SKU_ERR_CANCELLED ? "download cancelled" : r.error); return rc; }
	if (info->check_crc) {
		uint32_t crc;
		if (sku_crc32_file(dest, &crc, NULL) != SKU_OK || crc != info->crc) {
			sku_remove_file(dest);
			sku_snprintf(err, en, "CRC mismatch after download (got %lu, expected %lu)", (unsigned long)crc, (unsigned long)info->crc);
			sku_error("client: %s", err);
			return SKU_ERR_FORMAT;
		}
	}
	sku_info("client: %s downloaded and verified (%lld bytes)", info->package, (long long)r.bytes);
	return SKU_OK;
}

int sku_client_install_file(const char *downloaded, const char *target, char *err, size_t en)
{
	char tmp[SKU_PATH_MAX];
	FILE *in, *out;
	char buf[65536];
	size_t n;
	bool ok = true;

	if (err && en) err[0] = 0;
	sku_snprintf(tmp, sizeof tmp, "%s.skupart", target);
	in = sku_fopen(downloaded, "rb");
	if (!in) { sku_snprintf(err, en, "cannot read %s", downloaded); return SKU_ERR_IO; }
	out = sku_fopen(tmp, "wb");
	if (!out) { fclose(in); sku_snprintf(err, en, "cannot write next to %s", target); return SKU_ERR_IO; }
	while ((n = fread(buf, 1, sizeof buf, in)) > 0) if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
	if (ferror(in)) ok = false;
	if (fclose(out) != 0) ok = false;
	fclose(in);
	if (!ok) { sku_remove_file(tmp); sku_snprintf(err, en, "copy to %s failed", tmp); return SKU_ERR_IO; }
	if (sku_set_executable(tmp) != SKU_OK) { sku_remove_file(tmp); sku_snprintf(err, en, "cannot make %s executable", tmp); return SKU_ERR_IO; }
	if (sku_rename(tmp, target) != SKU_OK) { sku_remove_file(tmp); sku_snprintf(err, en, "cannot replace %s", target); return SKU_ERR_IO; }
	sku_remove_file(downloaded);
	sku_info("client: %s installed over %s", downloaded, target);
	return SKU_OK;
}

int sku_client_install_from_zip(const char *zip, const char *entry, const char *target, char *err, size_t en)
{
	char dir[SKU_PATH_MAX], extracted[SKU_PATH_MAX];
	int rc;
	sku_path_dirname(dir, sizeof dir, zip);
	sku_path_join(extracted, sizeof extracted, dir, entry);
	rc = sku_zip_extract(zip, entry, extracted, err, en);
	if (rc != SKU_OK) { sku_error("client: cannot extract %s from %s: %s", entry, zip, err ? err : ""); return rc; }
	rc = sku_client_install_file(extracted, target, err, en);
	if (rc == SKU_OK) sku_remove_file(zip);
	else sku_remove_file(extracted);
	return rc;
}

void sku_client_info_free(sku_client_info *info)
{
	free(info->changelog);
	info->changelog = NULL;
}
