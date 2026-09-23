#include "log.h"
#include "platform.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>

static FILE *log_file;
static int level = SKU_LOG_INFO;
static bool mirror;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static const char *names[] = { "ERROR", "WARN ", "INFO ", "DEBUG" };

int sku_log_open(const char *path, int64_t rotate_bytes)
{
	sku_fileinfo st;
	sku_log_close();
	if (rotate_bytes > 0 && sku_stat(path, &st) == SKU_OK && st.size > rotate_bytes) {
		char old[SKU_PATH_MAX];
		sku_snprintf(old, sizeof old, "%s.1", path);
		sku_rename(path, old);
	}
	pthread_mutex_lock(&lock);
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
	if (log_file) { fprintf(log_file, "%s %s %s\n", when, names[l], msg); fflush(log_file); }
	if (mirror || (!log_file && l <= SKU_LOG_WARN)) fprintf(stderr, "%s %s\n", names[l], msg);
	pthread_mutex_unlock(&lock);
}
