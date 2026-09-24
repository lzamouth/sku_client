#include "changelog.h"
#include "platform.h"
#include "paths.h"
#include "protocol.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

bool sku_changelog_is_header(const char *line, char *out, size_t n)
{
	const char *p = line;
	size_t len;
	while (*p == ' ' || *p == '\t') p++;
	while (*p == '#') p++;               /* markdown heading marks */
	while (*p == ' ' || *p == '\t') p++;
	if ((*p == 'v' || *p == 'V') && isdigit((unsigned char)p[1])) p++;
	if (!isdigit((unsigned char)*p)) return false;
	len = strlen(p);
	while (len && isspace((unsigned char)p[len - 1])) len--;
	if (len == 0 || len >= n || len > 40) return false;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)p[i];
		if (!(isalnum(c) || c == '.' || c == '-' || c == '_')) return false;   /* a version has no spaces */
	}
	memcpy(out, p, len); out[len] = 0;
	return true;
}

/* Versions are compared as they are written by the depot, optionally with a
 * leading "v" ignored on either side. */
static bool same_version(const char *a, const char *b)
{
	if ((*a == 'v' || *a == 'V') && isdigit((unsigned char)a[1])) a++;
	if ((*b == 'v' || *b == 'V') && isdigit((unsigned char)b[1])) b++;
	return strcmp(a, b) == 0;
}

int sku_changelog_sections(const char *text, const char *installed_version, sku_changelog_section **out)
{
	char *copy = sku_xstrdup(text), *cursor = sku_skip_bom(copy), *line;
	int n = 0, lineno = 0, installed_at = -1;
	sku_changelog_section *s = NULL;

	while ((line = sku_next_line(&cursor)) != NULL) {
		char version[64];
		if (sku_changelog_is_header(line, version, sizeof version) || n == 0) {
			s = sku_xrealloc(s, (size_t)(n + 1) * sizeof *s);
			memset(&s[n], 0, sizeof s[n]);
			if (sku_changelog_is_header(line, version, sizeof version)) sku_strlcpy(s[n].version, version, sizeof s[n].version);
			s[n].first_line = lineno;
			if (n && s[n - 1].last_line < lineno - 1) s[n - 1].last_line = lineno - 1;
			n++;
		}
		s[n - 1].last_line = lineno;
		lineno++;
	}
	free(copy);
	if (installed_version && *installed_version)
		for (int i = 0; i < n; i++) if (s[i].version[0] && same_version(s[i].version, installed_version)) { installed_at = i; break; }
	if (installed_at > 0)
		for (int i = 0; i < installed_at; i++) if (s[i].version[0]) s[i].newer = true;
	*out = s;
	return n;
}

void sku_addon_doc_cache_path(const sku_addon *a, const char *kind, char *out, size_t n)
{
	char cache[SKU_PATH_MAX];
	sku_instance_cache_path(a->inst, a->dir, cache, sizeof cache);   /* .../cache/<key>.lists */
	cache[strlen(cache) - 6] = 0;
	sku_snprintf(out, n, "%s.%s.txt", cache, kind);
}

int sku_addon_doc_path(const sku_addon *a, const char *kind, char *out, size_t n)
{
	const char *rel;
	sku_addon_doc_cache_path(a, kind, out, n);
	if (sku_is_file(out)) return SKU_OK;
	/* the copy shipped in the addon: the declared name, else the default */
	rel = strcmp(kind, "readme") == 0 ? (a->readme[0] ? a->readme : SKU_F_README) : (a->changelog[0] ? a->changelog : SKU_F_CHANGELOG);
	sku_path_join(out, n, a->dir, rel);
	if (sku_is_file(out)) return SKU_OK;
	out[0] = 0;
	return SKU_ERR_NOTFOUND;
}
