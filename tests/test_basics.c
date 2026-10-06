/* Unit tests: util, paths, cfg, crc32, platform file helpers. */
#include "test.h"
#include "log.h"
#include <sys/stat.h>
#include "sku.h"
#include "paths.h"
#include "cfg.h"
#include "crc32.h"
#include "platform.h"

static void test_util(void)
{
	char buf[16], s[] = "  hello \r\n";
	CHECK_EQ_INT(sku_strlcpy(buf, "abcdefghijklmnopqrstuvwxyz", sizeof buf), 26);
	CHECK_EQ_STR(buf, "abcdefghijklmno");
	CHECK(sku_str_ieq("Liveries", "LIVERIES"));
	CHECK(!sku_str_ieq("Liveries", "Liverie"));
	CHECK(sku_str_istarts("Liveries/x", "liveries"));
	CHECK(sku_str_ends("foo.acf", ".acf"));
	CHECK(!sku_str_ends("acf", ".acf"));
	CHECK_EQ_STR(sku_str_trim(s), "hello");
	CHECK(sku_str_to_bool("true"));
	CHECK(!sku_str_to_bool("True"));
	CHECK(!sku_str_to_bool(NULL));
	sku_nice_size(buf, sizeof buf, 1536); CHECK_EQ_STR(buf, "1.50 kiB");
	sku_nice_size(buf, sizeof buf, 0);    CHECK_EQ_STR(buf, "0");
	{
		char text[] = "\xEF\xBB\xBF" "a\r\nb\n\nc\rd";
		sku_strlist l = {0};
		CHECK_EQ_INT(sku_split_lines(text, &l), 5);
		CHECK_EQ_STR(l.v[0], "a"); CHECK_EQ_STR(l.v[1], "b"); CHECK_EQ_STR(l.v[2], ""); CHECK_EQ_STR(l.v[3], "c"); CHECK_EQ_STR(l.v[4], "d");
		CHECK(sku_strlist_has(&l, "c"));
		sku_strlist_free(&l);
	}
}

static void test_paths(void)
{
	char p[SKU_PATH_MAX];
	sku_path_join(p, sizeof p, "/xp", "Aircraft"); CHECK_EQ_STR(p, "/xp/Aircraft");
	sku_path_join(p, sizeof p, "/xp/", "Aircraft"); CHECK_EQ_STR(p, "/xp/Aircraft");
	sku_strlcpy(p, "a\\b//c/", sizeof p); sku_path_normalize(p); CHECK_EQ_STR(p, "a/b/c");
	sku_strlcpy(p, "C:\\", sizeof p); sku_path_normalize(p); CHECK_EQ_STR(p, "C:/");
	sku_strlcpy(p, "/", sizeof p); sku_path_normalize(p); CHECK_EQ_STR(p, "/");
	CHECK_EQ_STR(sku_path_basename("/a/b/c.txt"), "c.txt");
	sku_path_dirname(p, sizeof p, "/a/b/c.txt"); CHECK_EQ_STR(p, "/a/b");
	sku_path_dirname(p, sizeof p, "c.txt"); CHECK_EQ_STR(p, ".");
	CHECK(sku_path_is_absolute("/x")); CHECK(sku_path_is_absolute("D:/x")); CHECK(!sku_path_is_absolute("x/y"));

	sku_strlcpy(p, "objects/a.obj", sizeof p);        CHECK_EQ_INT(sku_path_check_relative(p), SKU_OK);
	sku_strlcpy(p, "liveries\\Air X\\tex.png", sizeof p); CHECK_EQ_INT(sku_path_check_relative(p), SKU_OK); CHECK_EQ_STR(p, "liveries/Air X/tex.png");
	sku_strlcpy(p, "../x", sizeof p);                 CHECK_EQ_INT(sku_path_check_relative(p), SKU_ERR_UNSAFE);
	sku_strlcpy(p, "a/../../x", sizeof p);            CHECK_EQ_INT(sku_path_check_relative(p), SKU_ERR_UNSAFE);
	sku_strlcpy(p, "a/./x", sizeof p);                CHECK_EQ_INT(sku_path_check_relative(p), SKU_ERR_UNSAFE);
	sku_strlcpy(p, "/etc/passwd", sizeof p);          CHECK_EQ_INT(sku_path_check_relative(p), SKU_ERR_UNSAFE);
	sku_strlcpy(p, "C:/x", sizeof p);                 CHECK_EQ_INT(sku_path_check_relative(p), SKU_ERR_UNSAFE);
	sku_strlcpy(p, "a:b", sizeof p);                  CHECK_EQ_INT(sku_path_check_relative(p), SKU_ERR_UNSAFE);
	sku_strlcpy(p, "", sizeof p);                     CHECK_EQ_INT(sku_path_check_relative(p), SKU_ERR_UNSAFE);
	sku_strlcpy(p, "a\tb", sizeof p);                 CHECK_EQ_INT(sku_path_check_relative(p), SKU_ERR_UNSAFE);
	sku_strlcpy(p, "..foo/x", sizeof p);              CHECK_EQ_INT(sku_path_check_relative(p), SKU_OK);
}

static void test_cfg(void)
{
	sku_cfg c;
	char *text;
	sku_cfg_init(&c);
	CHECK_EQ_INT(sku_cfg_parse(&c, "\xEF\xBB\xBF# comment\r\nname|FJS Q4XP\r\nmodule|https://x/y|z\n\ndisabled|false"), SKU_OK);
	CHECK_EQ_INT(c.n, 5);
	CHECK_EQ_STR(sku_cfg_get(&c, "name"), "FJS Q4XP");
	CHECK_EQ_STR(sku_cfg_get(&c, "module"), "https://x/y|z");     /* split at the first '|' only */
	CHECK_EQ_STR(sku_cfg_get(&c, "disabled"), "false");           /* last line without newline */
	CHECK(sku_cfg_get(&c, "missing") == NULL);
	CHECK_EQ_STR(sku_cfg_get_or(&c, "zone", "custom"), "custom");
	CHECK(!sku_cfg_get_bool(&c, "disabled", true));
	sku_cfg_set(&c, "name", "New");
	sku_cfg_set(&c, "version", "1.0");
	sku_cfg_set_default(&c, "version", "2.0");
	sku_cfg_format(&c, &text, false);
	CHECK_EQ_STR(text, "# comment\nname|New\nmodule|https://x/y|z\n\ndisabled|false\nversion|1.0\n");
	free(text);
	sku_cfg_format(&c, &text, true);
	CHECK_EQ_STR(text, "name|New\nmodule|https://x/y|z\ndisabled|false\nversion|1.0\n");
	free(text);
	sku_cfg_remove(&c, "module");
	CHECK(!sku_cfg_has(&c, "module"));
	CHECK_EQ_INT(c.n, 5);
	sku_cfg_free(&c);

	/* round trip through a file */
	{
		char dir[SKU_PATH_MAX], path[SKU_PATH_MAX];
		sku_snprintf(dir, sizeof dir, "/tmp/sku_test_%d", (int)sku_time_now());
		CHECK_EQ_INT(sku_mkdir_p(dir), SKU_OK);
		sku_path_join(path, sizeof path, dir, "a/b/c.cfg");
		sku_cfg_init(&c);
		sku_cfg_set(&c, "k", "v");
		CHECK_EQ_INT(sku_cfg_save(&c, path), SKU_ERR_IO);   /* parent dir missing */
		sku_path_join(path, sizeof path, dir, "c.cfg");
		CHECK_EQ_INT(sku_cfg_save(&c, path), SKU_OK);
		sku_cfg_free(&c);
		sku_cfg_init(&c);
		CHECK_EQ_INT(sku_cfg_load(&c, path), SKU_OK);
		CHECK_EQ_STR(sku_cfg_get(&c, "k"), "v");
		sku_cfg_free(&c);
		CHECK_EQ_INT(sku_cfg_load(&c, "/nonexistent/x.cfg"), SKU_ERR_NOTFOUND);
		CHECK_EQ_INT(sku_remove_file(path), SKU_OK);
		CHECK_EQ_INT(sku_remove_dir(dir), SKU_OK);
	}
}

static int count_cb(const char *name, bool is_dir, bool is_symlink, void *user)
{
	(void)name; (void)is_symlink;
	if (is_dir) (*(int *)user)++;
	return 0;
}

static void test_crc_and_files(void)
{
	char dir[SKU_PATH_MAX], path[SKU_PATH_MAX];
	char *data;
	size_t len;
	uint32_t crc;
	sku_fileinfo st;
	int dirs = 0;

	CHECK_EQ_INT(sku_crc32_buf("123456789", 9), 0xCBF43926);   /* the standard check value */
	CHECK_EQ_INT(sku_crc32_buf("", 0), 0);
	CHECK_EQ_INT(sku_crc32_update(sku_crc32_buf("1234", 4), "56789", 5), 0xCBF43926);

	sku_snprintf(dir, sizeof dir, "/tmp/sku_test2_%d", (int)sku_time_now());
	CHECK_EQ_INT(sku_mkdir_p(dir), SKU_OK);
	sku_path_join(path, sizeof path, dir, "f.bin");
	CHECK_EQ_INT(sku_write_file_atomic(path, "123456789", 9), SKU_OK);
	CHECK_EQ_INT(sku_crc32_file(path, &crc, NULL), SKU_OK);
	CHECK_EQ_INT(crc, 0xCBF43926);
	CHECK_EQ_INT(sku_read_file(path, &data, &len), SKU_OK);
	CHECK_EQ_INT(len, 9); CHECK_EQ_STR(data, "123456789");
	free(data);
	CHECK_EQ_INT(sku_stat(path, &st), SKU_OK);
	CHECK(st.is_file && !st.is_dir); CHECK_EQ_INT(st.size, 9); CHECK(st.mtime > 1700000000);
	CHECK(sku_is_file(path)); CHECK(sku_is_dir(dir)); CHECK(!sku_exists("/nonexistent/zz"));
	CHECK_EQ_INT(sku_crc32_file("/nonexistent/zz", &crc, NULL), SKU_ERR_NOTFOUND);
	sku_path_join(path, sizeof path, dir, "sub");
	CHECK_EQ_INT(sku_mkdir(path), SKU_OK);
	CHECK_EQ_INT(sku_list_dir(dir, count_cb, &dirs), SKU_OK);
	CHECK_EQ_INT(dirs, 1);
	CHECK_EQ_INT(sku_remove_dir(path), SKU_OK);
	sku_path_join(path, sizeof path, dir, "f.bin");
	CHECK_EQ_INT(sku_remove_file(path), SKU_OK);
	CHECK_EQ_INT(sku_remove_file(path), SKU_ERR_NOTFOUND);
	CHECK_EQ_INT(sku_remove_dir(dir), SKU_OK);
	CHECK(sku_cpu_count() >= 1);
	CHECK_EQ_STR(sku_os_name(), "linux");
}

/* Log rotation: once per launch and per path, four versions kept, the current
 * log emptied in place (same inode) so that klogg and glogg reload it; never on size. */
static void test_log_rotation(void)
{
	char dir[SKU_PATH_MAX], log[SKU_PATH_MAX], p[SKU_PATH_MAX], *text;
	struct stat before, after;
	sku_snprintf(dir, sizeof dir, "/tmp/sku_test3_%d", (int)sku_time_now());
	CHECK_EQ_INT(sku_mkdir_p(dir), SKU_OK);
	sku_path_join(log, sizeof log, dir, "updater_log.txt");

	/* the CLI: no rotation at open, the log grows */
	CHECK_EQ_INT(sku_log_open(log), SKU_OK); sku_info("cli one"); sku_log_close();
	CHECK_EQ_INT(sku_log_open(log), SKU_OK); sku_info("cli two"); sku_log_close();
	sku_snprintf(p, sizeof p, "%s.1", log); CHECK(!sku_exists(p));

	/* the application: one rotation per path, however often the log is reopened */
	sku_log_rotate_at_open(true);
	CHECK_EQ_INT(stat(log, &before), 0);
	CHECK_EQ_INT(sku_log_open(log), SKU_OK); sku_info("launch 1");
	CHECK_EQ_INT(stat(log, &after), 0);
	CHECK(before.st_ino == after.st_ino);                       /* emptied in place, not replaced */
	sku_log_close();
	CHECK_EQ_INT(sku_log_open(log), SKU_OK); sku_info("launch 1, instance reopened"); sku_log_close();
	CHECK_EQ_INT(sku_read_file(p, &text, NULL), SKU_OK);
	CHECK(strstr(text, "cli one") && strstr(text, "cli two")); free(text);
	CHECK_EQ_INT(sku_read_file(log, &text, NULL), SKU_OK);
	CHECK(strstr(text, "launch 1") && strstr(text, "instance reopened") && !strstr(text, "cli")); free(text);

	/* five more launches (a new process each time: forget the paths already rotated) */
	for (int i = 2; i <= 6; i++) {
		sku_log_rotate_reset_for_tests();
		CHECK_EQ_INT(sku_log_open(log), SKU_OK); sku_info("launch %d", i); sku_log_close();
	}
	CHECK_EQ_INT(sku_read_file(log, &text, NULL), SKU_OK); CHECK(strstr(text, "launch 6") != NULL); free(text);
	sku_snprintf(p, sizeof p, "%s.1", log); CHECK_EQ_INT(sku_read_file(p, &text, NULL), SKU_OK); CHECK(strstr(text, "launch 5") != NULL); free(text);
	sku_snprintf(p, sizeof p, "%s.3", log); CHECK_EQ_INT(sku_read_file(p, &text, NULL), SKU_OK); CHECK(strstr(text, "launch 3") != NULL); free(text);
	sku_snprintf(p, sizeof p, "%s.4", log); CHECK(!sku_exists(p));

	/* a long session is never cut: no rotation on size */
	sku_log_rotate_reset_for_tests();
	sku_log_rotate_at_open(false);
	CHECK_EQ_INT(sku_log_open(log), SKU_OK);
	for (int i = 0; i < 3000; i++) sku_info("long session line %d ......................................", i);
	sku_log_close();
	CHECK_EQ_INT(sku_read_file(log, &text, NULL), SKU_OK);
	CHECK(strstr(text, "launch 6") && strstr(text, "long session line 0 ") && strstr(text, "long session line 2999")); free(text);
	for (int i = 1; i <= 3; i++) { sku_snprintf(p, sizeof p, "%s.%d", log, i); sku_remove_file(p); }
	sku_remove_file(log);
	sku_remove_dir(dir);
}

int main(void)
{
	test_util();
	test_paths();
	test_cfg();
	test_crc_and_files();
	test_log_rotation();
	TEST_MAIN_END();
}
