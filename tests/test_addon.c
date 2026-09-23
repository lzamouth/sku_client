/* End-to-end test: scan a fake X-Plane root, check and update one addon
 * against a depot served locally (see run_addon_test.sh).
 *   SKU_TEST_XP = fake X-Plane root, SKU_TEST_URL = depot base URL */
#include "test.h"
#include "sku.h"
#include "platform.h"
#include "paths.h"
#include "log.h"
#include "http.h"
#include "instance.h"
#include "scan.h"
#include "addon.h"
#include "report.h"
#include "crc32.h"

static const char *xp, *url;

static void set_module(const char *addon_dir, const char *level)
{
	/* points the local cfg at one of the depots */
	char p[SKU_PATH_MAX], text[512];
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater.cfg");
	sku_snprintf(text, sizeof text, "name|Test plane\nmodule|%s/%s\nversion|1.0\nlocked|false\ndisabled|false\nliveries|true\nzone|custom\n", url, level);
	sku_write_file_atomic(p, text, strlen(text));
}

static bool file_is(const char *addon_dir, const char *rel, const char *content)
{
	char p[SKU_PATH_MAX], *data;
	bool ok;
	sku_path_join(p, sizeof p, addon_dir, rel);
	if (sku_read_file(p, &data, NULL) != SKU_OK) return false;
	ok = strcmp(data, content) == 0;
	free(data);
	return ok;
}

static void test_level(const char *level, bool second_run_uses_cache)
{
	sku_instance in;
	sku_found_list found = {0};
	sku_addon *a;
	sku_check_opts co = { false, false, 2 };
	sku_update_opts uo = { false, 3, 1 };
	char addon_dir[SKU_PATH_MAX], p[SKU_PATH_MAX];
	int ndl = 0;

	sku_path_join(addon_dir, sizeof addon_dir, xp, "Aircraft/Test/Plane");
	set_module(addon_dir, level);

	CHECK_EQ_INT(sku_instance_open(&in, xp, false), SKU_OK);
	CHECK_EQ_INT(sku_scan_instance(&in, &found), SKU_OK);
	CHECK_EQ_INT(found.n, 1);                       /* Other/deep is hidden behind a .acf */
	CHECK_EQ_STR(found.v[0].dir, addon_dir);
	CHECK_EQ_INT(found.v[0].genre, SKU_GENRE_AIRCRAFT);

	a = sku_addon_open(&in, found.v[0].dir, found.v[0].genre);
	CHECK_EQ_STR(a->name, "Test plane");
	CHECK_EQ_STR(a->version, "1.0");
	CHECK_EQ_STR(a->channel, "stable");             /* beta file present but enabled|false */
	CHECK_EQ_INT(a->nchannels, 1);
	CHECK_EQ_STR(a->channels[0].name, "beta");
	CHECK_EQ_INT(a->ignore.n, 1);
	CHECK(!a->v4);

	/* --- check */
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK(a->checked);
	CHECK_EQ_INT(sku_addon_status(a, NULL, 0, NULL, 0), SKU_ST_UPDATE);
	CHECK_EQ_STR(a->remote_version, "2.0");
	CHECK_EQ_INT(a->level, strcmp(level, "level2") == 0 ? 2 : 1);
	CHECK_EQ_INT(a->files.n, 8);                    /* the ../../evil line was rejected */
	CHECK_EQ_INT(a->blacklist.n, 1);
	CHECK_EQ_INT(a->plan.n_new, 4);                 /* new.obj, OptionSave.prf, Blue livery, with space */
	CHECK_EQ_INT(a->plan.n_changed, 2);             /* wing.obj, Red livery */
	CHECK_EQ_INT(a->plan.n_delete, 1);
	CHECK_EQ_INT(a->plan.n_ignored, 1);             /* data/user.prf */
	CHECK_EQ_INT(a->plan.n_unchanged, 1);           /* plane.acf */
	CHECK_EQ_INT(a->plan.n_error, 0);
	if (strcmp(level, "level2") == 0) { CHECK_EQ_STR(a->changelog, "changelog.txt"); CHECK(a->plan.bytes > 0); }

	/* liveries off: the two livery files drop out of the plan */
	CHECK_EQ_INT(sku_addon_set_liveries(a, false, true), SKU_OK);
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_INT(a->plan.n_new, 3);
	CHECK_EQ_INT(a->plan.n_changed, 1);
	CHECK_EQ_INT(sku_addon_set_liveries(a, true, true), SKU_OK);

	/* --- update */
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_INT(sku_addon_update(a, &uo), SKU_OK);
	CHECK_EQ_INT(sku_addon_status(a, NULL, 0, NULL, 0), SKU_ST_UPDATED);
	CHECK(file_is(addon_dir, "objects/wing.obj", "new wing v2"));
	CHECK(file_is(addon_dir, "objects/new.obj", "brand new object"));
	CHECK(file_is(addon_dir, "liveries/Blue/tex.png", "blue livery"));
	CHECK(file_is(addon_dir, "sub dir/with space.txt", "spaces in names"));
	CHECK(file_is(addon_dir, "data/user.prf", "user data"));      /* ignored: untouched */
	CHECK(file_is(addon_dir, "plane.acf", "ACF v1"));
	sku_path_join(p, sizeof p, addon_dir, "objects/obsolete.png"); CHECK(!sku_exists(p));
	for (int i = 0; i < a->plan.n; i++) if (a->plan.v[i].reason != SKU_PLAN_IGNORED) { CHECK_EQ_INT(a->plan.v[i].result, SKU_OK); ndl++; }
	CHECK_EQ_INT(ndl, 7);

	/* v4 layout after the first save */
	CHECK(a->v4);
	CHECK_EQ_STR(a->version, "2.0");
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/updater.cfg");        CHECK(sku_is_file(p));
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/ignore.txt");         CHECK(sku_is_file(p));
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater.cfg");        CHECK(!sku_exists(p));
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater_beta.cfg");   CHECK(!sku_exists(p));
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater_ignore.txt"); CHECK(!sku_exists(p));
	{
		char beta[SKU_URL_MAX];
		sku_snprintf(beta, sizeof beta, "%s/beta", url);
		CHECK_EQ_STR(sku_cfg_get_or(&a->local, "channel.beta.module", "?"), beta);
	}
	CHECK_EQ_STR(sku_cfg_get_or(&a->local, "channel", "?"), "stable");

	/* --- second check: everything up to date, hashes come from the cache */
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_INT(sku_addon_status(a, NULL, 0, NULL, 0), SKU_ST_UP_TO_DATE);
	CHECK_EQ_INT(a->plan.n_new + a->plan.n_changed + a->plan.n_delete, 0);
	if (second_run_uses_cache) CHECK(a->plan.n_cached >= 5);
	CHECK_EQ_INT(sku_report_save(a, "Check"), SKU_OK);
	sku_instance_report_path(&in, a->dir, p, sizeof p); CHECK(sku_is_file(p));

	/* --- a full check ignores the cache and finds the same result */
	co.full = true;
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_INT(a->plan.n_cached, 0);
	CHECK_EQ_INT(sku_addon_status(a, NULL, 0, NULL, 0), SKU_ST_UP_TO_DATE);

	/* --- corrupt a file: a fast check must notice (mtime/size changed) */
	sku_path_join(p, sizeof p, addon_dir, "objects/new.obj");
	sku_write_file_atomic(p, "corrupted!", 10);
	co.full = false;
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_INT(sku_addon_status(a, NULL, 0, NULL, 0), SKU_ST_REPAIR);
	CHECK_EQ_INT(a->plan.n_changed, 1);
	CHECK_EQ_INT(sku_addon_update(a, &uo), SKU_OK);
	CHECK(file_is(addon_dir, "objects/new.obj", "brand new object"));

	/* --- channel switch */
	CHECK_EQ_INT(sku_addon_set_channel(a, "nope", false), SKU_ERR_NOTFOUND);
	CHECK_EQ_INT(sku_addon_set_channel(a, "beta", false), SKU_OK);
	CHECK_EQ_STR(a->channel, "beta");
	CHECK(strstr(a->base_url, "/beta/") != NULL);
	CHECK_EQ_INT(sku_addon_set_channel(a, "stable", false), SKU_OK);

	/* --- disabled addon: check does nothing */
	CHECK_EQ_INT(sku_addon_set_disabled(a, true, false), SKU_OK);
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_INT(sku_addon_status(a, NULL, 0, NULL, 0), SKU_ST_DISABLED);
	CHECK_EQ_INT(sku_addon_set_disabled(a, false, false), SKU_OK);

	sku_addon_close(a);
	sku_found_list_free(&found);
	sku_instance_close(&in);
}

static void test_errors(void)
{
	sku_instance in;
	sku_addon *a;
	sku_check_opts co = { false, false, 1 };
	char addon_dir[SKU_PATH_MAX], text[256], detail[512];

	sku_path_join(addon_dir, sizeof addon_dir, xp, "Aircraft/Test/Plane");
	CHECK_EQ_INT(sku_instance_open(&in, xp, false), SKU_OK);
	a = sku_addon_open(&in, addon_dir, SKU_GENRE_AIRCRAFT);

	/* locked depot */
	sku_cfg_set(&a->local, "module", "http://127.0.0.1:0/x"); /* overwritten below */
	{
		char m[SKU_URL_MAX];
		sku_snprintf(m, sizeof m, "%s/locked", url);
		sku_cfg_set(&a->local, "module", m);
		sku_addon_save(a);
	}
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_INT(sku_addon_status(a, text, sizeof text, detail, sizeof detail), SKU_ST_LOCKED);
	CHECK(!a->checked);

	/* HTML instead of a cfg */
	{
		char m[SKU_URL_MAX];
		sku_snprintf(m, sizeof m, "%s/html", url);
		sku_cfg_set(&a->local, "module", m);
		sku_addon_save(a);
	}
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_ERR_FORMAT);
	CHECK_EQ_INT(sku_addon_status(a, text, sizeof text, detail, sizeof detail), SKU_ST_ERROR_NET);
	CHECK(strstr(detail, "web page") != NULL);

	/* 404 */
	{
		char m[SKU_URL_MAX];
		sku_snprintf(m, sizeof m, "%s/missing", url);
		sku_cfg_set(&a->local, "module", m);
		sku_addon_save(a);
	}
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_ERR_NET);
	CHECK_EQ_INT(sku_addon_status(a, text, sizeof text, detail, sizeof detail), SKU_ST_ERROR_NET);
	CHECK(strstr(detail, "404") != NULL);

	sku_addon_close(a);
	sku_instance_close(&in);
}

int main(void)
{
	xp = getenv("SKU_TEST_XP"); url = getenv("SKU_TEST_URL");
	if (!xp || !url) { fprintf(stderr, "SKU_TEST_XP / SKU_TEST_URL not set\n"); return 2; }
	sku_log_set_level(SKU_LOG_DEBUG);
	sku_http_global_init(NULL, 0, "sku-test");
	test_level(getenv("SKU_TEST_LEVEL") ? getenv("SKU_TEST_LEVEL") : "level1", true);
	test_errors();
	sku_http_global_cleanup();
	TEST_MAIN_END();
}
