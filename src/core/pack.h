/* Packs: several addon folders managed as one product (simHeaven style).
 *
 * Membership is declared in each member's cfg ("pack|<id>", "member|<id>",
 * optional "pack_name|<display name>"); the client groups them by identity,
 * so renaming folders does not matter. Members that do not exist yet can be
 * declared by any member (local or remote cfg) with
 *   pack_members|<member-id>=<path relative to the X-Plane root>=<module url>;...
 * and are created on request, never silently. */
#ifndef SKU_PACK_H
#define SKU_PACK_H

#include "sku.h"
#include "addon.h"

typedef struct {
	char id[64];
	char name[SKU_NAME_MAX];
	int *members;          /* indexes into the addon array given to sku_packs_build */
	int  nmembers;
} sku_pack;

typedef struct {
	sku_pack *v;
	int n, cap;
} sku_pack_list;

typedef struct {
	char member_id[64];
	char path[SKU_PATH_MAX];   /* relative to the X-Plane root */
	char url[SKU_URL_MAX];
} sku_pack_member_decl;

void sku_pack_list_free(sku_pack_list *l);

/* Groups the addons that carry a pack id. Addons without one are left alone. */
int  sku_packs_build(sku_addon **addons, int n, sku_pack_list *out);

/* Members declared by the pack's members (local + remote cfgs) but not
 * present among the addons. Returns the number found; *out is malloc'd. */
int  sku_pack_missing(const sku_pack *p, sku_addon **addons, sku_pack_member_decl **out);

/* Parses a pack_members value; rejects unsafe paths. Returns the count. */
int  sku_pack_parse_members(const char *value, sku_pack_member_decl **out);

/* Creates <root>/<path>/skunkcrafts/updater.cfg for a declared member so the
 * next check/update fills the folder. Refuses paths outside the root or in
 * X-Plane's own folders. */
int  sku_pack_create_member(const sku_instance *in, const sku_pack *p, const sku_pack_member_decl *d, char *created_dir, size_t n);

/* True when the path may host an addon (not an X-Plane system folder). */
bool sku_pack_path_allowed(const char *rel);

#endif
