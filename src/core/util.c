#include "sku.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

const char *sku_err_str(int err)
{
	switch (err) {
	case SKU_OK:            return "ok";
	case SKU_ERR_IO:        return "file system error";
	case SKU_ERR_NOTFOUND:  return "not found";
	case SKU_ERR_FORMAT:    return "malformed data";
	case SKU_ERR_NET:       return "network error";
	case SKU_ERR_UNSAFE:    return "unsafe path rejected";
	case SKU_ERR_CANCELLED: return "cancelled";
	case SKU_ERR_NOMEM:     return "out of memory";
	case SKU_ERR_ARG:       return "invalid argument";
	default:                return "unknown error";
	}
}

static void oom(void)
{
	fputs("sku: out of memory\n", stderr);
	abort();
}

void *sku_xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	if (!p) oom();
	return p;
}

void *sku_xcalloc(size_t n, size_t sz)
{
	void *p = calloc(n ? n : 1, sz ? sz : 1);
	if (!p) oom();
	return p;
}

void *sku_xrealloc(void *p, size_t n)
{
	p = realloc(p, n ? n : 1);
	if (!p) oom();
	return p;
}

char *sku_xstrdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = sku_xmalloc(n);
	memcpy(d, s, n);
	return d;
}

size_t sku_strlcpy(char *dst, const char *src, size_t dstsize)
{
	size_t n = strlen(src);
	if (dstsize) {
		size_t k = n < dstsize - 1 ? n : dstsize - 1;
		memcpy(dst, src, k);
		dst[k] = 0;
	}
	return n;
}

size_t sku_strlcat(char *dst, const char *src, size_t dstsize)
{
	size_t d = strlen(dst);
	if (d >= dstsize) return d + strlen(src);
	return d + sku_strlcpy(dst + d, src, dstsize - d);
}

int sku_snprintf(char *dst, size_t dstsize, const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(dst, dstsize, fmt, ap);
	va_end(ap);
	if (n < 0 && dstsize) dst[0] = 0;
	return n;
}

bool sku_str_ieq(const char *a, const char *b)
{
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
		a++; b++;
	}
	return *a == *b;
}

bool sku_str_istarts(const char *s, const char *prefix)
{
	while (*prefix) {
		if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) return false;
		s++; prefix++;
	}
	return true;
}

bool sku_str_ends(const char *s, const char *suffix)
{
	size_t n = strlen(s), m = strlen(suffix);
	return m <= n && memcmp(s + n - m, suffix, m) == 0;
}

char *sku_str_trim(char *s)
{
	char *e;
	while (*s && isspace((unsigned char)*s)) s++;
	e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) e--;
	*e = 0;
	return s;
}

void sku_str_chomp(char *s)
{
	size_t n = strlen(s);
	while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = 0;
}

bool sku_str_to_bool(const char *s)
{
	return s && strcmp(s, "true") == 0;
}

const char *sku_bool_to_str(bool b)
{
	return b ? "true" : "false";
}

void sku_nice_size(char *out, size_t n, int64_t bytes)
{
	if (bytes < 1)                sku_snprintf(out, n, "0");
	else if (bytes >= 1 << 30)    sku_snprintf(out, n, "%.2f GiB", (double)bytes / (1 << 30));
	else if (bytes >= 1 << 20)    sku_snprintf(out, n, "%.2f MiB", (double)bytes / (1 << 20));
	else if (bytes >= 1 << 10)    sku_snprintf(out, n, "%.2f kiB", (double)bytes / (1 << 10));
	else                          sku_snprintf(out, n, "%lld bytes", (long long)bytes);
}

void sku_strlist_push(sku_strlist *l, const char *s)
{
	if (l->n == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 16;
		l->v = sku_xrealloc(l->v, (size_t)l->cap * sizeof *l->v);
	}
	l->v[l->n++] = sku_xstrdup(s);
}

void sku_strlist_free(sku_strlist *l)
{
	for (int i = 0; i < l->n; i++) free(l->v[i]);
	free(l->v);
	l->v = NULL; l->n = l->cap = 0;
}

bool sku_strlist_has(const sku_strlist *l, const char *s)
{
	for (int i = 0; i < l->n; i++)
		if (strcmp(l->v[i], s) == 0) return true;
	return false;
}

char *sku_skip_bom(char *text)
{
	if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) return text + 3;
	return text;
}

char *sku_next_line(char **cursor)
{
	char *line = *cursor, *p;
	if (!line || !*line) return NULL;
	for (p = line; *p && *p != '\n' && *p != '\r'; p++) ;
	if (*p == '\r' && p[1] == '\n') { *p = 0; *cursor = p + 2; }
	else if (*p) { *p = 0; *cursor = p + 1; }
	else *cursor = p;
	return line;
}

int sku_split_lines(char *text, sku_strlist *out)
{
	char *cursor = sku_skip_bom(text), *line;
	while ((line = sku_next_line(&cursor)) != NULL) sku_strlist_push(out, line);
	return out->n;
}
