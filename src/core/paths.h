/* Path helpers and the safety check applied to every path coming from a depot. */
#ifndef SKU_PATHS_H
#define SKU_PATHS_H

#include "sku.h"

void        sku_path_join(char *out, size_t n, const char *dir, const char *rel);
void        sku_path_normalize(char *p);          /* '\' -> '/', collapse "//", drop trailing '/' */
const char *sku_path_basename(const char *p);
void        sku_path_dirname(char *out, size_t n, const char *p);
bool        sku_path_is_absolute(const char *p);

/* Relative path from a whitelist/manifest/blacklist/ignore list.
 * Normalizes separators in place, then returns SKU_OK only if the path
 * stays inside its base directory: not empty, not absolute, no "." or ".."
 * component, no drive letter, no control characters. */
int         sku_path_check_relative(char *rel);

#endif
