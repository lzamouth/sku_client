/* Minimal zip reader, see zip.h. Central directory first (the authoritative
 * one), then the local header of the wanted entry, then zlib for deflate. */
#include "zip.h"
#include "platform.h"
#include "paths.h"
#include "log.h"
#include <zlib.h>
#include <stdlib.h>
#include <string.h>

#define SIG_EOCD    0x06054b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_LOCAL   0x04034b50u

static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static void fail(char *err, size_t en, const char *why)
{
	if (err && en) sku_strlcpy(err, why, en);
}

/* Reads `n` bytes at `off`; false on short read. */
static bool read_at(FILE *f, int64_t off, void *buf, size_t n)
{
	return fseeko(f, (off_t)off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
}

/* One entry of the central directory. */
struct entry {
	char     name[SKU_PATH_MAX];
	uint32_t method, crc, comp, uncomp, attrs;
	int64_t  local_off;
};

/* Loads the central directory: *cd (malloc'd), its size and entry count. */
static int load_central(FILE *f, int64_t size, unsigned char **cd, size_t *cd_size, size_t *nentries, char *err, size_t en)
{
	unsigned char tail[65536 + 22];
	size_t tail_len, i;
	int64_t eocd_off = -1, cd_off;
	tail_len = (size_t)(size < (int64_t)sizeof tail ? size : (int64_t)sizeof tail);
	if (!read_at(f, size - (int64_t)tail_len, tail, tail_len)) { fail(err, en, "cannot read the zip"); return SKU_ERR_IO; }
	for (i = tail_len - 22; ; i--) {
		if (rd32(tail + i) == SIG_EOCD) { eocd_off = (int64_t)i; break; }
		if (i == 0) break;
	}
	if (eocd_off < 0) { fail(err, en, "no central directory (not a zip, or zip64)"); return SKU_ERR_FORMAT; }
	*nentries = rd16(tail + eocd_off + 10);
	*cd_size = rd32(tail + eocd_off + 12);
	cd_off = rd32(tail + eocd_off + 16);
	if (*cd_size == 0 || *cd_size > 64u * 1024 * 1024 || cd_off + (int64_t)*cd_size > size) { fail(err, en, "bad central directory"); return SKU_ERR_FORMAT; }
	*cd = sku_xmalloc(*cd_size);
	if (!read_at(f, cd_off, *cd, *cd_size)) { free(*cd); *cd = NULL; fail(err, en, "cannot read the central directory"); return SKU_ERR_IO; }
	return SKU_OK;
}

/* Decodes entry i (at *off) and advances *off; false when the directory is corrupt. */
static bool next_entry(const unsigned char *cd, size_t cd_size, int64_t *off, struct entry *e)
{
	const unsigned char *p = cd + *off;
	size_t n, x, c;
	if ((size_t)*off + 46 > cd_size || rd32(p) != SIG_CENTRAL) return false;
	n = rd16(p + 28); x = rd16(p + 30); c = rd16(p + 32);
	if ((size_t)*off + 46 + n > cd_size || n >= sizeof e->name) return false;
	memcpy(e->name, p + 46, n); e->name[n] = 0;
	e->method = rd16(p + 10); e->crc = rd32(p + 16); e->comp = rd32(p + 20); e->uncomp = rd32(p + 24);
	e->attrs = rd32(p + 38); e->local_off = rd32(p + 42);
	*off += 46 + (int64_t)(n + x + c);
	return true;
}

/* Writes one entry's data to dest, CRC checked. */
static int extract_entry(FILE *f, int64_t size, const struct entry *e, const char *dest, char *err, size_t en)
{
	FILE *out = NULL;
	unsigned char *in = NULL, *outbuf = NULL;
	uint32_t got_crc;
	int64_t off;
	int rc = SKU_ERR_FORMAT;

	if (e->method != 0 && e->method != 8) { fail(err, en, "unsupported compression method"); return SKU_ERR_FORMAT; }
	if (e->comp == 0xFFFFFFFFu || e->uncomp == 0xFFFFFFFFu) { fail(err, en, "zip64 entries are not supported"); return SKU_ERR_FORMAT; }
	{
		unsigned char lh[30];
		if (!read_at(f, e->local_off, lh, sizeof lh) || rd32(lh) != SIG_LOCAL) { fail(err, en, "corrupt local header"); return SKU_ERR_FORMAT; }
		off = e->local_off + 30 + rd16(lh + 26) + rd16(lh + 28);
	}
	if (off + (int64_t)e->comp > size) { fail(err, en, "truncated zip"); return SKU_ERR_FORMAT; }
	{
		uint32_t method = e->method, comp = e->comp, want_crc = e->crc;

	out = sku_fopen(dest, "wb");
	if (!out) { fail(err, en, "cannot write the extracted file"); rc = SKU_ERR_IO; goto out; }
	if (fseeko(f, (off_t)off, SEEK_SET) != 0) { fail(err, en, "cannot read the zip"); rc = SKU_ERR_IO; goto out; }
	in = sku_xmalloc(65536);
	got_crc = (uint32_t)crc32(0L, Z_NULL, 0);
	if (method == 0) {
		size_t left = comp;
		while (left) {
			size_t n = left < 65536 ? left : 65536;
			if (fread(in, 1, n, f) != n || fwrite(in, 1, n, out) != n) { fail(err, en, "read/write error"); rc = SKU_ERR_IO; goto out; }
			got_crc = (uint32_t)crc32(got_crc, in, (uInt)n);
			left -= n;
		}
	} else {
		z_stream z;
		size_t left = comp;
		int zrc;
		memset(&z, 0, sizeof z);
		if (inflateInit2(&z, -MAX_WBITS) != Z_OK) { fail(err, en, "zlib init failed"); goto out; }
		outbuf = sku_xmalloc(65536);
		do {
			size_t n = left < 65536 ? left : 65536;
			if (n && fread(in, 1, n, f) != n) { inflateEnd(&z); fail(err, en, "read error"); rc = SKU_ERR_IO; goto out; }
			left -= n;
			z.next_in = in; z.avail_in = (uInt)n;
			do {
				size_t produced;
				z.next_out = outbuf; z.avail_out = 65536;
				zrc = inflate(&z, Z_NO_FLUSH);
				if (zrc != Z_OK && zrc != Z_STREAM_END && zrc != Z_BUF_ERROR) { inflateEnd(&z); fail(err, en, "corrupt deflate stream"); goto out; }
				produced = 65536 - z.avail_out;
				if (produced && fwrite(outbuf, 1, produced, out) != produced) { inflateEnd(&z); fail(err, en, "write error"); rc = SKU_ERR_IO; goto out; }
				got_crc = (uint32_t)crc32(got_crc, outbuf, (uInt)produced);
			} while (z.avail_out == 0 && zrc != Z_STREAM_END);
		} while (zrc != Z_STREAM_END && left);
		inflateEnd(&z);
		if (zrc != Z_STREAM_END) { fail(err, en, "truncated deflate stream"); goto out; }
	}
	if (fclose(out) != 0) { out = NULL; fail(err, en, "write error"); rc = SKU_ERR_IO; goto out; }
	out = NULL;
	if (got_crc != want_crc) { fail(err, en, "CRC mismatch in the zip entry"); goto out; }
	rc = SKU_OK;
	}
out:
	if (out) fclose(out);
	if (rc != SKU_OK) sku_remove_file(dest);
	free(outbuf); free(in);
	return rc;
}

int sku_zip_extract(const char *zip_path, const char *entry, const char *dest, char *err, size_t en)
{
	FILE *f;
	int64_t size, off = 0;
	unsigned char *cd = NULL;
	size_t cd_size, nentries, i;
	struct entry e;
	int rc;

	if (err && en) err[0] = 0;
	f = sku_fopen(zip_path, "rb");
	if (!f) { fail(err, en, "cannot open the zip"); return SKU_ERR_IO; }
	if (fseeko(f, 0, SEEK_END) != 0 || (size = (int64_t)ftello(f)) < 22) { fail(err, en, "not a zip file"); fclose(f); return SKU_ERR_FORMAT; }
	if ((rc = load_central(f, size, &cd, &cd_size, &nentries, err, en)) != SKU_OK) { fclose(f); return rc; }
	rc = SKU_ERR_NOTFOUND;
	fail(err, en, "entry not found in the zip");
	for (i = 0; i < nentries; i++) {
		if (!next_entry(cd, cd_size, &off, &e)) { fail(err, en, "corrupt central directory"); rc = SKU_ERR_FORMAT; break; }
		if (strcmp(e.name, entry) == 0) { rc = extract_entry(f, size, &e, dest, err, en); break; }
	}
	free(cd);
	fclose(f);
	return rc;
}

int sku_zip_extract_all(const char *zip_path, const char *dest_dir, char *err, size_t en)
{
	FILE *f;
	int64_t size, off = 0;
	unsigned char *cd = NULL;
	size_t cd_size, nentries, i;
	struct entry e;
	int rc;

	if (err && en) err[0] = 0;
	f = sku_fopen(zip_path, "rb");
	if (!f) { fail(err, en, "cannot open the zip"); return SKU_ERR_IO; }
	if (fseeko(f, 0, SEEK_END) != 0 || (size = (int64_t)ftello(f)) < 22) { fail(err, en, "not a zip file"); fclose(f); return SKU_ERR_FORMAT; }
	if ((rc = load_central(f, size, &cd, &cd_size, &nentries, err, en)) != SKU_OK) { fclose(f); return rc; }
	if (sku_mkdir_p(dest_dir) != SKU_OK) { fail(err, en, "cannot create the destination folder"); rc = SKU_ERR_IO; goto out; }
	for (i = 0; i < nentries; i++) {
		char rel[SKU_PATH_MAX], dest[SKU_PATH_MAX], parent[SKU_PATH_MAX];
		size_t n;
		if (!next_entry(cd, cd_size, &off, &e)) { fail(err, en, "corrupt central directory"); rc = SKU_ERR_FORMAT; goto out; }
		sku_strlcpy(rel, e.name, sizeof rel);
		n = strlen(rel);
		if (n == 0) continue;
		if (rel[n - 1] == '/') rel[--n] = 0;
		if (n == 0) continue;
		if (sku_path_check_relative(rel) != SKU_OK) { fail(err, en, "unsafe path in the zip"); rc = SKU_ERR_UNSAFE; goto out; }
		sku_path_join(dest, sizeof dest, dest_dir, rel);
		if (e.name[strlen(e.name) - 1] == '/') { if (sku_mkdir_p(dest) != SKU_OK) { fail(err, en, "cannot create a folder"); rc = SKU_ERR_IO; goto out; } continue; }
		sku_path_dirname(parent, sizeof parent, dest);
		if (sku_mkdir_p(parent) != SKU_OK) { fail(err, en, "cannot create a folder"); rc = SKU_ERR_IO; goto out; }
		if ((rc = extract_entry(f, size, &e, dest, err, en)) != SKU_OK) goto out;
		/* Unix mode in the high 16 bits of the external attributes (zips made on Unix); a bundle's MacOS/ folder holds programs */
		if (((e.attrs >> 16) & 0111) || strstr(rel, "/MacOS/")) sku_set_executable(dest);
	}
	rc = SKU_OK;
out:
	free(cd);
	fclose(f);
	return rc;
}
