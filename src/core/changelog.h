/* Changelog and readme of an addon: where to find them, how to split a
 * changelog into version sections. */
#ifndef SKU_CHANGELOG_H
#define SKU_CHANGELOG_H

#include "sku.h"
#include "addon.h"

typedef struct {
	char version[64];     /* "" for text before the first header */
	int  first_line;      /* inclusive */
	int  last_line;       /* inclusive */
	bool newer;           /* more recent than the installed version */
} sku_changelog_section;

/* True when the line is a section header; the bare version is copied to out. */
bool sku_changelog_is_header(const char *line, char *out, size_t n);

/* Splits text into sections (malloc'd array, count returned). Sections
 * before the one matching installed_version are flagged "newer"; when the
 * installed version is not found, nothing is flagged. */
int  sku_changelog_sections(const char *text, const char *installed_version, sku_changelog_section **out);

/* Best available copy of the addon's changelog or readme ("changelog" /
 * "readme"): the one fetched from the depot at the last check, else the
 * file shipped inside the addon folder. Returns SKU_ERR_NOTFOUND if none. */
int  sku_addon_doc_path(const sku_addon *a, const char *kind, char *out, size_t n);

/* Cache path of a fetched document (internal, used by addon_check.c). */
void sku_addon_doc_cache_path(const sku_addon *a, const char *kind, char *out, size_t n);

#endif
