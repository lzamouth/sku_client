#include "log.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>

#define LOG_KEEP 3   /* previous logs kept: <path>.1 (the last) to <path>.3 */

static FILE *log_file;
static int level = SKU_LOG_INFO;
static bool mirror, rotate_at_open;
static char rotated[8][SKU_PATH_MAX];   /* logs already rotated by this process */
static int nrotated;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static const char *names[] = { "ERROR", "WARN ", "INFO ", "DEBUG" };

/* <path>.2 -> .3, .1 -> .2, then <path> is COPIED to .1 and emptied in place.
 * The log keeps its file (same inode): a viewer following it, klogg or glogg,
 * sees it truncated and reloads, where a renamed log would leave it on the old
 * file. The copy also works on Windows, where an open file cannot be renamed. */
static void rotate(const char *path)
{
	char from[SKU_PATH_MAX], to[SKU_PATH_MAX], *text;
	size_t len;
	FILE *f;
	for (int i = LOG_KEEP - 1; i >= 1; i--) {
		sku_snprintf(from, sizeof from, "%s.%d", path, i);
		sku_snprintf(to, sizeof to, "%s.%d", path, i + 1);
		if (sku_is_file(from)) sku_rename(from, to);
	}
	if (sku_read_file(path, &text, &len) != SKU_OK) return;
	sku_snprintf(to, sizeof to, "%s.1", path);
	if (len && sku_write_file_atomic(to, text, len) == SKU_OK && (f = sku_fopen(path, "wb")) != NULL) fclose(f);
	free(text);
}

void sku_log_rotate_at_open(bool on)
{
	rotate_at_open = on;
}

void sku_log_rotate_reset_for_tests(void)
{
	nrotated = 0;
}

int sku_log_open(const char *path)
{
	bool first = true;
	sku_log_close();
	pthread_mutex_lock(&lock);
	for (int i = 0; i < nrotated; i++) if (strcmp(rotated[i], path) == 0) first = false;
	/* once per launch and per log, when the application asks for it; never on size:
	 * a session's log stays whole for debugging. The CLI appends. */
	if (rotate_at_open && first) {
		rotate(path);
		if (nrotated < (int)(sizeof rotated / sizeof *rotated)) sku_strlcpy(rotated[nrotated++], path, sizeof rotated[0]);
	}
	log_file = sku_fopen(path, "ab");
	pthread_mutex_unlock(&lock);
	return log_file ? SKU_OK : SKU_ERR_IO;
}

void sku_log_close(void)
{
	pthread_mutex_lock(&lock);
	if (log_file) fclose(log_file);
	log_file = NULL;
	pthread_mutex_unlock(&lock);
}

void sku_log_set_level(int l)
{
	level = l;
}

void sku_log_set_stderr(bool m)
{
	mirror = m;
}

void sku_log(int l, const char *fmt, ...)
{
	char msg[2048], when[32];
	va_list ap;
	if (l > level) return;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	sku_format_time(when, sizeof when, sku_time_now());
	pthread_mutex_lock(&lock);
	if (log_file) {
		fprintf(log_file, "%s %s %s\n", when, names[l], msg);
		fflush(log_file);
	}
	if (mirror || (!log_file && l <= SKU_LOG_WARN)) fprintf(stderr, "%s %s\n", names[l], msg);
	pthread_mutex_unlock(&lock);
}
