/* HTTP tests against a local server (see run_http_test.sh):
 *   SKU_TEST_URL = base URL of the fixture dir, SKU_TEST_DIR = a scratch dir. */
#include "test.h"
#include "sku.h"
#include "http.h"
#include "platform.h"
#include "crc32.h"
#include "paths.h"

static int progress_calls;
static int on_progress(void *user, int64_t got, int64_t total)
{
	(void)user; (void)got; (void)total;
	progress_calls++;
	return 0;
}

int main(void)
{
	const char *base = getenv("SKU_TEST_URL"), *dir = getenv("SKU_TEST_DIR");
	sku_http *h;
	sku_http_result r;
	char url[SKU_URL_MAX], dest[SKU_PATH_MAX], *body;
	size_t len;
	uint32_t crc;
	atomic_int cancel;

	if (!base || !dir) { fprintf(stderr, "SKU_TEST_URL / SKU_TEST_DIR not set\n"); return 2; }
	CHECK_EQ_INT(sku_http_global_init(NULL, 0, "sku-test"), SKU_OK);
	h = sku_http_new();
	CHECK(h != NULL);

	/* text file into memory */
	sku_snprintf(url, sizeof url, "%s/skunkcrafts_updater.cfg", base);
	CHECK_EQ_INT(sku_http_get_mem(h, url, NULL, NULL, &body, &len, &r), SKU_OK);
	CHECK_EQ_INT(r.status, 200);
	CHECK(body && strstr(body, "name|Test") != NULL);
	CHECK_EQ_INT(len, r.bytes);
	CHECK(!sku_http_looks_like_html(body, len, r.content_type));
	CHECK(r.last_modified[0] != 0);
	free(body);
	/* conditional GET: python's server honours If-Modified-Since */
	{
		char since[64];
		sku_strlcpy(since, r.last_modified, sizeof since);
		CHECK_EQ_INT(sku_http_get_mem(h, url, NULL, since, &body, &len, &r), SKU_OK);
		CHECK(r.not_modified);
		CHECK_EQ_INT(r.status, 304);
		CHECK(body == NULL);
		CHECK_EQ_INT(sku_http_get_mem(h, url, NULL, "Thu, 01 Jan 1970 00:00:00 GMT", &body, &len, &r), SKU_OK);
		CHECK(!r.not_modified);
		CHECK_EQ_INT(r.status, 200);
		free(body);
	}

	/* 404 */
	sku_snprintf(url, sizeof url, "%s/missing.txt", base);
	CHECK_EQ_INT(sku_http_get_mem(h, url, NULL, NULL, &body, &len, &r), SKU_ERR_NET);
	CHECK_EQ_INT(r.status, 404);
	CHECK(body == NULL);
	CHECK(strstr(r.error, "404") != NULL);

	/* an HTML page where a protocol file is expected */
	sku_snprintf(url, sizeof url, "%s/page.html", base);
	CHECK_EQ_INT(sku_http_get_mem(h, url, NULL, NULL, &body, &len, &r), SKU_OK);
	CHECK(sku_http_looks_like_html(body, len, r.content_type));
	free(body);
	CHECK(sku_http_looks_like_html("  <!doctype html><p>", 20, "application/octet-stream"));
	CHECK(!sku_http_looks_like_html("name|x", 6, "text/plain"));

	/* binary file to disk, with size check and CRC */
	sku_snprintf(url, sizeof url, "%s/objects/a.bin", base);
	sku_snprintf(dest, sizeof dest, "%s/dl/objects/a.bin", dir);
	progress_calls = 0;
	atomic_init(&cancel, 0);
	CHECK_EQ_INT(sku_http_get_file(h, url, dest, 9, on_progress, NULL, &cancel, &r), SKU_OK);
	CHECK(progress_calls > 0);
	CHECK(sku_is_file(dest));
	CHECK_EQ_INT(sku_crc32_file(dest, &crc, NULL), SKU_OK);
	CHECK_EQ_INT(crc, 0xCBF43926);
	{
		char part[SKU_PATH_MAX];
		sku_snprintf(part, sizeof part, "%s.skupart", dest);
		CHECK(!sku_exists(part));
	}
	/* wrong expected size: rejected, the previous file is left untouched */
	CHECK_EQ_INT(sku_http_get_file(h, url, dest, 10, NULL, NULL, NULL, &r), SKU_ERR_NET);
	CHECK(strstr(r.error, "size mismatch") != NULL);
	CHECK(sku_is_file(dest));
	/* 404 to disk: nothing written */
	sku_snprintf(url, sizeof url, "%s/objects/none.bin", base);
	sku_snprintf(dest, sizeof dest, "%s/dl/objects/none.bin", dir);
	CHECK_EQ_INT(sku_http_get_file(h, url, dest, -1, NULL, NULL, NULL, &r), SKU_ERR_NET);
	CHECK_EQ_INT(r.status, 404);
	CHECK(!sku_exists(dest));
	/* cancelled before start */
	atomic_store(&cancel, 1);
	sku_snprintf(url, sizeof url, "%s/objects/a.bin", base);
	CHECK_EQ_INT(sku_http_get_file(h, url, dest, -1, NULL, NULL, &cancel, &r), SKU_ERR_CANCELLED);
	CHECK(!sku_exists(dest));

	sku_http_free(h);
	sku_http_global_cleanup();
	TEST_MAIN_END();
}
