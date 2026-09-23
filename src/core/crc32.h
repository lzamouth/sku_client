/* CRC-32 (IEEE 802.3, polynomial 0xEDB88320), the same as zlib and the depots. */
#ifndef SKU_CRC32_H
#define SKU_CRC32_H

#include "sku.h"
#include <stdatomic.h>

uint32_t sku_crc32_update(uint32_t crc, const void *data, size_t len); /* start with crc = 0 */
uint32_t sku_crc32_buf(const void *data, size_t len);

/* Streams the file; *cancel (may be NULL) is polled between blocks. */
int      sku_crc32_file(const char *path, uint32_t *out, atomic_int *cancel);

#endif
