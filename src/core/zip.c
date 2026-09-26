/* Minimal zip reader, see zip.h. Central directory first (the authoritative
 * one), then the local header of the wanted entry, then zlib for deflate. */
#include "zip.h"
#include "platform.h"
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

int sku_zip_extract(const char *zip_path, const char *entry, const char *dest, char *err, size_t en)
{
	FILE *f, *out = NULL;
	int64_t size, eocd_off = -1, cd_off, off;
	unsigned char tail[65536 + 22], *cd = NULL, *in = NULL, *outbuf = NULL;
	size_t tail_len, cd_size, i, nentries, name_len = strlen(entry);
	uint32_t method = 0, comp = 0, uncomp = 0, want_crc = 0, got_crc = 0;
	int64_t local_off = -1;
	int rc = SKU_ERR_FORMAT;

	if (err && en) err[0] = 0;
	f = sku_fopen(zip_path, "rb");
	if (!f) { fail(err, en, "cannot open the zip"); return SKU_ERR_IO; }
	if (fseeko(f, 0, SEEK_END) != 0 || (size = (int64_t)ftello(f)) < 22) { fail(err, en, "not a zip file"); goto out; }

	/* end of central directory: last 22 bytes plus an optional comment of up to 64 KiB */
	tail_len = (size_t)(size < (int64_t)sizeof tail ? size : (int64_t)sizeof tail);
	if (!read_at(f, size - (int64_t)tail_len, tail, tail_len)) { fail(err, en, "cannot read the zip"); goto out; }
	for (i = tail_len - 22; ; i--) {
		if (rd32(tail + i) == SIG_EOCD) { eocd_off = (int64_t)i; break; }
		if (i == 0) break;
	}
	if (eocd_off < 0) { fail(err, en, "no central directory (not a zip, or zip64)"); goto out; }
	nentries = rd16(tail + eocd_off + 10);
	cd_size = rd32(tail + eocd_off + 12);
	cd_off = rd32(tail + eocd_off + 16);
	if (cd_size == 0 || cd_size > 64u * 1024 * 1024 || cd_off + (int64_t)cd_size > size) { fail(err, en, "bad central directory"); goto out; }
	cd = sku_xmalloc(cd_size);
	if (!read_at(f, cd_off, cd, cd_size)) { fail(err, en, "cannot read the central directory"); goto out; }

	/* find the entry */
	for (off = 0, i = 0; i < nentries && (size_t)off + 46 <= cd_size; i++) {
		const unsigned char *e = cd + off;
		size_t n, x, c;
		if (rd32(e) != SIG_CENTRAL) { fail(err, en, "corrupt central directory"); goto out; }
		n = rd16(e + 28); x = rd16(e + 30); c = rd16(e + 32);
		if ((size_t)off + 46 + n > cd_size) { fail(err, en, "corrupt central directory"); goto out; }
		if (n == name_len && memcmp(e + 46, entry, n) == 0) {
			method = rd16(e + 10); want_crc = rd32(e + 16); comp = rd32(e + 20); uncomp = rd32(e + 24); local_off = rd32(e + 42);
			break;
		}
		off += 46 + (int64_t)(n + x + c);
	}
	if (local_off < 0) { fail(err, en, "entry not found in the zip"); rc = SKU_ERR_NOTFOUND; goto out; }
	if (method != 0 && method != 8) { fail(err, en, "unsupported compression method"); goto out; }
	if (comp == 0xFFFFFFFFu || uncomp == 0xFFFFFFFFu) { fail(err, en, "zip64 entries are not supported"); goto out; }

	/* local header: skip its own name and extra field */
	{
		unsigned char lh[30];
		if (!read_at(f, local_off, lh, sizeof lh) || rd32(lh) != SIG_LOCAL) { fail(err, en, "corrupt local header"); goto out; }
		off = local_off + 30 + rd16(lh + 26) + rd16(lh + 28);
	}
	if (off + (int64_t)comp > size) { fail(err, en, "truncated zip"); goto out; }

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
	(void)uncomp;
	rc = SKU_OK;
out:
	if (out) fclose(out);
	if (rc != SKU_OK) sku_remove_file(dest);
	free(outbuf); free(in); free(cd);
	fclose(f);
	return rc;
}
