/* The depot protocol: file names, URL rules and parsers for the remote lists.
 * Level 1 = what 3.x clients speak; level 2 (protocol|2 in the cfg) adds the manifest, one
 * "path|crc|size" line per file in place of the whitelist and sizeslist. */
#ifndef SKU_PROTOCOL_H
#define SKU_PROTOCOL_H

#include "sku.h"
#include "cfg.h"

#define SKU_F_CFG        "skunkcrafts_updater.cfg"
#define SKU_F_BETA_CFG   "skunkcrafts_updater_beta.cfg"
#define SKU_F_IGNORE     "skunkcrafts_updater_ignore.txt"
#define SKU_F_WHITELIST  "skunkcrafts_updater_whitelist.txt"
#define SKU_F_SIZES      "skunkcrafts_updater_sizeslist.txt"
#define SKU_F_BLACKLIST  "skunkcrafts_updater_blacklist.txt"
#define SKU_F_MANIFEST   "skunkcrafts_updater_manifest.txt"
#define SKU_F_CHANGELOG  "changelog.txt"
#define SKU_F_README     "readme.txt"

/* The client's private folder inside the addon (CRC cache). The 4.0 betas up to beta9 also
 * moved the configuration and the ignore list there; those files are migrated back to the
 * 3.x names, which aircraft and other tools read (the version string, for one). */
#define SKU_DIR_V4       "skunkcrafts"
#define SKU_F_V4_CFG     "skunkcrafts/updater.cfg"    /* legacy, migrated back to skunkcrafts_updater.cfg */
#define SKU_F_V4_IGNORE  "skunkcrafts/ignore.txt"     /* legacy, folded into skunkcrafts_updater_ignore.txt */
#define SKU_F_V4_CRC     "skunkcrafts/crc_cache.txt"   /* CRC cache: moves and dies with the addon */
#define SKU_D_CHANNELS   "skunkcrafts/channels"        /* one <name>.cfg per beta channel: channel|, module|, zone| */
#define SKU_F_CHANNEL    "skunkcrafts/channel.cfg"     /* the active channel: channel|<name>, "stable" for the main one */

typedef struct {
	char    *path;        /* relative, normalized, validated */
	uint32_t crc;
	bool     check_crc;   /* false when the depot says -1 */
	int64_t  size;        /* -1 when unknown */
} sku_remote_file;

typedef struct {
	sku_remote_file *v;
	int n, cap;
} sku_remote_files;

void sku_remote_files_free(sku_remote_files *f);

/* Parsers. Unsafe or malformed lines are skipped and counted in *rejected. */
int  sku_protocol_parse_whitelist(const char *text, sku_remote_files *out, int *rejected);   /* path|crc */
int  sku_protocol_apply_sizes(const char *text, sku_remote_files *files);                   /* path|bytes */
int  sku_protocol_parse_manifest(const char *text, sku_remote_files *out, int *rejected);    /* path|crc|size[|...] */
int  sku_protocol_parse_pathlist(const char *text, sku_strlist *out, int *rejected);        /* blacklist, ignore list */

/* Base URL from zone + module, always ending with '/'. */
/* Files at the addon root that belong to the client or the user, whatever a depot lists:
 * 1 = skunkcrafts_updater.cfg and skunkcrafts_updater_ignore.txt (never downloaded nor deleted),
 * 2 = a tester file skunkcrafts_updater_beta*.cfg (read as a channel offer), 0 = an ordinary file. */
int  sku_protocol_client_file(const char *rel);
void sku_protocol_base_url(char *out, size_t n, const char *zone, const char *module);

/* Validates a remote cfg (mandatory keys); fills err on failure. */
int  sku_protocol_check_remote_cfg(const sku_cfg *remote, char *err, size_t n);

/* Protocol level announced by a remote cfg (1 when absent). */
int  sku_protocol_level(const sku_cfg *remote);

#endif
