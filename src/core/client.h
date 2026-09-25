/* The updater's own update: a depot at the same protocol as the addons
 * (level 1 or 2), holding one package per platform under fixed names.
 * This module only detects and downloads; installing is left to the user
 * (or to a later version of the client). */
#ifndef SKU_CLIENT_H
#define SKU_CLIENT_H

#include "sku.h"
#include "http.h"
#include <stdatomic.h>

#ifndef SKU_CLIENT_DEPOT_URL
#define SKU_CLIENT_DEPOT_URL "https://1228098873.rsc.cdn77.org/sku_client"
#endif

/* Package names. The depot publishes them with the version inserted
 * ("SkunkcraftsUpdater-4.0.1-linux.zip", "sku-cli-4.0.1.zip") so that a
 * CDN serving a stale list can never point at the wrong file; the plain
 * names below are the fallback for older depots. */
#define SKU_PKG_WINDOWS_SETUP "SkunkcraftsUpdater-windows-setup.exe"
#define SKU_PKG_WINDOWS_ZIP   "SkunkcraftsUpdater-windows.zip"
#define SKU_PKG_MACOS_ZIP     "SkunkcraftsUpdater-macos.zip"
#define SKU_PKG_LINUX_ZIP     "SkunkcraftsUpdater-linux.zip"
#define SKU_PKG_LINUX_APPIMAGE "SkunkcraftsUpdater-x86_64.AppImage"
#define SKU_PKG_CLI_ZIP       "sku-cli.zip"

typedef struct {
	char     version[64];        /* published version */
	char     module[SKU_URL_MAX];/* where the depot says it lives (may differ from the URL used: follow it) */
	bool     newer;              /* published version > SKU_VERSION */
	bool     locked;             /* depot locked: nothing to download for now */
	char     package[128];       /* package for this platform, "" when the depot has none */
	uint32_t crc;
	bool     check_crc;
	int64_t  size;               /* -1 unknown */
	char    *changelog;          /* text, malloc'd, or NULL */
} sku_client_info;

/* Compares two versions "x.y.z[-suffix]" (a leading "v" ignored): <0, 0, >0.
 * Numeric parts first; a version without suffix is newer than the same with
 * one ("4.0.0" > "4.0.0-beta2"); suffixes compare by letters then number. */
int sku_client_version_cmp(const char *a, const char *b);

/* Package name for this platform and install mode: the Windows installer
 * when the executable sits next to its Uninstall.exe, the AppImage when
 * running as one, else the zip of the drop-in program. */
const char *sku_client_package(void);

/* The versioned form of a package name: "SkunkcraftsUpdater-<v>-linux.zip", "sku-cli-<v>.zip". */
void sku_client_versioned_name(const char *package, const char *version, char *out, size_t n);

/* Reads the depot: version, package entry (crc/size), changelog.
 * package NULL = sku_client_package(). Fails only when the cfg or the file
 * list cannot be read (err filled); a missing package leaves package "". */
int sku_client_check(const char *depot_url, const char *package, sku_client_info *out, char *err, size_t n);

/* Downloads info->package into dest_dir (created if needed), verifies the
 * CRC, fills dest with the final path. cb/user/cancel may be NULL. */
int sku_client_download(const char *depot_url, const sku_client_info *info, const char *dest_dir,
                        sku_progress_cb cb, void *user, atomic_int *cancel, char *dest, size_t n, char *err, size_t en);

void sku_client_info_free(sku_client_info *info);

#endif
