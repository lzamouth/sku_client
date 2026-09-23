/* String-keyed hash map (chaining, FNV-1a). Values are opaque pointers or
 * integers; keys are copied. Not thread-safe. */
#ifndef SKU_STRMAP_H
#define SKU_STRMAP_H

#include "sku.h"

typedef struct sku_strmap_node {
	struct sku_strmap_node *next;
	char   *key;
	void   *ptr;
	int64_t num;
} sku_strmap_node;

typedef struct {
	sku_strmap_node **buckets;
	size_t nbuckets, count;
} sku_strmap;

void  sku_strmap_init(sku_strmap *m, size_t expected);
void  sku_strmap_free(sku_strmap *m);
sku_strmap_node *sku_strmap_get(const sku_strmap *m, const char *key);
sku_strmap_node *sku_strmap_put(sku_strmap *m, const char *key);   /* existing node or a new zeroed one */
bool  sku_strmap_remove(sku_strmap *m, const char *key);

/* Iteration: for (n = sku_strmap_first(m, &it); n; n = sku_strmap_next(m, &it)) */
typedef struct { size_t bucket; sku_strmap_node *node; } sku_strmap_iter;
sku_strmap_node *sku_strmap_first(const sku_strmap *m, sku_strmap_iter *it);
sku_strmap_node *sku_strmap_next(const sku_strmap *m, sku_strmap_iter *it);

#endif
