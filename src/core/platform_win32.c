/* platform.h implementation for Windows (mingw-w64).
 * UTF-8 in, UTF-16 to the Win32 API; long paths get the \\?\ prefix. */
#include "platform.h"
#include <windows.h>
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <io.h>
#include <fcntl.h>

#define WPATH_MAX 4096

/* UTF-8 -> UTF-16, '/' -> '\', "\\?\" prefix for long absolute paths. */
static int to_wide(const char *path, wchar_t *out, size_t n)
{
	wchar_t tmp[WPATH_MAX];
	int k = MultiByteToWideChar(CP_UTF8, 0, path, -1, tmp, WPATH_MAX);
	size_t prefix = 0;
	if (k <= 0) return SKU_ERR_ARG;
	for (wchar_t *p = tmp; *p; p++) if (*p == L'/') *p = L'\\';
	if (k > 240 && ((tmp[0] && tmp[1] == L':') || (tmp[0] == L'\\' && tmp[1] == L'\\'))) {
		if (tmp[0] == L'\\') { wcscpy(out, L"\\\\?\\UNC\\"); prefix = 8; k -= 2; wcscpy(out + prefix, tmp + 2); }
		else { wcscpy(out, L"\\\\?\\"); prefix = 4; wcscpy(out + prefix, tmp); }
		return prefix + (size_t)k < n ? SKU_OK : SKU_ERR_ARG;
	}
	if ((size_t)k >= n) return SKU_ERR_ARG;
	wcscpy(out, tmp);
	return SKU_OK;
}

static int from_wide(const wchar_t *w, char *out, size_t n)
{
	int k = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL);
	if (k <= 0) return SKU_ERR_ARG;
	for (char *p = out; *p; p++) if (*p == '\\') *p = '/';
	return SKU_OK;
}

static int64_t filetime_to_unix(const FILETIME *ft)
{
	ULARGE_INTEGER u;
	u.LowPart = ft->dwLowDateTime; u.HighPart = ft->dwHighDateTime;
	return (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

static int stat_impl(const char *path, sku_fileinfo *st, bool follow)
{
	wchar_t w[WPATH_MAX];
	WIN32_FILE_ATTRIBUTE_DATA a;
	HANDLE h;
	if (to_wide(path, w, WPATH_MAX) != SKU_OK) return SKU_ERR_ARG;
	memset(st, 0, sizeof *st);
	if (!GetFileAttributesExW(w, GetFileExInfoStandard, &a)) {
		DWORD e = GetLastError();
		return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? SKU_ERR_NOTFOUND : SKU_ERR_IO;
	}
	st->is_symlink = (a.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
	if (st->is_symlink && follow) {
		BY_HANDLE_FILE_INFORMATION info;
		h = CreateFileW(w, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
		if (h == INVALID_HANDLE_VALUE) return SKU_ERR_NOTFOUND;
		if (!GetFileInformationByHandle(h, &info)) { CloseHandle(h); return SKU_ERR_IO; }
		CloseHandle(h);
		st->is_symlink = false;
		st->is_dir  = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		st->is_file = !st->is_dir;
		st->size    = ((int64_t)info.nFileSizeHigh << 32) | info.nFileSizeLow;
		st->mtime   = filetime_to_unix(&info.ftLastWriteTime);
		return SKU_OK;
	}
	st->is_dir  = (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	st->is_file = !st->is_dir && !st->is_symlink;
	st->size    = ((int64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
	st->mtime   = filetime_to_unix(&a.ftLastWriteTime);
	return SKU_OK;
}

int sku_stat(const char *path, sku_fileinfo *st)  { return stat_impl(path, st, true); }
int sku_lstat(const char *path, sku_fileinfo *st) { return stat_impl(path, st, false); }

bool sku_exists(const char *path) { sku_fileinfo st; return sku_stat(path, &st) == SKU_OK; }
bool sku_is_dir(const char *path)  { sku_fileinfo st; return sku_stat(path, &st) == SKU_OK && st.is_dir; }
bool sku_is_file(const char *path) { sku_fileinfo st; return sku_stat(path, &st) == SKU_OK && st.is_file; }

FILE *sku_fopen(const char *path, const char *mode)
{
	wchar_t w[WPATH_MAX], wm[16];
	if (to_wide(path, w, WPATH_MAX) != SKU_OK) return NULL;
	MultiByteToWideChar(CP_UTF8, 0, mode, -1, wm, 16);
	return _wfopen(w, wm);
}

int sku_mkdir(const char *path)
{
	wchar_t w[WPATH_MAX];
	if (to_wide(path, w, WPATH_MAX) != SKU_OK) return SKU_ERR_ARG;
	if (CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS) return SKU_OK;
	return SKU_ERR_IO;
}

int sku_mkdir_p(const char *path)
{
	char tmp[SKU_PATH_MAX];
	size_t n = sku_strlcpy(tmp, path, sizeof tmp);
	if (n >= sizeof tmp) return SKU_ERR_ARG;
	for (char *p = tmp + 3; *p; p++) {   /* skip "C:/" */
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
	wchar_t w[WPATH_MAX];
	if (to_wide(path, w, WPATH_MAX) != SKU_OK) return SKU_ERR_ARG;
	SetFileAttributesW(w, FILE_ATTRIBUTE_NORMAL);
	if (DeleteFileW(w)) return SKU_OK;
	return GetLastError() == ERROR_FILE_NOT_FOUND ? SKU_ERR_NOTFOUND : SKU_ERR_IO;
}

int sku_remove_dir(const char *path)
{
	wchar_t w[WPATH_MAX];
	if (to_wide(path, w, WPATH_MAX) != SKU_OK) return SKU_ERR_ARG;
	return RemoveDirectoryW(w) ? SKU_OK : SKU_ERR_IO;
}

int sku_rename(const char *from, const char *to)
{
	wchar_t wf[WPATH_MAX], wt[WPATH_MAX];
	if (to_wide(from, wf, WPATH_MAX) != SKU_OK || to_wide(to, wt, WPATH_MAX) != SKU_OK) return SKU_ERR_ARG;
	return MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ? SKU_OK : SKU_ERR_IO;
}

int sku_realpath(const char *path, char *out, size_t n)
{
	wchar_t w[WPATH_MAX], full[WPATH_MAX];
	HANDLE h;
	DWORD k;
	if (to_wide(path, w, WPATH_MAX) != SKU_OK) return SKU_ERR_ARG;
	h = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
	if (h == INVALID_HANDLE_VALUE) return SKU_ERR_NOTFOUND;
	k = GetFinalPathNameByHandleW(h, full, WPATH_MAX, FILE_NAME_NORMALIZED);
	CloseHandle(h);
	if (k == 0 || k >= WPATH_MAX) return SKU_ERR_IO;
	/* strip the \\?\ (or \\?\UNC\) prefix */
	if (wcsncmp(full, L"\\\\?\\UNC\\", 8) == 0) { full[6] = L'\\'; return from_wide(full + 6, out, n); }
	if (wcsncmp(full, L"\\\\?\\", 4) == 0) return from_wide(full + 4, out, n);
	return from_wide(full, out, n);
}

int sku_getcwd(char *out, size_t n)
{
	wchar_t w[WPATH_MAX];
	if (!GetCurrentDirectoryW(WPATH_MAX, w)) return SKU_ERR_IO;
	return from_wide(w, out, n);
}

int sku_chdir(const char *path)
{
	wchar_t w[WPATH_MAX];
	if (to_wide(path, w, WPATH_MAX) != SKU_OK) return SKU_ERR_ARG;
	return SetCurrentDirectoryW(w) ? SKU_OK : SKU_ERR_IO;
}

int sku_exe_path(char *out, size_t n)
{
	wchar_t w[WPATH_MAX];
	DWORD k = GetModuleFileNameW(NULL, w, WPATH_MAX);
	if (k == 0 || k >= WPATH_MAX) return SKU_ERR_IO;
	return from_wide(w, out, n);
}

int sku_read_file(const char *path, char **out, size_t *len)
{
	FILE *f = sku_fopen(path, "rb");
	char *buf = NULL;
	size_t cap = 0, n = 0;
	if (!f) return GetLastError() == ERROR_FILE_NOT_FOUND ? SKU_ERR_NOTFOUND : SKU_ERR_IO;
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
	if (sku_snprintf(tmp, sizeof tmp, "%s.tmp.%lu", path, (unsigned long)GetCurrentProcessId()) >= (int)sizeof tmp) return SKU_ERR_ARG;
	f = sku_fopen(tmp, "wb");
	if (!f) return SKU_ERR_IO;
	if (fwrite(data, 1, len, f) != len || fflush(f) != 0 || _commit(_fileno(f)) != 0) { fclose(f); sku_remove_file(tmp); return SKU_ERR_IO; }
	if (fclose(f) != 0) { sku_remove_file(tmp); return SKU_ERR_IO; }
	if (sku_rename(tmp, path) != SKU_OK) { sku_remove_file(tmp); return SKU_ERR_IO; }
	return SKU_OK;
}

int sku_list_dir(const char *path, sku_dir_cb cb, void *user)
{
	wchar_t w[WPATH_MAX];
	WIN32_FIND_DATAW fd;
	HANDLE h;
	int stop = 0;
	char pattern[SKU_PATH_MAX];
	sku_snprintf(pattern, sizeof pattern, "%s/*", path);
	if (to_wide(pattern, w, WPATH_MAX) != SKU_OK) return SKU_ERR_ARG;
	h = FindFirstFileExW(w, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD e = GetLastError();
		return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? SKU_ERR_NOTFOUND : SKU_ERR_IO;
	}
	do {
		char name[SKU_NAME_MAX * 4];
		bool is_link, is_dir;
		if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
		if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, NULL, NULL) <= 0) continue;
		is_link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
		is_dir  = !is_link && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		stop = cb(name, is_dir, is_link, user);
	} while (!stop && FindNextFileW(h, &fd));
	FindClose(h);
	return SKU_OK;
}

int sku_user_home(char *out, size_t n)
{
	wchar_t w[WPATH_MAX];
	if (!SHGetSpecialFolderPathW(NULL, w, CSIDL_PROFILE, TRUE)) return SKU_ERR_NOTFOUND;
	return from_wide(w, out, n);
}

int sku_user_config_dir(char *out, size_t n)
{
	wchar_t w[WPATH_MAX];
	char base[SKU_PATH_MAX];
	if (!SHGetSpecialFolderPathW(NULL, w, CSIDL_APPDATA, TRUE)) return SKU_ERR_NOTFOUND;
	if (from_wide(w, base, sizeof base) != SKU_OK) return SKU_ERR_ARG;
	sku_snprintf(out, n, "%s/SkunkCrafts/Updater", base);
	return sku_mkdir_p(out);
}

const char *sku_os_name(void)
{
	return "windows";
}

int64_t sku_time_ms(void)
{
	return (int64_t)GetTickCount64();
}

int64_t sku_time_now(void)
{
	return (int64_t)time(NULL);
}

void sku_sleep_ms(int ms)
{
	Sleep((DWORD)ms);
}

int sku_cpu_count(void)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 1;
}

void sku_format_time(char *out, size_t n, int64_t t)
{
	time_t tt = (time_t)t;
	struct tm *tm = localtime(&tt);
	if (tm) strftime(out, n, "%Y-%m-%d %H:%M:%S", tm); else sku_strlcpy(out, "?", n);
}
