#include "paths.h"
#include <string.h>
#include <ctype.h>

void sku_path_join(char *out, size_t n, const char *dir, const char *rel)
{
	size_t d = strlen(dir);
	if (d == 0) { sku_strlcpy(out, rel, n); return; }
	if (dir[d - 1] == '/') sku_snprintf(out, n, "%s%s", dir, rel);
	else sku_snprintf(out, n, "%s/%s", dir, rel);
}

void sku_path_normalize(char *p)
{
	char *w = p, *r = p;
	for (; *r; r++) {
		char c = *r == '\\' ? '/' : *r;
		if (c == '/' && w > p && w[-1] == '/' && !(w == p + 1)) continue;  /* keep a leading "//" (UNC) */
		*w++ = c;
	}
	*w = 0;
	/* drop a trailing slash unless the path is a root ("/" or "C:/") */
	{
		size_t n = strlen(p);
		while (n > 1 && p[n - 1] == '/' && !(n == 3 && p[1] == ':')) p[--n] = 0;
	}
}

const char *sku_path_basename(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

void sku_path_dirname(char *out, size_t n, const char *p)
{
	const char *s = strrchr(p, '/');
	if (!s) { sku_strlcpy(out, ".", n); return; }
	if (s == p) { sku_strlcpy(out, "/", n); return; }
	if ((size_t)(s - p) >= n) { out[0] = 0; return; }
	memcpy(out, p, (size_t)(s - p));
	out[s - p] = 0;
}

bool sku_path_is_absolute(const char *p)
{
	if (p[0] == '/' || p[0] == '\\') return true;
	if (isalpha((unsigned char)p[0]) && p[1] == ':') return true;
	return false;
}

int sku_path_check_relative(char *rel)
{
	char *comp;
	if (!rel || !*rel) return SKU_ERR_UNSAFE;
	sku_path_normalize(rel);
	if (!*rel || sku_path_is_absolute(rel)) return SKU_ERR_UNSAFE;
	for (const unsigned char *c = (unsigned char *)rel; *c; c++)
		if (*c < 0x20 || *c == 0x7f || *c == ':') return SKU_ERR_UNSAFE;
	comp = rel;
	for (;;) {
		char *slash = strchr(comp, '/');
		size_t len = slash ? (size_t)(slash - comp) : strlen(comp);
		if (len == 0) return SKU_ERR_UNSAFE;                                  /* "a//b" cannot happen after normalize, "a/" can */
		if ((len == 1 && comp[0] == '.') || (len == 2 && comp[0] == '.' && comp[1] == '.')) return SKU_ERR_UNSAFE;
		if (!slash) break;
		comp = slash + 1;
	}
	return SKU_OK;
}
