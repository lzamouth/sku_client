#include "cfg.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

void sku_cfg_init(sku_cfg *c)
{
	memset(c, 0, sizeof *c);
}

void sku_cfg_free(sku_cfg *c)
{
	for (int i = 0; i < c->n; i++) { free(c->v[i].key); free(c->v[i].value); }
	free(c->v);
	sku_cfg_init(c);
}

static sku_cfg_line *push(sku_cfg *c)
{
	if (c->n == c->cap) {
		c->cap = c->cap ? c->cap * 2 : 16;
		c->v = sku_xrealloc(c->v, (size_t)c->cap * sizeof *c->v);
	}
	memset(&c->v[c->n], 0, sizeof c->v[c->n]);
	return &c->v[c->n++];
}

static sku_cfg_line *find(const sku_cfg *c, const char *key)
{
	for (int i = 0; i < c->n; i++)
		if (c->v[i].key && strcmp(c->v[i].key, key) == 0) return &c->v[i];
	return NULL;
}

int sku_cfg_parse(sku_cfg *c, const char *text)
{
	char *copy = sku_xstrdup(text), *p = copy;
	while (p && *p) {
		char *nl = strchr(p, '\n'), *sep, *key, *val;
		sku_cfg_line *l;
		if (nl) *nl = 0;
		sku_str_chomp(p);
		key = sku_str_trim(p);
		sep = strchr(key, '|');
		l = push(c);
		if (!*key || *key == '#' || !sep) {
			l->value = sku_xstrdup(key);
		} else {
			*sep = 0;
			val = sku_str_trim(sep + 1);
			l->key = sku_xstrdup(sku_str_trim(key));
			l->value = sku_xstrdup(val);
		}
		p = nl ? nl + 1 : NULL;
	}
	free(copy);
	return SKU_OK;
}

int sku_cfg_load(sku_cfg *c, const char *path)
{
	char *text;
	int rc = sku_read_file(path, &text, NULL);
	if (rc != SKU_OK) return rc;
	rc = sku_cfg_parse(c, text);
	free(text);
	return rc;
}

int sku_cfg_format(const sku_cfg *c, char **out, bool v1)
{
	size_t cap = 256, n = 0;
	char *buf = sku_xmalloc(cap);
	for (int i = 0; i < c->n; i++) {
		const sku_cfg_line *l = &c->v[i];
		size_t need;
		if (v1 && !l->key) continue;
		need = (l->key ? strlen(l->key) + 1 : 0) + strlen(l->value) + 2;
		if (n + need > cap) { while (n + need > cap) cap *= 2; buf = sku_xrealloc(buf, cap); }
		if (l->key) n += (size_t)sprintf(buf + n, "%s|%s\n", l->key, l->value);
		else n += (size_t)sprintf(buf + n, "%s\n", l->value);
	}
	buf[n] = 0;
	*out = buf;
	return (int)n;
}

static int save(const sku_cfg *c, const char *path, bool v1)
{
	char *text;
	int n = sku_cfg_format(c, &text, v1), rc;
	rc = sku_write_file_atomic(path, text, (size_t)n);
	free(text);
	return rc;
}

int sku_cfg_save(const sku_cfg *c, const char *path)    { return save(c, path, false); }
int sku_cfg_save_v1(const sku_cfg *c, const char *path) { return save(c, path, true); }

bool sku_cfg_has(const sku_cfg *c, const char *key)
{
	return find(c, key) != NULL;
}

const char *sku_cfg_get(const sku_cfg *c, const char *key)
{
	sku_cfg_line *l = find(c, key);
	return l ? l->value : NULL;
}

const char *sku_cfg_get_or(const sku_cfg *c, const char *key, const char *def)
{
	const char *v = sku_cfg_get(c, key);
	return v ? v : def;
}

bool sku_cfg_get_bool(const sku_cfg *c, const char *key, bool def)
{
	const char *v = sku_cfg_get(c, key);
	return v ? sku_str_to_bool(v) : def;
}

void sku_cfg_set(sku_cfg *c, const char *key, const char *value)
{
	sku_cfg_line *l = find(c, key);
	if (l) { free(l->value); l->value = sku_xstrdup(value); return; }
	l = push(c);
	l->key = sku_xstrdup(key);
	l->value = sku_xstrdup(value);
}

void sku_cfg_set_bool(sku_cfg *c, const char *key, bool value)
{
	sku_cfg_set(c, key, sku_bool_to_str(value));
}

void sku_cfg_set_default(sku_cfg *c, const char *key, const char *value)
{
	if (!find(c, key)) sku_cfg_set(c, key, value);
}

void sku_cfg_remove(sku_cfg *c, const char *key)
{
	for (int i = 0; i < c->n; i++) {
		if (c->v[i].key && strcmp(c->v[i].key, key) == 0) {
			free(c->v[i].key); free(c->v[i].value);
			memmove(&c->v[i], &c->v[i + 1], (size_t)(c->n - i - 1) * sizeof *c->v);
			c->n--;
			i--;
		}
	}
}

void sku_cfg_add_raw(sku_cfg *c, const char *line)
{
	sku_cfg_line *l = push(c);
	l->value = sku_xstrdup(line);
}
