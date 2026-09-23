#include "pack.h"
#include "platform.h"
#include "paths.h"
#include "protocol.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

void sku_pack_list_free(sku_pack_list *l)
{
	for (int i = 0; i < l->n; i++) free(l->v[i].members);
	free(l->v);
	memset(l, 0, sizeof *l);
}

static sku_pack *find(sku_pack_list *l, const char *id)
{
	for (int i = 0; i < l->n; i++) if (strcmp(l->v[i].id, id) == 0) return &l->v[i];
	return NULL;
}

int sku_packs_build(sku_addon **addons, int n, sku_pack_list *out)
{
	memset(out, 0, sizeof *out);
	for (int i = 0; i < n; i++) {
		sku_addon *a = addons[i];
		sku_pack *p;
		if (!a->pack_id[0]) continue;
		p = find(out, a->pack_id);
		if (!p) {
			if (out->n == out->cap) {
				out->cap = out->cap ? out->cap * 2 : 8;
				out->v = sku_xrealloc(out->v, (size_t)out->cap * sizeof *out->v);
			}
			p = &out->v[out->n++];
			memset(p, 0, sizeof *p);
			sku_strlcpy(p->id, a->pack_id, sizeof p->id);
		}
		if (!p->name[0] && a->pack_name[0]) sku_strlcpy(p->name, a->pack_name, sizeof p->name);
		p->members = sku_xrealloc(p->members, (size_t)(p->nmembers + 1) * sizeof *p->members);
		p->members[p->nmembers++] = i;
	}
	for (int i = 0; i < out->n; i++) if (!out->v[i].name[0]) sku_strlcpy(out->v[i].name, out->v[i].id, sizeof out->v[i].name);
	return out->n;
}

bool sku_pack_path_allowed(const char *rel)
{
	static const char *forbidden[] = {
		"Resources/default", "Resources/bitmaps", "Resources/dlls", "Resources/sounds", "Resources/CIF",
		"Resources/fonts", "Resources/shaders", "Resources/skins", "Global Scenery", "Output", "Custom Data",
	};
	if (!strchr(rel, '/')) return false;   /* a member lives in a folder, never a root-level file */
	for (size_t i = 0; i < sizeof forbidden / sizeof *forbidden; i++) {
		size_t k = strlen(forbidden[i]);
		if (sku_str_istarts(rel, forbidden[i]) && (rel[k] == 0 || rel[k] == '/' || rel[k] == ' ')) return false;
	}
	return true;
}

int sku_pack_parse_members(const char *value, sku_pack_member_decl **out)
{
	char *copy = sku_xstrdup(value), *tok, *save = NULL;
	int n = 0;
	*out = NULL;
	for (tok = strtok_r(copy, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
		char *id = sku_str_trim(tok), *path = strchr(id, '='), *url;
		sku_pack_member_decl d;
		if (!path) continue;
		*path++ = 0;
		url = strchr(path, '=');
		if (!url) continue;
		*url++ = 0;
		memset(&d, 0, sizeof d);
		sku_strlcpy(d.member_id, sku_str_trim(id), sizeof d.member_id);
		sku_strlcpy(d.path, sku_str_trim(path), sizeof d.path);
		sku_strlcpy(d.url, sku_str_trim(url), sizeof d.url);
		if (!d.member_id[0] || !d.url[0] || sku_path_check_relative(d.path) != SKU_OK || !sku_pack_path_allowed(d.path)) {
			sku_warn("pack_members: rejected \"%s\"", d.member_id[0] ? d.member_id : "?");
			continue;
		}
		*out = sku_xrealloc(*out, (size_t)(n + 1) * sizeof **out);
		(*out)[n++] = d;
	}
	free(copy);
	return n;
}

int sku_pack_missing(const sku_pack *p, sku_addon **addons, sku_pack_member_decl **out)
{
	int n = 0;
	*out = NULL;
	for (int m = 0; m < p->nmembers; m++) {
		sku_addon *a = addons[p->members[m]];
		const char *values[2] = { sku_cfg_get(&a->local, "pack_members"), sku_cfg_get(&a->remote, "pack_members") };
		for (int v = 0; v < 2; v++) {
			sku_pack_member_decl *decl;
			int nd;
			if (!values[v]) continue;
			nd = sku_pack_parse_members(values[v], &decl);
			for (int i = 0; i < nd; i++) {
				bool present = false, listed = false;
				for (int k = 0; k < p->nmembers && !present; k++) present = strcmp(addons[p->members[k]]->member_id, decl[i].member_id) == 0;
				for (int k = 0; k < n && !listed; k++) listed = strcmp((*out)[k].member_id, decl[i].member_id) == 0;
				if (present || listed) continue;
				*out = sku_xrealloc(*out, (size_t)(n + 1) * sizeof **out);
				(*out)[n++] = decl[i];
			}
			free(decl);
		}
	}
	return n;
}

int sku_pack_create_member(const sku_instance *in, const sku_pack *p, const sku_pack_member_decl *d, char *created_dir, size_t n)
{
	char rel[SKU_PATH_MAX], dir[SKU_PATH_MAX], sub[SKU_PATH_MAX], cfg_path[SKU_PATH_MAX];
	sku_cfg c;
	int rc;

	sku_strlcpy(rel, d->path, sizeof rel);
	if (sku_path_check_relative(rel) != SKU_OK || !sku_pack_path_allowed(rel)) return SKU_ERR_UNSAFE;
	sku_path_join(dir, sizeof dir, in->root, rel);
	if (sku_scan_is_addon(dir, NULL)) return SKU_OK;   /* already there (found outside the scan): nothing to do */
	sku_path_join(sub, sizeof sub, dir, SKU_DIR_V4);
	if (sku_mkdir_p(sub) != SKU_OK) return SKU_ERR_IO;

	sku_cfg_init(&c);
	sku_cfg_add_raw(&c, "# created by SkunkCrafts Updater for a pack member; the first update fills the folder");
	sku_cfg_set(&c, "name", d->member_id);
	sku_cfg_set(&c, "module", d->url);
	sku_cfg_set(&c, "version", "none");
	sku_cfg_set(&c, "zone", "custom");
	sku_cfg_set(&c, "disabled", "false");
	sku_cfg_set(&c, "channel", "stable");
	sku_cfg_set(&c, "pack", p->id);
	sku_cfg_set(&c, "member", d->member_id);
	if (p->name[0] && strcmp(p->name, p->id) != 0) sku_cfg_set(&c, "pack_name", p->name);
	sku_path_join(cfg_path, sizeof cfg_path, dir, SKU_F_V4_CFG);
	rc = sku_cfg_save(&c, cfg_path);
	sku_cfg_free(&c);
	if (rc == SKU_OK) {
		sku_info("pack %s: created member \"%s\" at %s (%s)", p->id, d->member_id, dir, d->url);
		if (created_dir) sku_strlcpy(created_dir, dir, n);
	}
	return rc;
}
