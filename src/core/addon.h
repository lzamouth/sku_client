/* One managed addon: local configuration, remote state, the update plan and
 * the live status shown by the user interface.
 *
 * Threading: check/update run on the caller's thread and use worker pools
 * internally. Status and progress are readable at any time from another
 * thread through sku_addon_status() and the atomics. Everything else is
 * owned by the caller's thread. */
#ifndef SKU_ADDON_H
#define SKU_ADDON_H

#include "sku.h"
#include "cfg.h"
#include "instance.h"
#include "scan.h"
#include "protocol.h"
#include <stdatomic.h>
#include <pthread.h>

typedef enum {
	SKU_ST_LOCAL,             /* only the local configuration is known */
	SKU_ST_DISABLED,          /* updates disabled by the user */
	SKU_ST_CHECKING,
	SKU_ST_UP_TO_DATE,
	SKU_ST_VERSION_MISMATCH,  /* files match, local and remote versions differ */
	SKU_ST_REPAIR,            /* same version, files missing or changed */
	SKU_ST_UPDATE,            /* newer version available */
	SKU_ST_LOCKED,            /* depot locked by the developer */
	SKU_ST_UPDATING,
	SKU_ST_UPDATED,
	SKU_ST_CANCELLED,
	SKU_ST_ERROR_CONFIG,      /* local or remote configuration problem */
	SKU_ST_ERROR_NET,
	SKU_ST_ERROR_FILES,       /* some files could not be read, downloaded or deleted */
} sku_status;

const char *sku_status_name(sku_status s);

typedef enum { SKU_PLAN_NEW, SKU_PLAN_CHANGED, SKU_PLAN_DELETE, SKU_PLAN_IGNORED, SKU_PLAN_ERROR } sku_reason;
const char *sku_reason_name(sku_reason r);

typedef struct {
	char      *path;       /* relative to the addon folder */
	sku_reason reason;
	int64_t    size;       /* -1 unknown */
	uint32_t   crc;
	bool       check_crc;
	int        result;     /* after update: SKU_OK or an error code */
	char      *detail;     /* after update: error text, or NULL */
} sku_plan_item;

typedef struct {
	sku_plan_item *v;
	int n, cap;
	int n_new, n_changed, n_delete, n_ignored, n_error, n_unchanged, n_cached;
	int64_t bytes;         /* to download */
	int64_t duration_ms;
} sku_plan;

typedef struct {
	char name[64];
	char module[SKU_URL_MAX];
	char zone[64];
} sku_channel;

typedef struct {
	const sku_instance *inst;
	char       dir[SKU_PATH_MAX];
	sku_genre  genre;
	bool       v4;               /* local files are in the v4 layout */
	bool       dirty;            /* in-memory local cfg differs from disk */

	/* local configuration (unified model, whatever the on-disk layout) */
	sku_cfg    local;
	char       name[SKU_NAME_MAX];
	char       version[64];
	char       module[SKU_URL_MAX];
	char       zone[64];
	bool       disabled;
	bool       liveries;         /* aircraft only */
	char       channel[64];      /* "stable" or a channel name */
	sku_channel *channels;
	int        nchannels;
	char       pack_id[64], member_id[64], pack_name[SKU_NAME_MAX];
	sku_strlist ignore;

	/* remote state, valid after a successful check */
	char       base_url[SKU_URL_MAX];
	sku_cfg    remote;
	int        level;
	bool       checked;
	char       remote_version[64];
	sku_remote_files files;
	sku_strlist blacklist;
	sku_plan   plan;
	int        lists_from_cache;         /* remote lists answered 304 at the last check */
	char       changelog[SKU_PATH_MAX];  /* relative path declared by the depot, or "" */
	char       readme[SKU_PATH_MAX];

	/* live status */
	pthread_mutex_t lock;
	sku_status status;
	char       status_text[256];
	char       status_detail[512];
	atomic_int  prog_done, prog_total;    /* files, during check and update */
	atomic_llong bytes_done, bytes_total; /* during update */
	atomic_int  cancel;
} sku_addon;

typedef struct {
	bool full;            /* ignore the CRC cache, hash everything */
	bool dry_run;         /* never write anything (cfg, cache, report) */
	int  crc_threads;     /* 0 = number of CPUs */
} sku_check_opts;

typedef struct {
	bool dry_run;         /* plan and report only, no download or deletion */
	int  download_threads;/* 0 = 8 */
	int  retries;         /* per file, 0 = 2 */
} sku_update_opts;

sku_addon *sku_addon_open(const sku_instance *in, const char *dir, sku_genre genre);
void       sku_addon_close(sku_addon *a);
int        sku_addon_reload(sku_addon *a);       /* re-reads the local files */
int        sku_addon_save(sku_addon *a);         /* writes the v4 layout and removes the 3.x files */

int        sku_addon_check(sku_addon *a, const sku_check_opts *opts);
int        sku_addon_update(sku_addon *a, const sku_update_opts *opts);
void       sku_addon_cancel(sku_addon *a);

/* User settings: applied in memory, saved unless dry_run. */
int        sku_addon_set_disabled(sku_addon *a, bool disabled, bool dry_run);
int        sku_addon_set_liveries(sku_addon *a, bool liveries, bool dry_run);
int        sku_addon_set_channel(sku_addon *a, const char *channel, bool dry_run);

/* Thread-safe status snapshot. */
sku_status sku_addon_status(sku_addon *a, char *text, size_t tn, char *detail, size_t dn);

/* internal, shared by addon*.c */
void       sku_addon_set_status(sku_addon *a, sku_status s, const char *text, const char *detail);
void       sku_plan_clear(sku_plan *p);
sku_plan_item *sku_plan_add(sku_plan *p, const char *path, sku_reason reason, int64_t size, uint32_t crc, bool check_crc);
void       sku_addon_encode_url(char *out, size_t n, const char *base_url, const char *rel);
/* Renames a channel (keys channel.<from>.* -> channel.<to>.*, active channel too); a depot
 * announces its channel name with "channel|<name>" in its cfg. Saves unless dry_run. */
int        sku_addon_rename_channel(sku_addon *a, const char *from, const char *to, bool dry_run);
/* <addon>/skunkcrafts/crc_cache.txt, its folder created unless dry_run */
void       sku_addon_crc_cache_path(const sku_addon *a, bool dry_run, char *out, size_t n);

#endif
