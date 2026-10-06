/* Process-wide log file (one per X-Plane instance) with size-based rotation. */
#ifndef SKU_LOG_H
#define SKU_LOG_H

#include "sku.h"

enum sku_log_level { SKU_LOG_ERROR, SKU_LOG_WARN, SKU_LOG_INFO, SKU_LOG_DEBUG };

/* Rotation keeps path.1 (the last) to path.3. It happens once per path, at the first open,
 * when sku_log_rotate_at_open(true) was called (the application, at each launch; the CLI
 * appends), never on size: a session's log stays whole. The current log is copied then
 * emptied in place, so viewers following it (klogg, glogg) reload it. */
int  sku_log_open(const char *path);
void sku_log_rotate_at_open(bool on);
void sku_log_rotate_reset_for_tests(void);                  /* forgets the paths rotated: a new launch */
void sku_log_close(void);
void sku_log_set_level(int level);                          /* default SKU_LOG_INFO */
void sku_log_set_stderr(bool mirror);                       /* also print to stderr (CLI) */
void sku_log(int level, const char *fmt, ...);

#define sku_error(...) sku_log(SKU_LOG_ERROR, __VA_ARGS__)
#define sku_warn(...)  sku_log(SKU_LOG_WARN,  __VA_ARGS__)
#define sku_info(...)  sku_log(SKU_LOG_INFO,  __VA_ARGS__)
#define sku_debug(...) sku_log(SKU_LOG_DEBUG, __VA_ARGS__)

#endif
