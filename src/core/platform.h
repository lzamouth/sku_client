/* Thin portability layer over the file system and the OS.
 * All paths are UTF-8 with '/' separators, on every platform. */
#ifndef SKU_PLATFORM_H
#define SKU_PLATFORM_H

#include "sku.h"
#include <stdio.h>

typedef struct {
	bool    is_dir;
	bool    is_file;
	bool    is_symlink;   /* only meaningful from sku_lstat */
	int64_t size;
	int64_t mtime;        /* seconds since the epoch */
} sku_fileinfo;

int  sku_stat(const char *path, sku_fileinfo *st);    /* follows symlinks; SKU_ERR_NOTFOUND if absent */
int  sku_lstat(const char *path, sku_fileinfo *st);   /* does not follow the final symlink */
bool sku_exists(const char *path);
bool sku_is_dir(const char *path);
bool sku_is_file(const char *path);

FILE *sku_fopen(const char *path, const char *mode);
int   sku_mkdir(const char *path);                /* single level, ok if it exists */
int   sku_mkdir_p(const char *path);
int   sku_remove_file(const char *path);
int   sku_remove_dir(const char *path);           /* must be empty */
int   sku_remove_tree(const char *path);          /* files and folders below, then path itself (symlinks not followed) */
int   sku_rename(const char *from, const char *to); /* atomic replace of an existing target */
int   sku_set_executable(const char *path);        /* rwxr-xr-x on POSIX, no-op on Windows */
/* Starts exe as an independent process (argv[0] included, NULL-terminated) and returns at once.
 * Windows goes through the shell, so an installer that needs elevation gets its prompt. */
int   sku_spawn_detached(const char *exe, char *const argv[]);
int   sku_realpath(const char *path, char *out, size_t n);  /* resolves symlinks, absolute */
int   sku_getcwd(char *out, size_t n);
int   sku_chdir(const char *path);
int   sku_exe_path(char *out, size_t n);          /* full path of the running executable */

/* Read a whole file; the buffer is malloc'd and NUL-terminated. */
int   sku_read_file(const char *path, char **out, size_t *len);
/* Write a whole file through a temporary file + rename. */
int   sku_write_file_atomic(const char *path, const void *data, size_t len);

/* Directory listing. The callback returns non-zero to stop. Entries "." and ".." are skipped. */
typedef int (*sku_dir_cb)(const char *name, bool is_dir, bool is_symlink, void *user);
int   sku_list_dir(const char *path, sku_dir_cb cb, void *user);

/* Per-user directories (created if needed) and the OS name. */
int   sku_user_config_dir(char *out, size_t n);   /* .../skunkcrafts/updater */
int   sku_user_home(char *out, size_t n);
const char *sku_os_name(void);                    /* "linux", "windows", "macos" */

int64_t sku_time_ms(void);                        /* monotonic milliseconds */
int64_t sku_time_now(void);                       /* wall clock, seconds since the epoch */
void    sku_sleep_ms(int ms);
int     sku_cpu_count(void);
void    sku_format_time(char *out, size_t n, int64_t t); /* "2026-09-23 17:05:33" local time */

#endif
