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
#include "pack.h"
#include "changelog.h"
#include "client.h"
#include "zip.h"
#include <unistd.h>

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

/* a file that is not a zip, for the error path */
static const char *target_dummy_path(const char *dir, char *out, size_t n)
{
	sku_path_join(out, n, dir, "notazip.txt");
	sku_write_file_atomic(out, "plain text, not a zip archive", 29);
	return out;
}

static void test_client(void)
{
	sku_client_info info;
	char depot[SKU_URL_MAX], err[256], dir[SKU_PATH_MAX], dest[SKU_PATH_MAX];

	/* version order */
	CHECK(sku_client_version_cmp("4.0.0", "4.0.0") == 0);
	CHECK(sku_client_version_cmp("v4.0.0", "4.0.0") == 0);
	CHECK(sku_client_version_cmp("4.0.1", "4.0.0") > 0);
	CHECK(sku_client_version_cmp("4.1", "4.0.9") > 0);
	CHECK(sku_client_version_cmp("4.0.0-beta2", "4.0.0-beta1") > 0);
	CHECK(sku_client_version_cmp("4.0.0", "4.0.0-beta2") > 0);        /* release after its betas */
	CHECK(sku_client_version_cmp("4.0.0-rc1", "4.0.0-beta9") > 0);    /* beta < rc */
	CHECK(sku_client_version_cmp("4.0.0-dev", "4.0.0") < 0);
	CHECK(sku_client_version_cmp("10.0.0", "9.9.9") > 0);
	CHECK_EQ_STR(sku_client_package(), SKU_PKG_LINUX_ZIP);            /* tests run on Linux, not as an AppImage */

	/* a newer version is published: package found, changelog fetched */
	sku_snprintf(depot, sizeof depot, "%s/client", url);
	CHECK_EQ_INT(sku_client_check(depot, NULL, &info, err, sizeof err), SKU_OK);
	CHECK_EQ_STR(info.version, "9.9.9");
	CHECK(info.newer);
	CHECK(!info.locked);
	CHECK_EQ_STR(info.package, "SkunkcraftsUpdater-9.9.9-linux.zip");   /* the versioned name wins */
	CHECK(info.check_crc);
	CHECK(info.size > 0);
	CHECK(info.changelog && strstr(info.changelog, "self update"));
	CHECK(sku_str_ends(info.module, "/client/"));
	sku_path_join(dir, sizeof dir, xp, "Output/client_downloads");
	CHECK_EQ_INT(sku_client_download(depot, &info, dir, NULL, NULL, NULL, dest, sizeof dest, err, sizeof err), SKU_OK);
	CHECK(sku_str_ends(dest, "SkunkcraftsUpdater-9.9.9-linux.zip"));
	/* the Linux package is a zip: stored and deflated entries, CRC checked, missing entry reported */
	{
		char out[SKU_PATH_MAX], *content;
		size_t len;
		sku_path_join(out, sizeof out, dir, "README.txt");
		CHECK_EQ_INT(sku_zip_extract(dest, "README.txt", out, err, sizeof err), SKU_OK);
		CHECK_EQ_INT(sku_read_file(out, &content, NULL), SKU_OK); CHECK_EQ_STR(content, "readme 9.9.9"); free(content);
		sku_path_join(out, sizeof out, dir, "prog.bin");
		CHECK_EQ_INT(sku_zip_extract(dest, "SkunkcraftsUpdater_lin", out, err, sizeof err), SKU_OK);
		CHECK_EQ_INT(sku_read_file(out, &content, &len), SKU_OK); CHECK_EQ_INT(len, 200 * strlen("new program 9.9.9 ")); CHECK(strncmp(content, "new program 9.9.9 ", 18) == 0); free(content);
		sku_path_join(out, sizeof out, dir, "nope.bin");
		CHECK_EQ_INT(sku_zip_extract(dest, "nope", out, err, sizeof err), SKU_ERR_NOTFOUND);
		CHECK(!sku_exists(out));
		CHECK_EQ_INT(sku_zip_extract(target_dummy_path(dir, out, sizeof out), "x", out, err, sizeof err) == SKU_OK ? 1 : 0, 0);   /* not a zip */
	}
	sku_client_info_free(&info);

	/* in-place install from the zip: the program inside replaces the target, made executable; the zip is removed */
	{
		char target[SKU_PATH_MAX], *content;
		sku_path_join(target, sizeof target, dir, "running_program");
		CHECK_EQ_INT(sku_write_file_atomic(target, "old program", 11), SKU_OK);
		CHECK_EQ_INT(sku_client_install_from_zip(dest, "SkunkcraftsUpdater_lin", target, err, sizeof err), SKU_OK);
		CHECK(!sku_exists(dest));
		CHECK_EQ_INT(sku_read_file(target, &content, NULL), SKU_OK);
		CHECK(strncmp(content, "new program 9.9.9 ", 18) == 0);
		CHECK(access(target, X_OK) == 0);
		free(content);
		sku_client_install_file("/nonexistent/file", target, err, sizeof err);
		CHECK(err[0]);
	}

	/* macOS bundle: the zip is extracted as a tree and swapped with the running bundle, kept as .old */
	{
		char bundle[SKU_PATH_MAX], old[SKU_PATH_MAX], exe[SKU_PATH_MAX], zipdest[SKU_PATH_MAX], *content;
		CHECK_EQ_INT(sku_client_check(depot, SKU_PKG_MACOS_ZIP, &info, err, sizeof err), SKU_OK);
		CHECK_EQ_STR(info.package, "SkunkcraftsUpdater-9.9.9-macos.zip");
		CHECK_EQ_INT(sku_client_download(depot, &info, dir, NULL, NULL, NULL, zipdest, sizeof zipdest, err, sizeof err), SKU_OK);
		sku_client_info_free(&info);
		sku_path_join(bundle, sizeof bundle, dir, "Apps/SkunkcraftsUpdater.app");
		sku_path_join(exe, sizeof exe, bundle, "Contents/MacOS/SkunkcraftsUpdater");
		{
			char parent[SKU_PATH_MAX], found[SKU_PATH_MAX];
			sku_path_dirname(parent, sizeof parent, exe); sku_mkdir_p(parent);
			CHECK_EQ_INT(sku_write_file_atomic(exe, "old bundle program", 18), SKU_OK);
			CHECK_EQ_INT(sku_client_bundle_of(exe, found, sizeof found), SKU_OK);
			CHECK_EQ_STR(found, bundle);
			CHECK_EQ_INT(sku_client_bundle_of("/usr/bin/x", found, sizeof found), SKU_ERR_NOTFOUND);
		}
		CHECK_EQ_INT(sku_client_install_bundle(zipdest, bundle, err, sizeof err), SKU_OK);
		CHECK(!sku_exists(zipdest));
		CHECK_EQ_INT(sku_read_file(exe, &content, NULL), SKU_OK);
		CHECK(strncmp(content, "new bundle program 9.9.9 ", 25) == 0); free(content);
		CHECK(access(exe, X_OK) == 0);
		sku_path_join(old, sizeof old, dir, "Apps/SkunkcraftsUpdater.app.old/Contents/MacOS/SkunkcraftsUpdater");
		CHECK_EQ_INT(sku_read_file(old, &content, NULL), SKU_OK);
		CHECK_EQ_STR(content, "old bundle program"); free(content);
		sku_path_join(old, sizeof old, dir, "Apps/SkunkcraftsUpdater.app/Contents/Info.plist"); CHECK(sku_is_file(old));
		sku_client_cleanup_old(exe);                                    /* the next start drops the .old bundle */
		sku_path_join(old, sizeof old, dir, "Apps/SkunkcraftsUpdater.app.old"); CHECK(!sku_exists(old));
	}

	/* an unknown package name: the check still reports the version */
	CHECK_EQ_INT(sku_client_check(depot, "nope.zip", &info, err, sizeof err), SKU_OK);
	CHECK_EQ_STR(info.package, "");
	CHECK_EQ_INT(sku_client_download(depot, &info, dir, NULL, NULL, NULL, dest, sizeof dest, err, sizeof err), SKU_ERR_NOTFOUND);
	sku_client_info_free(&info);

	/* an older depot with plain names: found through the fallback; and not newer */
	sku_snprintf(depot, sizeof depot, "%s/client_old", url);
	CHECK_EQ_INT(sku_client_check(depot, NULL, &info, err, sizeof err), SKU_OK);
	CHECK(!info.newer);
	CHECK_EQ_STR(info.package, SKU_PKG_LINUX_ZIP);
	sku_client_info_free(&info);
	{
		char v[128];
		sku_client_versioned_name(SKU_PKG_WINDOWS_SETUP, "4.1.0", v, sizeof v); CHECK_EQ_STR(v, "SkunkcraftsUpdater-4.1.0-windows-setup.exe");
		sku_client_versioned_name(SKU_PKG_CLI_ZIP, "4.1.0", v, sizeof v);       CHECK_EQ_STR(v, "sku-cli-4.1.0.zip");
	}

	/* a web page instead of the depot */
	sku_snprintf(depot, sizeof depot, "%s/html", url);
	CHECK(sku_client_check(depot, NULL, &info, err, sizeof err) != SKU_OK);
	CHECK(err[0]);
	sku_client_info_free(&info);
}

static void test_level(const char *level, bool second_run_uses_cache)
{
	sku_instance in;
	sku_found_list found = {0};
	sku_addon *a;
	sku_check_opts co = { false, false, 2 };
	sku_update_opts uo = { false, 3, 1 };
	char addon_dir[SKU_PATH_MAX], p[SKU_PATH_MAX], q_channel_file[SKU_PATH_MAX];
	int ndl = 0;

	sku_path_join(addon_dir, sizeof addon_dir, xp, "Aircraft/Test/Plane");
	set_module(addon_dir, level);

	CHECK_EQ_INT(sku_instance_open(&in, xp, false), SKU_OK);
	CHECK_EQ_INT(sku_scan_instance(&in, &found), SKU_OK);
	CHECK_EQ_INT(found.n, 3);                       /* the plane + 2 pack members; Other/deep is hidden behind a .acf */
	{
		int plane = -1;
		for (int i = 0; i < found.n; i++) if (found.v[i].genre == SKU_GENRE_AIRCRAFT) plane = i;
		CHECK(plane >= 0);
		CHECK_EQ_STR(found.v[plane].dir, addon_dir);
		a = sku_addon_open(&in, found.v[plane].dir, found.v[plane].genre);
	}
	CHECK_EQ_STR(a->name, "Test plane");
	CHECK_EQ_STR(a->version, "1.0");
	CHECK_EQ_STR(a->channel, "stable");             /* beta file present but enabled|false */
	CHECK_EQ_INT(a->nchannels, 1);
	CHECK_EQ_STR(a->channels[0].name, "beta");
	CHECK_EQ_INT(a->ignore.n, 1);
	CHECK(!a->legacy_v4);
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater_beta.cfg");   CHECK(!sku_exists(p));   /* absorbed at load */

	/* --- check */
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK(a->checked);
	CHECK_EQ_INT(sku_addon_status(a, NULL, 0, NULL, 0), SKU_ST_UPDATE);
	CHECK_EQ_STR(a->remote_version, "2.0");
	CHECK_EQ_INT(a->level, strcmp(level, "level2") == 0 ? 2 : 1);
	CHECK_EQ_INT(a->files.n, 9);                    /* the ../../evil line was rejected */
	CHECK_EQ_INT(a->blacklist.n, 1);
	CHECK_EQ_INT(a->plan.n_new, 5);                 /* new.obj, OptionSave.prf, Blue livery, with space, changelog */
	CHECK_EQ_INT(a->plan.n_changed, 2);             /* wing.obj, Red livery */
	CHECK_EQ_INT(a->plan.n_delete, 1);
	CHECK_EQ_INT(a->plan.n_ignored, 1);             /* data/user.prf */
	CHECK_EQ_INT(a->plan.n_unchanged, 1);           /* plane.acf */
	CHECK_EQ_INT(a->plan.n_error, 0);
	CHECK(!a->no_query);                           /* a depot that accepts query strings keeps them */
	if (strcmp(level, "level2") == 0) {
		char doc[SKU_PATH_MAX], *text;
		sku_changelog_section *sec;
		int ns;
		CHECK_EQ_STR(a->changelog, "docs/CHANGELOG.md");
		CHECK_EQ_STR(a->readme, "readme.txt");
		CHECK(a->plan.bytes > 0);
		/* both documents were fetched into the instance cache */
		CHECK_EQ_INT(sku_addon_doc_path(a, "changelog", doc, sizeof doc), SKU_OK);
		CHECK(strstr(doc, "/Output/skunkcrafts/updater/cache/") != NULL);
		CHECK_EQ_INT(sku_read_file(doc, &text, NULL), SKU_OK);
		ns = sku_changelog_sections(text, a->version, &sec);
		CHECK_EQ_INT(ns, 4);                              /* "# Test plane" preamble, 2.0, 1.5, 1.0 */
		CHECK_EQ_STR(sec[0].version, ""); CHECK(!sec[0].newer);
		CHECK_EQ_STR(sec[1].version, "2.0"); CHECK(sec[1].newer);
		CHECK_EQ_STR(sec[2].version, "1.5"); CHECK(sec[2].newer);
		CHECK_EQ_STR(sec[3].version, "1.0"); CHECK(!sec[3].newer);
		CHECK_EQ_INT(sec[1].first_line, 2); CHECK_EQ_INT(sec[1].last_line, 5);
		free(sec); free(text);
		CHECK_EQ_INT(sku_addon_doc_path(a, "readme", doc, sizeof doc), SKU_OK);
	} else {
		/* level 1: the changelog is published under its default name, so it is fetched too */
		char doc[SKU_PATH_MAX], *text;
		sku_changelog_section *sec;
		CHECK_EQ_INT(sku_addon_doc_path(a, "changelog", doc, sizeof doc), SKU_OK);
		CHECK(sku_str_ends(doc, ".changelog.txt"));
		CHECK_EQ_INT(sku_read_file(doc, &text, NULL), SKU_OK);
		CHECK_EQ_INT(sku_changelog_sections(text, a->version, &sec), 2);   /* bare "2.0" / "1.0" headers */
		CHECK_EQ_STR(sec[0].version, "2.0"); CHECK(sec[0].newer);
		CHECK_EQ_STR(sec[1].version, "1.0"); CHECK(!sec[1].newer);
		free(sec); free(text);
		CHECK_EQ_INT(sku_addon_doc_path(a, "readme", doc, sizeof doc), SKU_ERR_NOTFOUND);   /* not published at level 1 */
	}

	/* liveries off: the two livery files drop out of the plan */
	CHECK_EQ_INT(sku_addon_set_liveries(a, false, true), SKU_OK);
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_INT(a->plan.n_new, 4);
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
	CHECK_EQ_INT(ndl, 8);

	/* level 1: the changelog now also sits in the addon; the cached copy is preferred,
	 * the shipped one is the fallback when the cache is gone */
	if (strcmp(level, "level1") == 0) {
		char doc[SKU_PATH_MAX], *text;
		sku_changelog_section *sec;
		CHECK_EQ_INT(sku_addon_doc_path(a, "changelog", doc, sizeof doc), SKU_OK);
		CHECK(sku_str_ends(doc, ".changelog.txt"));
		sku_remove_file(doc);
		CHECK_EQ_INT(sku_addon_doc_path(a, "changelog", doc, sizeof doc), SKU_OK);
		CHECK(sku_str_ends(doc, "Plane/changelog.txt"));
		CHECK_EQ_INT(sku_read_file(doc, &text, NULL), SKU_OK);
		CHECK_EQ_INT(sku_changelog_sections(text, "2.0", &sec), 2);   /* bare "2.0" / "1.0" headers */
		CHECK_EQ_STR(sec[0].version, "2.0"); CHECK(!sec[0].newer);      /* installed = newest: nothing newer */
		free(sec); free(text);
	}

	/* the CRC cache lives in the addon's private folder */
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/crc_cache.txt"); CHECK(sku_is_file(p));

	/* the configuration stayed where 3.x, aircraft and tools read it, as plain key|value lines
	 * without any channel key; the beta file became skunkcrafts/channels/beta.cfg and was absorbed */
	CHECK_EQ_STR(a->version, "2.0");
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater.cfg");        CHECK(sku_is_file(p));
	{
		char *cfg_text, beta[SKU_URL_MAX + 64];
		CHECK_EQ_INT(sku_read_file(p, &cfg_text, NULL), SKU_OK);
		CHECK(strstr(cfg_text, "version|2.0\n") != NULL);
		CHECK(strstr(cfg_text, "channel") == NULL);
		CHECK(strchr(cfg_text, '#') == NULL);                          /* no comment lines for 3.x parsers */
		free(cfg_text);
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/channels/beta.cfg");
		CHECK_EQ_INT(sku_read_file(p, &cfg_text, NULL), SKU_OK);
		sku_snprintf(beta, sizeof beta, "channel|beta\nmodule|%s/beta\nzone|custom\n", url);
		CHECK_EQ_STR(cfg_text, beta);
		free(cfg_text);
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/channel.cfg");
		CHECK_EQ_INT(sku_read_file(p, &cfg_text, NULL), SKU_OK);
		CHECK_EQ_STR(cfg_text, "channel|stable\n");
		free(cfg_text);
	}
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater_ignore.txt"); CHECK(sku_is_file(p));
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater_beta.cfg");   CHECK(!sku_exists(p));
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/updater.cfg");        CHECK(!sku_exists(p));
	sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/ignore.txt");         CHECK(!sku_exists(p));
	CHECK_EQ_STR(a->channel, "stable");

	/* --- an install converted by a 4.0 beta (configuration and ignore list moved into skunkcrafts/,
	 * root files removed) is migrated back on load: settings kept, ignore lists folded, folder emptied */
	{
		char *cfg_text;
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater.cfg");
		CHECK_EQ_INT(sku_read_file(p, &cfg_text, NULL), SKU_OK);
		sku_remove_file(p);
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/updater.cfg");
		{
			char *with_rc = sku_xmalloc(strlen(cfg_text) + 128);
			sprintf(with_rc, "# 4.0 beta layout\n%schannel.rc.module|%s/rc\nchannel.rc.zone|custom\n", cfg_text, url);
			CHECK_EQ_INT(sku_write_file_atomic(p, with_rc, strlen(with_rc)), SKU_OK);
			free(with_rc);
		}
		free(cfg_text);
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/ignore.txt");
		CHECK_EQ_INT(sku_write_file_atomic(p, "data/restored.prf\n", 18), SKU_OK);
		CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
		CHECK_EQ_STR(a->version, "2.0");
		CHECK_EQ_INT(a->nchannels, 2);                                  /* beta and rc */
		CHECK(sku_strlist_has(&a->ignore, "data/restored.prf"));
		CHECK(sku_strlist_has(&a->ignore, "data/user.prf"));
		CHECK(!a->legacy_v4);                                           /* migrated at load (writable instance) */
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater.cfg");        CHECK(sku_is_file(p));
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/updater.cfg");        CHECK(!sku_exists(p));
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/ignore.txt");         CHECK(!sku_exists(p));
		CHECK_EQ_INT(sku_read_file(p, &cfg_text, NULL) == SKU_OK ? 1 : 0, 0);
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater_ignore.txt");
		CHECK_EQ_INT(sku_read_file(p, &cfg_text, NULL), SKU_OK);
		CHECK(strstr(cfg_text, "data/restored.prf") && strstr(cfg_text, "data/user.prf"));
		free(cfg_text);
		CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
		CHECK_EQ_INT(a->ignore.n, 2);
		/* the rc keys moved to skunkcrafts/channels/rc.cfg; remove the channel again for the rest
		 * of the test: its file is set aside, not deleted, and does not come back */
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/channels/rc.cfg");    CHECK(sku_is_file(p));
		CHECK_EQ_INT(sku_addon_remove_channel(a, "rc", false), SKU_OK);
		CHECK(!sku_exists(p));
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/channels/rc.cfg.removed"); CHECK(sku_is_file(p));
		CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
		CHECK_EQ_INT(a->nchannels, 1);
		CHECK_EQ_INT(sku_addon_remove_channel(a, "stable", false), SKU_ERR_NOTFOUND);
		CHECK_EQ_INT(sku_addon_remove_channel(a, "nope", false), SKU_ERR_NOTFOUND);
	}

	/* --- a new beta file with the same depot URL and another name renames the channel (no second channel) */
	{
		char text[SKU_URL_MAX + 64];
		sku_snprintf(text, sizeof text, "module|%s/beta\nzone|custom\nchannel|Private beta\nenabled|false\n", url);
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater_beta.cfg");
		CHECK_EQ_INT(sku_write_file_atomic(p, text, strlen(text)), SKU_OK);
		CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
		CHECK_EQ_INT(a->nchannels, 1);
		CHECK_EQ_STR(a->channels[0].name, "Private beta");
		CHECK_EQ_STR(a->channel, "stable");
		CHECK_EQ_INT(sku_addon_save(a), SKU_OK);   /* the file is absorbed */
		CHECK(!sku_exists(p));
		/* skunkcrafts_updater_beta.cfg replaces the "beta" channel whatever the URL (3.x: one file, one beta),
		 * and the file is absorbed at load, without waiting for a save */
		sku_snprintf(text, sizeof text, "module|%s/beta2/\nzone|custom\nchannel|Private beta\nenabled|true\n", url);
		CHECK_EQ_INT(sku_write_file_atomic(p, text, strlen(text)), SKU_OK);
		CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
		CHECK_EQ_INT(a->nchannels, 1);
		CHECK_EQ_STR(a->channels[0].name, "Private beta");
		CHECK(sku_str_ends(a->channels[0].module, "/beta2/"));
		CHECK_EQ_STR(a->channel, "Private beta");
		CHECK(!sku_exists(p));
		/* the same the other way round: a file whose default name is the current one renames it back */
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater_beta_Private beta.cfg");
		sku_snprintf(text, sizeof text, "module|%s/beta\nzone|custom\nchannel|beta\nenabled|false\n", url);
		CHECK_EQ_INT(sku_write_file_atomic(p, text, strlen(text)), SKU_OK);
		CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
		CHECK_EQ_INT(a->nchannels, 1);
		CHECK_EQ_STR(a->channels[0].name, "beta");
		CHECK_EQ_STR(a->channel, "stable");
		CHECK(!sku_exists(p));
	}

	/* --- users put the tester file back when they see it gone: never a second channel for a
	 * depot already followed, and the name given by the developer stays unless the file names another */
	{
		char text[SKU_URL_MAX + 64], beta_file[SKU_PATH_MAX];
		sku_path_join(beta_file, sizeof beta_file, addon_dir, "skunkcrafts_updater_beta.cfg");
		sku_snprintf(text, sizeof text, "module|%s/beta/\nzone|custom\nchannel|Private beta\nenabled|false\n", url);
		CHECK_EQ_INT(sku_write_file_atomic(beta_file, text, strlen(text)), SKU_OK);
		CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
		CHECK_EQ_INT(a->nchannels, 1);
		CHECK_EQ_STR(a->channels[0].name, "Private beta");
		for (int i = 0; i < 3; i++) {                                   /* the plain 3.x file, again and again */
			sku_snprintf(text, sizeof text, "module|%s/beta\nzone|custom\nenabled|true\n", url);
			CHECK_EQ_INT(sku_write_file_atomic(beta_file, text, strlen(text)), SKU_OK);
			CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
			CHECK_EQ_INT(a->nchannels, 1);
			CHECK_EQ_STR(a->channels[0].name, "Private beta");
			CHECK_EQ_STR(a->channel, "Private beta");                    /* enabled|true follows it */
			CHECK(!sku_exists(beta_file));
		}
		/* a file pointing at the main depot adds nothing */
		sku_snprintf(text, sizeof text, "module|%s\nzone|custom\nchannel|Main again\n", sku_cfg_get(&a->local, "module"));
		CHECK_EQ_INT(sku_write_file_atomic(beta_file, text, strlen(text)), SKU_OK);
		CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
		CHECK_EQ_INT(a->nchannels, 1);
		CHECK(!sku_exists(beta_file));
		/* saving never deletes a channel file it cannot read */
		sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/channels/broken.cfg");
		CHECK_EQ_INT(sku_write_file_atomic(p, "zone|custom\n", 12), SKU_OK);
		CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
		CHECK_EQ_INT(sku_addon_save(a), SKU_OK);
		CHECK(sku_is_file(p));
		sku_remove_file(p);
		/* the channels of 4.0.0-4.0.2, kept in skunkcrafts_updater.cfg, move to the private folder */
		{
			char *cfg_text, *with;
			sku_path_join(p, sizeof p, addon_dir, "skunkcrafts_updater.cfg");
			CHECK_EQ_INT(sku_read_file(p, &cfg_text, NULL), SKU_OK);
			with = sku_xmalloc(strlen(cfg_text) + 256);
			sprintf(with, "%schannel|old rc\nchannel.old rc.module|%s/rc\nchannel.old rc.zone|custom\n", cfg_text, url);
			CHECK_EQ_INT(sku_write_file_atomic(p, with, strlen(with)), SKU_OK);
			free(with); free(cfg_text);
			sku_path_join(q_channel_file, sizeof q_channel_file, addon_dir, "skunkcrafts/channel.cfg");
			sku_remove_file(q_channel_file);                            /* as left by 4.0.2: no private file */
			CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
			CHECK_EQ_INT(a->nchannels, 2);
			CHECK_EQ_STR(a->channel, "old rc");
			CHECK_EQ_INT(sku_read_file(p, &cfg_text, NULL), SKU_OK);
			CHECK(strstr(cfg_text, "channel") == NULL);
			free(cfg_text);
			sku_path_join(p, sizeof p, addon_dir, "skunkcrafts/channels/old_rc.cfg"); CHECK(sku_is_file(p));
		}
		/* back to a single "beta" channel on the beta depot, production active, for the rest of the test */
		CHECK_EQ_INT(sku_addon_remove_channel(a, "old rc", false), SKU_OK);
		CHECK_EQ_STR(a->channel, "stable");
		sku_snprintf(text, sizeof text, "module|%s/beta\nzone|custom\nchannel|beta\nenabled|false\n", url);
		CHECK_EQ_INT(sku_write_file_atomic(beta_file, text, strlen(text)), SKU_OK);
		CHECK_EQ_INT(sku_addon_reload(a), SKU_OK);
		CHECK_EQ_INT(a->nchannels, 1);
		CHECK_EQ_STR(a->channels[0].name, "beta");
		CHECK_EQ_STR(a->channel, "stable");
	}

	/* --- second check: everything up to date, hashes come from the cache,
	 * and the unchanged remote lists were answered with 304 */
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_INT(sku_addon_status(a, NULL, 0, NULL, 0), SKU_ST_UP_TO_DATE);
	CHECK(a->lists_from_cache >= 2);   /* manifest or whitelist+sizes, and the blacklist */
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
	/* on a channel, the remote cfg (the same as production's) neither renames nor moves anything */
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_STR(a->channel, "beta");
	CHECK_EQ_STR(a->remote_version, "2.0b1");
	CHECK(strstr(a->base_url, "/beta/") != NULL);
	CHECK(strstr(sku_cfg_get_or(&a->local, "module", ""), level) != NULL);   /* the production URL stays */
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

static void test_pack(void)
{
	sku_instance in;
	sku_found_list found = {0};
	sku_addon *addons[8];
	int n = 0;
	sku_pack_list packs;
	sku_pack_member_decl *missing;
	char created[SKU_PATH_MAX];
	sku_check_opts co = { false, false, 1 };
	sku_update_opts uo = { false, 2, 1 };

	CHECK_EQ_INT(sku_instance_open(&in, xp, false), SKU_OK);
	sku_scan_instance(&in, &found);
	for (int i = 0; i < found.n && n < 8; i++) addons[n++] = sku_addon_open(&in, found.v[i].dir, found.v[i].genre);
	CHECK_EQ_INT(sku_packs_build(addons, n, &packs), 1);
	CHECK_EQ_STR(packs.v[0].id, "simpack");
	CHECK_EQ_STR(packs.v[0].name, "Sim Pack");
	CHECK_EQ_INT(packs.v[0].nmembers, 2);

	/* member b's local cfg declares no members; its depot does, and a check adopts the declaration */
	for (int i = 0; i < n; i++) if (strcmp(addons[i]->member_id, "b") == 0) {
		CHECK(sku_cfg_get(&addons[i]->local, "pack_members") == NULL);
		CHECK_EQ_INT(sku_addon_check(addons[i], &co), SKU_OK);
		CHECK(sku_cfg_get(&addons[i]->local, "pack_members") != NULL && strstr(sku_cfg_get(&addons[i]->local, "pack_members"), "c=Custom Scenery/Pack C="));
		CHECK_EQ_STR(addons[i]->pack_name, "Sim Pack");
	}

	/* member c is declared by a's local cfg (and now b's) but absent */
	CHECK_EQ_INT(sku_pack_missing(&packs.v[0], addons, &missing), 1);
	CHECK_EQ_STR(missing[0].member_id, "c");
	CHECK_EQ_STR(missing[0].path, "Custom Scenery/Pack C");
	CHECK_EQ_INT(sku_pack_create_member(&in, &packs.v[0], &missing[0], created, sizeof created), SKU_OK);
	CHECK(sku_str_ends(created, "Custom Scenery/Pack C"));
	{
		sku_addon *c = sku_addon_open(&in, created, SKU_GENRE_SCENERY);
		CHECK_EQ_STR(c->pack_id, "simpack");
		CHECK_EQ_STR(c->member_id, "c");
		CHECK_EQ_STR(c->version, "none");
		CHECK_EQ_INT(sku_addon_check(c, &co), SKU_OK);
		CHECK_EQ_INT(c->plan.n_new, 2);
		CHECK_EQ_INT(sku_addon_update(c, &uo), SKU_OK);
		CHECK_EQ_STR(c->version, "2.0");
		sku_addon_close(c);
	}
	free(missing);
	/* after a rescan the pack has three members and nothing is missing */
	sku_pack_list_free(&packs);
	for (int i = 0; i < n; i++) sku_addon_close(addons[i]);
	n = 0;
	sku_found_list_free(&found);
	sku_scan_instance(&in, &found);
	for (int i = 0; i < found.n && n < 8; i++) addons[n++] = sku_addon_open(&in, found.v[i].dir, found.v[i].genre);
	CHECK_EQ_INT(sku_packs_build(addons, n, &packs), 1);
	CHECK_EQ_INT(packs.v[0].nmembers, 3);
	CHECK_EQ_INT(sku_pack_missing(&packs.v[0], addons, &missing), 0);
	free(missing);

	/* unsafe declarations are rejected */
	{
		sku_pack_member_decl *d;
		CHECK_EQ_INT(sku_pack_parse_members("x=../evil=http://x;y=Resources/default scenery/z=http://x;z=Custom Scenery/ok=http://x;w=rootfile=http://x", &d), 1);
		CHECK_EQ_STR(d[0].member_id, "z");
		free(d);
	}
	sku_pack_list_free(&packs);
	for (int i = 0; i < n; i++) sku_addon_close(addons[i]);
	sku_found_list_free(&found);
	sku_instance_close(&in);
}

/* Scan depth as in 3.x: Custom Scenery and Resources/plugins are looked at one
 * level deep; an unmanaged folder there gets a single debug line, its sub-folders
 * (and plugins/(disabled)/...) are not explored. */
/* A depot that answers 404 to any URL with a query string (3.x never sent one): the
 * client asks again without, and goes without for the rest of the session. The lists
 * are served to the client's User-Agent only, as some real depots do. */
static void test_no_query_depot(void)
{
	sku_instance in;
	sku_addon *a;
	sku_check_opts co = { false, false, 2 };
	char dir[SKU_PATH_MAX], p[SKU_PATH_MAX], text[SKU_URL_MAX + 128];

	sku_path_join(dir, sizeof dir, xp, "Aircraft/Strict/Plane");
	CHECK_EQ_INT(sku_mkdir_p(dir), SKU_OK);
	sku_snprintf(text, sizeof text, "name|Test plane\nmodule|%s/noquery\nversion|1.0\nzone|custom\n", url);
	sku_path_join(p, sizeof p, dir, "skunkcrafts_updater.cfg");
	CHECK_EQ_INT(sku_write_file_atomic(p, text, strlen(text)), SKU_OK);
	CHECK_EQ_INT(sku_instance_open(&in, xp, false), SKU_OK);
	a = sku_addon_open(&in, dir, SKU_GENRE_AIRCRAFT);
	CHECK(!a->no_query);
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);
	CHECK_EQ_INT(sku_addon_status(a, NULL, 0, NULL, 0), SKU_ST_UPDATE);
	CHECK_EQ_STR(a->remote_version, "2.0");
	CHECK_EQ_INT(a->files.n, 9);
	CHECK(a->no_query);
	CHECK_EQ_INT(sku_addon_check(a, &co), SKU_OK);   /* again, straight without */
	CHECK_EQ_INT(sku_addon_status(a, NULL, 0, NULL, 0), SKU_ST_UPDATE);
	sku_addon_close(a);
	sku_instance_close(&in);
}

static int count(const char *text, const char *needle)
{
	int n = 0;
	for (const char *p = strstr(text, needle); p; p = strstr(p + 1, needle)) n++;
	return n;
}

static void test_scan_depth(void)
{
	static const char *dirs[] = { "Aircraft", "Custom Scenery/Unmanaged/objects", "Custom Scenery/Unmanaged/Earth nav data",
		"Custom Scenery/Hidden/objects/deep", "Custom Scenery/Managed", "Resources/plugins/(disabled)/Old", "Resources/plugins/Live",
		"Output/preferences" };
	static const char *cfgs[] = { "Custom Scenery/Managed", "Custom Scenery/Hidden/objects", "Resources/plugins/(disabled)/Old",
		"Resources/plugins/Live" };
	static const char cfg[] = "name|x\nmodule|http://127.0.0.1:1/x\nversion|1\n";
	const char *level = getenv("SKU_TEST_LEVEL");
	char root[SKU_PATH_MAX], p[SKU_PATH_MAX], q[SKU_PATH_MAX], *log = NULL;
	sku_instance in;
	sku_found_list found = {0};

	sku_snprintf(root, sizeof root, "%s/../scan_%s", xp, level ? level : "level1");
	for (size_t i = 0; i < sizeof dirs / sizeof *dirs; i++) { sku_path_join(p, sizeof p, root, dirs[i]); CHECK_EQ_INT(sku_mkdir_p(p), SKU_OK); }
	for (size_t i = 0; i < sizeof cfgs / sizeof *cfgs; i++) {
		sku_path_join(p, sizeof p, root, cfgs[i]);
		sku_path_join(q, sizeof q, p, SKU_F_CFG);
		CHECK_EQ_INT(sku_write_file_atomic(q, cfg, strlen(cfg)), SKU_OK);
	}
	CHECK_EQ_INT(sku_instance_open(&in, root, false), SKU_OK);
	CHECK_EQ_INT(sku_scan_instance(&in, &found), SKU_OK);
	CHECK_EQ_INT(found.n, 2);                       /* Managed and Live; not Hidden/objects nor (disabled)/Old */
	sku_found_list_free(&found);
	CHECK_EQ_INT(sku_read_file(in.log_path, &log, NULL), SKU_OK);
	if (!log) return;
	CHECK_EQ_INT(count(log, "Custom Scenery/Unmanaged: not managed by the updater"), 1);
	CHECK_EQ_INT(count(log, "Custom Scenery/Hidden: not managed by the updater"), 1);
	CHECK_EQ_INT(count(log, "plugins/(disabled): not managed by the updater"), 1);
	CHECK_EQ_INT(count(log, "max depth"), 0);
	CHECK_EQ_INT(count(log, "3.x config"), 0);
	CHECK_EQ_INT(count(log, "Custom Scenery/Managed: managed addon found\n"), 1);
	free(log);
}

int main(void)
{
	xp = getenv("SKU_TEST_XP"); url = getenv("SKU_TEST_URL");
	if (!xp || !url) { fprintf(stderr, "SKU_TEST_XP / SKU_TEST_URL not set\n"); return 2; }
	sku_log_set_level(SKU_LOG_DEBUG);
	sku_http_global_init(NULL, 0, NULL);   /* the client's own User-Agent: the depot server refuses lists to others */
	test_client();
	test_level(getenv("SKU_TEST_LEVEL") ? getenv("SKU_TEST_LEVEL") : "level1", true);
	test_errors();
	test_pack();
	test_no_query_depot();
	test_scan_depth();
	sku_http_global_cleanup();
	TEST_MAIN_END();
}
