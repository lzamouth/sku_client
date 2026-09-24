/* An X-Plane installation ("instance"): its root folder and our data folder
 * inside it (Output/skunkcrafts/updater). */
#ifndef SKU_INSTANCE_H
#define SKU_INSTANCE_H

#include "sku.h"

typedef struct {
	char root[SKU_PATH_MAX];        /* normalized, no trailing slash */
	char data_dir[SKU_PATH_MAX];    /* <root>/Output/skunkcrafts/updater */
	char log_path[SKU_PATH_MAX];
	bool read_only;
} sku_instance;

/* True when dir has the four folders every X-Plane install has. */
bool sku_instance_is_root(const char *dir);

/* Fills in, creates data_dir (and its cache/reports subfolders), opens the
 * log there. Nothing else is written. With read_only nothing at all is
 * created or written (the log goes to stderr only): dry runs. */
int  sku_instance_open(sku_instance *in, const char *dir, bool read_only);
void sku_instance_close(sku_instance *in);

/* Finds the instance the executable was started from: the current directory,
 * the executable's directory, and on macOS the .app bundle's parent. */
int  sku_instance_detect(char *out, size_t n);

/* Installations listed by X-Plane itself (x-plane_install_12.txt, _11.txt). */
int  sku_instance_known(sku_strlist *out);

/* Paths inside data_dir for a given addon folder (a hash of the folder path). */
void sku_instance_cache_path(const sku_instance *in, const char *addon_dir, char *out, size_t n);   /* base name for the cached remote lists and docs */
void sku_instance_report_path(const sku_instance *in, const char *addon_dir, char *out, size_t n);

#endif
