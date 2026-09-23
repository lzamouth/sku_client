/* platform.h implementation for Linux and macOS. */
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

static int fill_stat(const struct stat *s, sku_fileinfo *st)
{
	memset(st, 0, sizeof *st);
	st->is_dir     = S_ISDIR(s->st_mode);
	st->is_file    = S_ISREG(s->st_mode);
	st->is_symlink = S_ISLNK(s->st_mode);
	st->size       = (int64_t)s->st_size;
	st->mtime      = (int64_t)s->st_mtime;
	return SKU_OK;
}

int sku_stat(const char *path, sku_fileinfo *st)
{
	struct stat s;
	if (stat(path, &s) != 0) return errno == ENOENT || errno == ENOTDIR ? SKU_ERR_NOTFOUND : SKU_ERR_IO;
	return fill_stat(&s, st);
}

int sku_lstat(const char *path, sku_fileinfo *st)
{
	struct stat s;
	if (lstat(path, &s) != 0) return errno == ENOENT || errno == ENOTDIR ? SKU_ERR_NOTFOUND : SKU_ERR_IO;
	return fill_stat(&s, st);
}

bool sku_exists(const char *path) { sku_fileinfo st; return sku_stat(path, &st) == SKU_OK; }
bool sku_is_dir(const char *path)  { sku_fileinfo st; return sku_stat(path, &st) == SKU_OK && st.is_dir; }
bool sku_is_file(const char *path) { sku_fileinfo st; return sku_stat(path, &st) == SKU_OK && st.is_file; }

FILE *sku_fopen(const char *path, const char *mode)
{
	return fopen(path, mode);
}

int sku_mkdir(const char *path)
{
	if (mkdir(path, 0755) == 0 || errno == EEXIST) return SKU_OK;
	return SKU_ERR_IO;
}

int sku_mkdir_p(const char *path)
{
	char tmp[SKU_PATH_MAX];
	size_t n = sku_strlcpy(tmp, path, sizeof tmp);
	if (n >= sizeof tmp) return SKU_ERR_ARG;
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (sku_mkdir(tmp) != SKU_OK && !sku_is_dir(tmp)) return SKU_ERR_IO;
			*p = '/';
		}
	}
	return sku_mkdir(tmp) == SKU_OK || sku_is_dir(tmp) ? SKU_OK : SKU_ERR_IO;
}

int sku_remove_file(const char *path)
{
	if (unlink(path) == 0) return SKU_OK;
	return errno == ENOENT ? SKU_ERR_NOTFOUND : SKU_ERR_IO;
}

int sku_remove_dir(const char *path)
{
	return rmdir(path) == 0 ? SKU_OK : SKU_ERR_IO;
}

int sku_rename(const char *from, const char *to)
{
	return rename(from, to) == 0 ? SKU_OK : SKU_ERR_IO;
}

int sku_realpath(const char *path, char *out, size_t n)
{
	char buf[PATH_MAX];
	if (!realpath(path, buf)) return SKU_ERR_NOTFOUND;
	return sku_strlcpy(out, buf, n) < n ? SKU_OK : SKU_ERR_ARG;
}

int sku_getcwd(char *out, size_t n)
{
	return getcwd(out, n) ? SKU_OK : SKU_ERR_IO;
}

int sku_chdir(const char *path)
{
	return chdir(path) == 0 ? SKU_OK : SKU_ERR_IO;
}

int sku_exe_path(char *out, size_t n)
{
#ifdef __APPLE__
	char buf[SKU_PATH_MAX];
	uint32_t sz = sizeof buf;
	if (_NSGetExecutablePath(buf, &sz) != 0) return SKU_ERR_IO;
	return sku_realpath(buf, out, n);
#else
	ssize_t k = readlink("/proc/self/exe", out, n - 1);
	if (k < 0) return SKU_ERR_IO;
	out[k] = 0;
	return SKU_OK;
#endif
}

int sku_read_file(const char *path, char **out, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf = NULL;
	size_t cap = 0, n = 0;
	if (!f) return errno == ENOENT ? SKU_ERR_NOTFOUND : SKU_ERR_IO;
	for (;;) {
		size_t k;
		if (n + 65536 + 1 > cap) { cap = cap ? cap * 2 : 131072; buf = sku_xrealloc(buf, cap); }
		k = fread(buf + n, 1, cap - n - 1, f);
		n += k;
		if (k == 0) break;
	}
	if (ferror(f)) { fclose(f); free(buf); return SKU_ERR_IO; }
	fclose(f);
	buf[n] = 0;
	*out = buf;
	if (len) *len = n;
	return SKU_OK;
}

int sku_write_file_atomic(const char *path, const void *data, size_t len)
{
	char tmp[SKU_PATH_MAX];
	FILE *f;
	if (sku_snprintf(tmp, sizeof tmp, "%s.tmp.%d", path, (int)getpid()) >= (int)sizeof tmp) return SKU_ERR_ARG;
	f = fopen(tmp, "wb");
	if (!f) return SKU_ERR_IO;
	if (fwrite(data, 1, len, f) != len || fflush(f) != 0 || fsync(fileno(f)) != 0) { fclose(f); unlink(tmp); return SKU_ERR_IO; }
	if (fclose(f) != 0) { unlink(tmp); return SKU_ERR_IO; }
	if (rename(tmp, path) != 0) { unlink(tmp); return SKU_ERR_IO; }
	return SKU_OK;
}

int sku_list_dir(const char *path, sku_dir_cb cb, void *user)
{
	DIR *d = opendir(path);
	struct dirent *e;
	int stop = 0;
	if (!d) return errno == ENOENT ? SKU_ERR_NOTFOUND : SKU_ERR_IO;
	while (!stop && (e = readdir(d)) != NULL) {
		bool is_dir, is_link;
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
#ifdef DT_DIR
		if (e->d_type != DT_UNKNOWN) {
			is_dir  = e->d_type == DT_DIR;
			is_link = e->d_type == DT_LNK;
		} else
#endif
		{
			char full[SKU_PATH_MAX];
			sku_fileinfo st = {0};
			sku_snprintf(full, sizeof full, "%s/%s", path, e->d_name);
			is_link = sku_lstat(full, &st) == SKU_OK && st.is_symlink;
			is_dir  = !is_link && st.is_dir;
		}
		stop = cb(e->d_name, is_dir, is_link, user);
	}
	closedir(d);
	return SKU_OK;
}

int sku_user_home(char *out, size_t n)
{
	const char *h = getenv("HOME");
	if (!h || !*h) return SKU_ERR_NOTFOUND;
	return sku_strlcpy(out, h, n) < n ? SKU_OK : SKU_ERR_ARG;
}

int sku_user_config_dir(char *out, size_t n)
{
	char home[SKU_PATH_MAX];
#ifdef __APPLE__
	if (sku_user_home(home, sizeof home) != SKU_OK) return SKU_ERR_NOTFOUND;
	sku_snprintf(out, n, "%s/Library/Application Support/SkunkCrafts/Updater", home);
#else
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg) sku_snprintf(out, n, "%s/skunkcrafts/updater", xdg);
	else {
		if (sku_user_home(home, sizeof home) != SKU_OK) return SKU_ERR_NOTFOUND;
		sku_snprintf(out, n, "%s/.config/skunkcrafts/updater", home);
	}
#endif
	return sku_mkdir_p(out);
}

const char *sku_os_name(void)
{
#ifdef __APPLE__
	return "macos";
#else
	return "linux";
#endif
}

int64_t sku_time_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t sku_time_now(void)
{
	return (int64_t)time(NULL);
}

void sku_sleep_ms(int ms)
{
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

int sku_cpu_count(void)
{
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	return n > 0 ? (int)n : 1;
}

void sku_format_time(char *out, size_t n, int64_t t)
{
	time_t tt = (time_t)t;
	struct tm tm;
	localtime_r(&tt, &tm);
	strftime(out, n, "%Y-%m-%d %H:%M:%S", &tm);
}
