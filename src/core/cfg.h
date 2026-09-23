/* "key|value" configuration files.
 *
 * Reads both the 3.x format (skunkcrafts_updater.cfg) and the v4 format
 * (skunkcrafts/updater.cfg): one "key|value" per line, split at the FIRST
 * '|', '#' comments and blank lines kept as raw lines, order preserved.
 * Saving is atomic (temporary file + rename). */
#ifndef SKU_CFG_H
#define SKU_CFG_H

#include "sku.h"

typedef struct {
	char *key;     /* NULL for a raw line (comment, blank, or a line without '|') */
	char *value;   /* the value, or the raw line itself when key is NULL */
} sku_cfg_line;

typedef struct {
	sku_cfg_line *v;
	int n, cap;
} sku_cfg;

void        sku_cfg_init(sku_cfg *c);
void        sku_cfg_free(sku_cfg *c);
int         sku_cfg_parse(sku_cfg *c, const char *text);       /* appends to c */
int         sku_cfg_load(sku_cfg *c, const char *path);
int         sku_cfg_save(const sku_cfg *c, const char *path);  /* v4: everything, in order */
int         sku_cfg_save_v1(const sku_cfg *c, const char *path); /* 3.x readable: key|value lines only */
int         sku_cfg_format(const sku_cfg *c, char **out, bool v1);

bool        sku_cfg_has(const sku_cfg *c, const char *key);
const char *sku_cfg_get(const sku_cfg *c, const char *key);    /* NULL if absent */
const char *sku_cfg_get_or(const sku_cfg *c, const char *key, const char *def);
bool        sku_cfg_get_bool(const sku_cfg *c, const char *key, bool def);
void        sku_cfg_set(sku_cfg *c, const char *key, const char *value);   /* in place, or appended */
void        sku_cfg_set_bool(sku_cfg *c, const char *key, bool value);
void        sku_cfg_set_default(sku_cfg *c, const char *key, const char *value); /* only if absent */
void        sku_cfg_remove(sku_cfg *c, const char *key);
void        sku_cfg_add_raw(sku_cfg *c, const char *line);     /* comment or blank line */

#endif
