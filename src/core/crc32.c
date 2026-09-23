#include "crc32.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

static uint32_t table[256];
static bool table_ready;

static void make_table(void)
{
	for (uint32_t i = 0; i < 256; i++) {
		uint32_t c = i;
		for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		table[i] = c;
	}
	table_ready = true;
}

uint32_t sku_crc32_update(uint32_t crc, const void *data, size_t len)
{
	const unsigned char *p = data;
	if (!table_ready) make_table();   /* idempotent: concurrent first calls compute the same table */
	crc = ~crc;
	for (size_t i = 0; i < len; i++) crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
	return ~crc;
}

uint32_t sku_crc32_buf(const void *data, size_t len)
{
	return sku_crc32_update(0, data, len);
}

int sku_crc32_file(const char *path, uint32_t *out, atomic_int *cancel)
{
	enum { BLOCK = 256 * 1024 };
	FILE *f = sku_fopen(path, "rb");
	unsigned char *buf;
	uint32_t crc = 0;
	size_t k;
	if (!f) return SKU_ERR_NOTFOUND;
	buf = sku_xmalloc(BLOCK);
	while ((k = fread(buf, 1, BLOCK, f)) > 0) {
		crc = sku_crc32_update(crc, buf, k);
		if (cancel && atomic_load(cancel)) { free(buf); fclose(f); return SKU_ERR_CANCELLED; }
	}
	free(buf);
	if (ferror(f)) { fclose(f); return SKU_ERR_IO; }
	fclose(f);
	*out = crc;
	return SKU_OK;
}
