/* Discovery of managed addons inside an instance. */
#ifndef SKU_SCAN_H
#define SKU_SCAN_H

#include "sku.h"
#include "instance.h"

typedef enum {
	SKU_GENRE_AIRCRAFT,
	SKU_GENRE_SCENERY,
	SKU_GENRE_PLUGIN,
	SKU_GENRE_OTHER,     /* a folder at the X-Plane root */
	SKU_GENRE_CUSTOM,    /* registered by the user, anywhere */
} sku_genre;

const char *sku_genre_name(sku_genre g);

typedef struct {
	char      dir[SKU_PATH_MAX];   /* the addon folder */
	sku_genre genre;
	bool      v4;                  /* found through skunkcrafts/updater.cfg rather than the 3.x marker */
} sku_found;

typedef struct {
	sku_found *v;
	int n, cap;
} sku_found_list;

void sku_found_list_free(sku_found_list *l);

/* Walks Aircraft (depth 11, stops at .acf), Custom Scenery (2),
 * Resources/plugins (2), the root itself (1) and the custom list. Symlinked
 * folders are followed. */
int  sku_scan_instance(const sku_instance *in, sku_found_list *out);

/* True when dir holds either marker; *v4 tells which. */
bool sku_scan_is_addon(const char *dir, bool *v4);

/* Custom addon folders, kept in <data_dir>/custom_addons.txt. */
int  sku_scan_custom_load(const sku_instance *in, sku_strlist *out);
int  sku_scan_custom_add(const sku_instance *in, const char *dir);
int  sku_scan_custom_remove(const sku_instance *in, const char *dir);

#endif
