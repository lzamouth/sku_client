#include "strmap.h"
#include <stdlib.h>
#include <string.h>

static uint64_t hash(const char *s)
{
	uint64_t h = 1469598103934665603ULL;
	for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
	return h;
}

void sku_strmap_init(sku_strmap *m, size_t expected)
{
	m->nbuckets = 64;
	while (m->nbuckets < expected * 2) m->nbuckets *= 2;
	m->buckets = sku_xcalloc(m->nbuckets, sizeof *m->buckets);
	m->count = 0;
}

void sku_strmap_free(sku_strmap *m)
{
	for (size_t i = 0; i < m->nbuckets; i++) {
		sku_strmap_node *n = m->buckets[i];
		while (n) { sku_strmap_node *next = n->next; free(n->key); free(n); n = next; }
	}
	free(m->buckets);
	memset(m, 0, sizeof *m);
}

sku_strmap_node *sku_strmap_get(const sku_strmap *m, const char *key)
{
	if (!m->nbuckets) return NULL;
	for (sku_strmap_node *n = m->buckets[hash(key) & (m->nbuckets - 1)]; n; n = n->next)
		if (strcmp(n->key, key) == 0) return n;
	return NULL;
}

static void grow(sku_strmap *m)
{
	size_t nb = m->nbuckets * 2;
	sku_strmap_node **b = sku_xcalloc(nb, sizeof *b);
	for (size_t i = 0; i < m->nbuckets; i++) {
		sku_strmap_node *n = m->buckets[i];
		while (n) {
			sku_strmap_node *next = n->next;
			size_t k = hash(n->key) & (nb - 1);
			n->next = b[k]; b[k] = n;
			n = next;
		}
	}
	free(m->buckets);
	m->buckets = b;
	m->nbuckets = nb;
}

sku_strmap_node *sku_strmap_put(sku_strmap *m, const char *key)
{
	sku_strmap_node *n = sku_strmap_get(m, key);
	size_t k;
	if (n) return n;
	if (!m->nbuckets) sku_strmap_init(m, 0);
	if (m->count + 1 > m->nbuckets) grow(m);
	n = sku_xcalloc(1, sizeof *n);
	n->key = sku_xstrdup(key);
	k = hash(key) & (m->nbuckets - 1);
	n->next = m->buckets[k];
	m->buckets[k] = n;
	m->count++;
	return n;
}

bool sku_strmap_remove(sku_strmap *m, const char *key)
{
	sku_strmap_node **pp;
	if (!m->nbuckets) return false;
	for (pp = &m->buckets[hash(key) & (m->nbuckets - 1)]; *pp; pp = &(*pp)->next) {
		if (strcmp((*pp)->key, key) == 0) {
			sku_strmap_node *n = *pp;
			*pp = n->next;
			free(n->key); free(n);
			m->count--;
			return true;
		}
	}
	return false;
}

sku_strmap_node *sku_strmap_first(const sku_strmap *m, sku_strmap_iter *it)
{
	it->bucket = 0; it->node = NULL;
	return sku_strmap_next(m, it);
}

sku_strmap_node *sku_strmap_next(const sku_strmap *m, sku_strmap_iter *it)
{
	if (it->node && it->node->next) { it->node = it->node->next; return it->node; }
	if (it->node) it->bucket++;
	for (; it->bucket < m->nbuckets; it->bucket++) {
		if (m->buckets[it->bucket]) { it->node = m->buckets[it->bucket]; return it->node; }
	}
	it->node = NULL;
	return NULL;
}
