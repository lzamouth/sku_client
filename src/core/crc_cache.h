/* Per-addon cache of file CRCs keyed by (path, size, mtime), so a check only
 * hashes files that changed since the last verified state. Thread-safe. */
#ifndef SKU_CRC_CACHE_H
#define SKU_CRC_CACHE_H

#include "sku.h"

typedef struct sku_crc_cache sku_crc_cache;

sku_crc_cache *sku_crc_cache_open(const char *path);   /* loads the file if present */
void sku_crc_cache_close(sku_crc_cache *c);

/* True and *crc set when the cached entry matches size and mtime. */
bool sku_crc_cache_lookup(sku_crc_cache *c, const char *rel, int64_t size, int64_t mtime, uint32_t *crc);
void sku_crc_cache_store(sku_crc_cache *c, const char *rel, int64_t size, int64_t mtime, uint32_t crc);
void sku_crc_cache_forget(sku_crc_cache *c, const char *rel);

/* Writes back only the entries used or stored since open: stale ones are dropped. */
int  sku_crc_cache_save(sku_crc_cache *c);
int  sku_crc_cache_count(const sku_crc_cache *c);

#endif
