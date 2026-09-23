#include "crc_cache.h"
#include "strmap.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct entry {
	int64_t  size, mtime;
	uint32_t crc;
	bool     touched;
};

struct sku_crc_cache {
	char path[SKU_PATH_MAX];
	sku_strmap map;      /* rel path -> struct entry* */
	pthread_mutex_t lock;
};

sku_crc_cache *sku_crc_cache_open(const char *path)
{
	sku_crc_cache *c = sku_xcalloc(1, sizeof *c);
	char *text;
	sku_strlcpy(c->path, path, sizeof c->path);
	sku_strmap_init(&c->map, 1024);
	pthread_mutex_init(&c->lock, NULL);
	if (sku_read_file(path, &text, NULL) == SKU_OK) {
		char *cursor = text, *p;
		while ((p = sku_next_line(&cursor)) != NULL) {
			char *f[4];
			int nf = 0;
			/* path|size|mtime|crc */
			for (char *q = p; nf < 4; ) {
				char *sep = strchr(q, '|');
				f[nf++] = q;
				if (!sep) break;
				*sep = 0; q = sep + 1;
			}
			if (nf == 4 && *f[0]) {
				struct entry *e = sku_xcalloc(1, sizeof *e);
				e->size  = strtoll(f[1], NULL, 10);
				e->mtime = strtoll(f[2], NULL, 10);
				e->crc   = (uint32_t)strtoull(f[3], NULL, 10);
				sku_strmap_put(&c->map, f[0])->ptr = e;
			}
		}
		free(text);
	}
	return c;
}

void sku_crc_cache_close(sku_crc_cache *c)
{
	sku_strmap_iter it;
	if (!c) return;
	for (sku_strmap_node *n = sku_strmap_first(&c->map, &it); n; n = sku_strmap_next(&c->map, &it)) free(n->ptr);
	sku_strmap_free(&c->map);
	pthread_mutex_destroy(&c->lock);
	free(c);
}

bool sku_crc_cache_lookup(sku_crc_cache *c, const char *rel, int64_t size, int64_t mtime, uint32_t *crc)
{
	bool hit = false;
	sku_strmap_node *n;
	pthread_mutex_lock(&c->lock);
	n = sku_strmap_get(&c->map, rel);
	if (n) {
		struct entry *e = n->ptr;
		if (e->size == size && e->mtime == mtime) { *crc = e->crc; e->touched = true; hit = true; }
	}
	pthread_mutex_unlock(&c->lock);
	return hit;
}

void sku_crc_cache_store(sku_crc_cache *c, const char *rel, int64_t size, int64_t mtime, uint32_t crc)
{
	sku_strmap_node *n;
	struct entry *e;
	pthread_mutex_lock(&c->lock);
	n = sku_strmap_put(&c->map, rel);
	if (!n->ptr) n->ptr = sku_xcalloc(1, sizeof *e);
	e = n->ptr;
	e->size = size; e->mtime = mtime; e->crc = crc; e->touched = true;
	pthread_mutex_unlock(&c->lock);
}

void sku_crc_cache_forget(sku_crc_cache *c, const char *rel)
{
	sku_strmap_node *n;
	pthread_mutex_lock(&c->lock);
	n = sku_strmap_get(&c->map, rel);
	if (n) { free(n->ptr); sku_strmap_remove(&c->map, rel); }
	pthread_mutex_unlock(&c->lock);
}

int sku_crc_cache_save(sku_crc_cache *c, bool prune)
{
	size_t cap = 65536, len = 0;
	char *buf = sku_xmalloc(cap);
	sku_strmap_iter it;
	int rc;
	pthread_mutex_lock(&c->lock);
	for (sku_strmap_node *n = sku_strmap_first(&c->map, &it); n; n = sku_strmap_next(&c->map, &it)) {
		struct entry *e = n->ptr;
		size_t need = strlen(n->key) + 64;
		if (prune && !e->touched) continue;
		if (len + need > cap) { while (len + need > cap) cap *= 2; buf = sku_xrealloc(buf, cap); }
		len += (size_t)sprintf(buf + len, "%s|%lld|%lld|%lu\n", n->key, (long long)e->size, (long long)e->mtime, (unsigned long)e->crc);
	}
	pthread_mutex_unlock(&c->lock);
	rc = sku_write_file_atomic(c->path, buf, len);
	free(buf);
	return rc;
}

int sku_crc_cache_count(const sku_crc_cache *c)
{
	return (int)c->map.count;
}
