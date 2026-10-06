/* Unit tests: strmap, protocol parsers, crc cache, thread pool. */
#include "test.h"
#include "sku.h"
#include "strmap.h"
#include "protocol.h"
#include "crc_cache.h"
#include "worker.h"
#include "platform.h"
#include "paths.h"
#include "changelog.h"

static void test_strmap(void)
{
	sku_strmap m;
	sku_strmap_iter it;
	int count = 0;
	char key[32];
	sku_strmap_init(&m, 0);
	for (int i = 0; i < 1000; i++) { sku_snprintf(key, sizeof key, "k%d", i); sku_strmap_put(&m, key)->num = i; }
	CHECK_EQ_INT(m.count, 1000);
	CHECK_EQ_INT(sku_strmap_get(&m, "k500")->num, 500);
	CHECK(sku_strmap_get(&m, "nope") == NULL);
	CHECK(sku_strmap_remove(&m, "k500"));
	CHECK(!sku_strmap_remove(&m, "k500"));
	for (sku_strmap_node *n = sku_strmap_first(&m, &it); n; n = sku_strmap_next(&m, &it)) count++;
	CHECK_EQ_INT(count, 999);
	sku_strmap_free(&m);
}

static void test_parsers(void)
{
	sku_remote_files f = {0};
	sku_strlist l = {0};
	int rejected = -1;
	char url[SKU_URL_MAX];

	sku_protocol_parse_whitelist("airfoils/a.afl|3465057978 \r\ndata/x.prf|-1\n\n# c\n../evil|1\nbad|notanumber\nbig|4294967296\n", &f, &rejected);
	CHECK_EQ_INT(f.n, 2);
	CHECK_EQ_INT(rejected, 3);
	CHECK_EQ_STR(f.v[0].path, "airfoils/a.afl");
	CHECK_EQ_INT(f.v[0].crc, 3465057978u); CHECK(f.v[0].check_crc); CHECK_EQ_INT(f.v[0].size, -1);
	CHECK(!f.v[1].check_crc);
	sku_protocol_apply_sizes("airfoils/a.afl|204035\nunknown|5\n", &f);
	CHECK_EQ_INT(f.v[0].size, 204035); CHECK_EQ_INT(f.v[1].size, -1);
	sku_remote_files_free(&f);

	sku_protocol_parse_manifest("objects\\a.obj|12345|678|future-field\nliveries/x|-1|-1\nshort|1\n", &f, &rejected);
	CHECK_EQ_INT(f.n, 2); CHECK_EQ_INT(rejected, 1);
	CHECK_EQ_STR(f.v[0].path, "objects/a.obj"); CHECK_EQ_INT(f.v[0].size, 678); CHECK_EQ_INT(f.v[0].crc, 12345);
	CHECK(!f.v[1].check_crc); CHECK_EQ_INT(f.v[1].size, -1);
	sku_remote_files_free(&f);

	sku_protocol_parse_pathlist("\xEF\xBB\xBFobjects/cabin/CabinAft.png\r\n\r\n/etc/passwd\nplugins/../x\rold/mac.txt \r\n", &l, &rejected);
	CHECK_EQ_INT(l.n, 2); CHECK_EQ_INT(rejected, 2);
	CHECK_EQ_STR(l.v[0], "objects/cabin/CabinAft.png");
	CHECK_EQ_STR(l.v[1], "old/mac.txt");
	sku_strlist_free(&l);

	sku_protocol_base_url(url, sizeof url, "custom", "https://flyjsim.b-cdn.net/updater/Q4XP12");
	CHECK_EQ_STR(url, "https://flyjsim.b-cdn.net/updater/Q4XP12/");
	sku_protocol_base_url(url, sizeof url, "custom", "http://updates.toliss.com/aircraft-repositories/A320//");
	CHECK_EQ_STR(url, "http://updates.toliss.com/aircraft-repositories/A320/");
	sku_protocol_base_url(url, sizeof url, "eu", "vendor/plane");
	CHECK_EQ_STR(url, "http://eu-ccd.kxcdn.com/vendor/plane/");
	sku_protocol_base_url(url, sizeof url, NULL, "https://x/");
	CHECK_EQ_STR(url, "https://x/");
	/* Thranda's former servers, rewritten as 3.x does */
	sku_protocol_base_url(url, sizeof url, "custom", "http://72.2.11.3:8080/updates/Kodiak");
	CHECK_EQ_STR(url, "http://updates.thranda.org/updates/Kodiak/");
	sku_protocol_base_url(url, sizeof url, "custom", "http://72.2.11.34:8080/x/");
	CHECK_EQ_STR(url, "http://updates.thranda.org/x/");
	sku_protocol_base_url(url, sizeof url, "eu", "72.2.11.3:8080/y");
	CHECK_EQ_STR(url, "http://eu-ccd.kxcdn.com/updates.thranda.org/y/");
	sku_protocol_base_url(url, sizeof url, "custom", "http://72.2.11.30:8080/z");   /* another address: untouched */
	CHECK_EQ_STR(url, "http://72.2.11.30:8080/z/");

	{
		sku_cfg c; char err[128];
		sku_cfg_init(&c);
		sku_cfg_parse(&c, "name|X\nmodule|https://x\n");
		CHECK_EQ_INT(sku_protocol_check_remote_cfg(&c, err, sizeof err), SKU_ERR_FORMAT);
		CHECK(strstr(err, "version") != NULL);
		sku_cfg_set(&c, "version", "1");
		CHECK_EQ_INT(sku_protocol_check_remote_cfg(&c, err, sizeof err), SKU_OK);   /* "locked" is optional: absent = unlocked */
		CHECK(!sku_cfg_get_bool(&c, "locked", false));
		sku_cfg_set(&c, "locked", "false");
		CHECK_EQ_INT(sku_protocol_check_remote_cfg(&c, err, sizeof err), SKU_OK);
		CHECK_EQ_INT(sku_protocol_level(&c), 1);
		sku_cfg_set(&c, "protocol", "2");
		CHECK_EQ_INT(sku_protocol_level(&c), 2);
		sku_cfg_free(&c);
	}
}

static void test_private_paths(void)
{
	sku_remote_files f = {0};
	sku_strlist l = {0};
	int rejected = 0;
	sku_protocol_parse_whitelist("ok.txt|1\nskunkcrafts/updater.cfg|2\nSkunkcrafts/crc_cache.txt|3\n", &f, &rejected);
	CHECK_EQ_INT(f.n, 1); CHECK_EQ_INT(rejected, 2);
	sku_remote_files_free(&f);
	rejected = 0;
	sku_protocol_parse_pathlist("skunkcrafts/ignore.txt\nold.obj\n", &l, &rejected);
	CHECK_EQ_INT(l.n, 1); CHECK_EQ_INT(rejected, 1);
	sku_strlist_free(&l);
}

static void test_crc_cache(void)
{
	char path[SKU_PATH_MAX];
	sku_crc_cache *c;
	uint32_t crc = 0;
	sku_snprintf(path, sizeof path, "/tmp/sku_cache_%d.crc", (int)sku_time_now());
	c = sku_crc_cache_open(path);
	CHECK_EQ_INT(sku_crc_cache_count(c), 0);
	sku_crc_cache_store(c, "a/b.obj", 100, 1700000000, 0xDEADBEEF);
	sku_crc_cache_store(c, "stale.obj", 1, 1, 1);
	CHECK(sku_crc_cache_lookup(c, "a/b.obj", 100, 1700000000, &crc)); CHECK_EQ_INT(crc, 0xDEADBEEF);
	CHECK(!sku_crc_cache_lookup(c, "a/b.obj", 101, 1700000000, &crc));   /* size changed */
	CHECK(!sku_crc_cache_lookup(c, "a/b.obj", 100, 1700000001, &crc));   /* mtime changed */
	CHECK(!sku_crc_cache_lookup(c, "nope", 100, 1700000000, &crc));
	CHECK_EQ_INT(sku_crc_cache_save(c, true), SKU_OK);
	sku_crc_cache_close(c);

	c = sku_crc_cache_open(path);
	CHECK_EQ_INT(sku_crc_cache_count(c), 2);
	CHECK(sku_crc_cache_lookup(c, "a/b.obj", 100, 1700000000, &crc)); CHECK_EQ_INT(crc, 0xDEADBEEF);
	CHECK_EQ_INT(sku_crc_cache_save(c, false), SKU_OK);   /* no prune: everything kept */
	sku_crc_cache_close(c);
	c = sku_crc_cache_open(path);
	CHECK_EQ_INT(sku_crc_cache_count(c), 2);
	CHECK(sku_crc_cache_lookup(c, "a/b.obj", 100, 1700000000, &crc));
	CHECK_EQ_INT(sku_crc_cache_save(c, true), SKU_OK);    /* prune: "stale.obj" was not touched, dropped */
	sku_crc_cache_close(c);
	c = sku_crc_cache_open(path);
	CHECK_EQ_INT(sku_crc_cache_count(c), 1);
	sku_crc_cache_forget(c, "a/b.obj");
	CHECK_EQ_INT(sku_crc_cache_count(c), 0);
	sku_crc_cache_close(c);
	sku_remove_file(path);
}

static atomic_int sum;
static atomic_int cancel_flag;

static void job(int i, int thread, void *user)
{
	(void)user; (void)thread;
	atomic_fetch_add(&sum, i);
	if (i == 50) atomic_store(&cancel_flag, 1);
	sku_sleep_ms(1);
}

static void test_pool(void)
{
	atomic_store(&sum, 0);
	CHECK_EQ_INT(sku_pool_run(4, 100, job, NULL, NULL), SKU_OK);
	CHECK_EQ_INT(atomic_load(&sum), 4950);
	CHECK_EQ_INT(sku_pool_run(4, 0, job, NULL, NULL), SKU_OK);
	atomic_store(&sum, 0); atomic_store(&cancel_flag, 0);
	CHECK_EQ_INT(sku_pool_run(2, 1000, job, NULL, &cancel_flag), SKU_ERR_CANCELLED);
	CHECK(atomic_load(&sum) < 4950 * 10);   /* most jobs skipped */
}

static void test_changelog_headers(void)
{
	char v[64];
	CHECK(sku_changelog_is_header("## 1.39.3", v, sizeof v)); CHECK_EQ_STR(v, "1.39.3");
	CHECK(sku_changelog_is_header("v1.2.11.0", v, sizeof v)); CHECK_EQ_STR(v, "1.2.11.0");
	CHECK(sku_changelog_is_header("12.2r1  ", v, sizeof v)); CHECK_EQ_STR(v, "12.2r1");
	CHECK(sku_changelog_is_header("### 1.0.0_a75", v, sizeof v)); CHECK_EQ_STR(v, "1.0.0_a75");
	CHECK(!sku_changelog_is_header("- fixed 3 bugs", v, sizeof v));
	CHECK(!sku_changelog_is_header("2.0 released today", v, sizeof v));
	CHECK(!sku_changelog_is_header("version", v, sizeof v));
	CHECK(!sku_changelog_is_header("", v, sizeof v));
}

int main(void)
{
	test_changelog_headers();
	test_strmap();
	test_parsers();
	test_crc_cache();
	test_private_paths();
	test_pool();
	TEST_MAIN_END();
}
