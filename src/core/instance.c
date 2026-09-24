#include "instance.h"
#include "platform.h"
#include "paths.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define LOG_ROTATE_BYTES (2 * 1024 * 1024)

bool sku_instance_is_root(const char *dir)
{
	static const char *marks[] = { "Aircraft", "Custom Scenery", "Resources/plugins", "Output/preferences" };
	char p[SKU_PATH_MAX];
	for (size_t i = 0; i < sizeof marks / sizeof *marks; i++) {
		sku_path_join(p, sizeof p, dir, marks[i]);
		if (!sku_is_dir(p)) return false;
	}
	return true;
}

int sku_instance_open(sku_instance *in, const char *dir, bool read_only)
{
	char sub[SKU_PATH_MAX];
	memset(in, 0, sizeof *in);
	in->read_only = read_only;
	if (sku_realpath(dir, in->root, sizeof in->root) != SKU_OK) sku_strlcpy(in->root, dir, sizeof in->root);
	sku_path_normalize(in->root);
	if (!sku_instance_is_root(in->root)) return SKU_ERR_NOTFOUND;
	sku_path_join(in->data_dir, sizeof in->data_dir, in->root, "Output/skunkcrafts/updater");
	sku_path_join(in->log_path, sizeof in->log_path, in->data_dir, "updater_log.txt");
	if (!read_only) {
		if (sku_mkdir_p(in->data_dir) != SKU_OK) return SKU_ERR_IO;
		sku_path_join(sub, sizeof sub, in->data_dir, "cache");   sku_mkdir_p(sub);
		sku_path_join(sub, sizeof sub, in->data_dir, "reports"); sku_mkdir_p(sub);
		if (sku_log_open(in->log_path, LOG_ROTATE_BYTES) != SKU_OK) return SKU_ERR_IO;
	}
	sku_info("---- SkunkCrafts Updater %s (%s) on %s%s", SKU_VERSION, sku_os_name(), in->root, read_only ? " [read-only]" : "");
	return SKU_OK;
}

void sku_instance_close(sku_instance *in)
{
	(void)in;
	sku_log_close();
}

int sku_instance_detect(char *out, size_t n)
{
	char p[SKU_PATH_MAX], d[SKU_PATH_MAX];
	if (sku_getcwd(p, sizeof p) == SKU_OK && sku_instance_is_root(p)) return (int)(sku_strlcpy(out, p, n) < n ? SKU_OK : SKU_ERR_ARG);
	if (sku_exe_path(p, sizeof p) != SKU_OK) return SKU_ERR_NOTFOUND;
	sku_path_dirname(d, sizeof d, p);
	if (sku_instance_is_root(d)) return (int)(sku_strlcpy(out, d, n) < n ? SKU_OK : SKU_ERR_ARG);
	/* Foo.app/Contents/MacOS/exe -> three levels up */
	if (strstr(d, ".app/Contents/MacOS")) {
		for (int i = 0; i < 3; i++) { sku_path_dirname(p, sizeof p, d); sku_strlcpy(d, p, sizeof d); }
		if (sku_instance_is_root(d)) return (int)(sku_strlcpy(out, d, n) < n ? SKU_OK : SKU_ERR_ARG);
	}
	return SKU_ERR_NOTFOUND;
}

/* Where X-Plane keeps x-plane_install_NN.txt on each OS. */
static int install_list_dir(char *out, size_t n)
{
	char home[SKU_PATH_MAX];
	if (sku_user_home(home, sizeof home) != SKU_OK) return SKU_ERR_NOTFOUND;
	if (strcmp(sku_os_name(), "windows") == 0) {
		const char *lad = getenv("LOCALAPPDATA");
		if (!lad) return SKU_ERR_NOTFOUND;
		sku_strlcpy(out, lad, n);
		sku_path_normalize(out);
	} else if (strcmp(sku_os_name(), "macos") == 0) {
		sku_snprintf(out, n, "%s/Library/Preferences", home);
	} else {
		sku_snprintf(out, n, "%s/.x-plane", home);
	}
	return SKU_OK;
}

int sku_instance_known(sku_strlist *out)
{
	char dir[SKU_PATH_MAX], file[SKU_PATH_MAX], *text;
	static const char *names[] = { "x-plane_install_12.txt", "x-plane_install_11.txt" };
	if (install_list_dir(dir, sizeof dir) != SKU_OK) return SKU_ERR_NOTFOUND;
	for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
		sku_strlist lines = {0};
		sku_path_join(file, sizeof file, dir, names[i]);
		if (sku_read_file(file, &text, NULL) != SKU_OK) continue;
		sku_split_lines(text, &lines);
		for (int k = 0; k < lines.n; k++) {
			char *p = sku_str_trim(lines.v[k]);
			if (!*p) continue;
			sku_path_normalize(p);
			if (sku_instance_is_root(p) && !sku_strlist_has(out, p)) sku_strlist_push(out, p);
		}
		sku_strlist_free(&lines);
		free(text);
	}
	return SKU_OK;
}

/* "<folder name>-<hash>": readable in the cache folder, unique per full path
 * (two addons may share a folder name in different places). */
static void addon_key(const char *addon_dir, char *out, size_t n)
{
	uint64_t h = 1469598103934665603ULL;
	const char *base = sku_path_basename(addon_dir);
	char name[48];
	size_t k = 0;
	for (const char *s = addon_dir; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
	for (const char *s = base; *s && k < sizeof name - 1; s++) {
		unsigned char c = (unsigned char)*s;
		name[k++] = (c < 128 && (isalnum(c) || c == '.' || c == '-' || c == '_')) ? (char)c : '_';
	}
	name[k] = 0;
	sku_snprintf(out, n, "%s-%08llx", k ? name : "addon", (unsigned long long)(h & 0xFFFFFFFFULL));
}

void sku_instance_cache_path(const sku_instance *in, const char *addon_dir, char *out, size_t n)
{
	char key[64];
	addon_key(addon_dir, key, sizeof key);
	sku_snprintf(out, n, "%s/cache/%s.crc", in->data_dir, key);
}

void sku_instance_report_path(const sku_instance *in, const char *addon_dir, char *out, size_t n)
{
	char key[64];
	addon_key(addon_dir, key, sizeof key);
	sku_snprintf(out, n, "%s/reports/%s.txt", in->data_dir, key);
}
