/* sku: command-line client. Same core as the graphical client.
 *
 *   sku [options] instances            list the X-Plane installations X-Plane knows about
 *   sku [options] list                 list the managed addons (packs grouped) of an instance
 *   sku [options] check  [name...]     check for updates (all addons, or those matching), lists what would change
 *   sku [options] update [name...]     check then update/repair, lists what was done
 *   sku [options] report [name...]     print the last saved report(s)
 *   sku [options] changelog [name...]  print the changelog (new sections marked with ">")
 *   sku [options] readme [name...]     print the readme
 *
 * name matches an addon name/folder or a pack name/id (then all its members).
 * Options: --xp DIR  --full  --dry-run  --verbose (debug log mirrored to stderr)  --threads N  --downloads N
 *          --report  --create-members (update: create the pack members declared but absent)
 */
#include "sku.h"
#include "platform.h"
#include "paths.h"
#include "log.h"
#include "http.h"
#include "instance.h"
#include "scan.h"
#include "addon.h"
#include "pack.h"
#include "report.h"
#include "changelog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct opts {
	const char *xp;
	bool full, dry_run, verbose, save_report, create_members;
	int threads, downloads;
};

static void usage(void)
{
	fputs("usage: sku [--xp DIR] [--full] [--dry-run] [--verbose] [--threads N] [--downloads N] [--report] [--create-members]\n"
	      "           instances | list | check [name...] | update [name...] | report [name...] | changelog [name...] | readme [name...]\n", stderr);
}

static int cmd_instances(void)
{
	sku_strlist l = {0};
	char here[SKU_PATH_MAX] = "";
	sku_instance_known(&l);
	for (int i = 0; i < l.n; i++) printf("%s\n", l.v[i]);
	if (sku_instance_detect(here, sizeof here) == SKU_OK && !sku_strlist_has(&l, here)) printf("%s  (current)\n", here);
	if (!l.n && !*here) { fprintf(stderr, "no X-Plane installation found\n"); return 1; }
	sku_strlist_free(&l);
	return 0;
}

static bool name_matches(const char *name, const char *dir, char **names, int n)
{
	if (n == 0) return true;
	for (int i = 0; i < n; i++)
		if (sku_str_istarts(name, names[i]) || strstr(name, names[i]) || (dir && strstr(dir, names[i]))) return true;
	return false;
}

static void print_addon(sku_addon *a, const char *indent)
{
	char text[256], detail[512];
	sku_status st = sku_addon_status(a, text, sizeof text, detail, sizeof detail);
	printf("%s%-8s %-16s %-36s %-14s %s\n", indent, sku_genre_name(a->genre), sku_status_name(st), a->name, a->version, text);
	if (*detail) printf("%s%-8s %-16s %-36s %-14s   %s\n", indent, "", "", "", "", detail);
}

static void print_plan(sku_addon *a)
{
	for (int i = 0; i < a->plan.n; i++) {
		const sku_plan_item *it = &a->plan.v[i];
		if (it->reason == SKU_PLAN_IGNORED) continue;
		printf("    %-8s %s%s%s\n", sku_reason_name(it->reason), it->path, it->detail ? "  # " : "", it->detail ? it->detail : "");
	}
}

/* list / check / update / report on one addon; returns non-zero on failure */
static int run_command(const char *cmd, sku_addon *a, const struct opts *o, const sku_instance *in, const char *indent)
{
	int rc = 0;
	if (strcmp(cmd, "list") == 0) {
		print_addon(a, indent);
	} else if (strcmp(cmd, "check") == 0) {
		sku_check_opts co = { o->full, o->dry_run, o->threads };
		if (sku_addon_check(a, &co) != SKU_OK) rc = 1;
		print_addon(a, indent);
		print_plan(a);
		if (o->save_report && !o->dry_run) sku_report_save(a, "Check");
	} else if (strcmp(cmd, "update") == 0) {
		sku_check_opts co = { o->full, o->dry_run, o->threads };
		sku_update_opts uo = { o->dry_run, o->downloads, 0 };
		if (sku_addon_check(a, &co) == SKU_OK && a->checked) {
			if (o->dry_run) print_plan(a);
			if (sku_addon_update(a, &uo) != SKU_OK) rc = 1;
		} else rc = 1;
		print_addon(a, indent);
		if (!o->dry_run) { print_plan(a); sku_report_save(a, "Update"); }
	} else if (strcmp(cmd, "changelog") == 0 || strcmp(cmd, "readme") == 0) {
		char path[SKU_PATH_MAX], *text;
		if (sku_addon_doc_path(a, cmd, path, sizeof path) != SKU_OK || sku_read_file(path, &text, NULL) != SKU_OK) {
			printf("%s: no %s available (run a check first, or the depot does not publish one)\n", a->name, cmd);
			return 0;
		}
		printf("==== %s (%s)\n", a->name, path);
		if (strcmp(cmd, "changelog") == 0) {
			sku_changelog_section *sec;
			int ns = sku_changelog_sections(text, a->version, &sec), lineno = 0;
			char *copy = sku_xstrdup(text), *cursor = copy, *line;
			while ((line = sku_next_line(&cursor)) != NULL) {
				bool newer = false;
				for (int s = 0; s < ns; s++) if (lineno >= sec[s].first_line && lineno <= sec[s].last_line) newer = sec[s].newer;
				printf("%s %s\n", newer ? ">" : " ", line);
				lineno++;
			}
			free(copy); free(sec);
		} else printf("%s\n", text);
		free(text);
	} else if (strcmp(cmd, "report") == 0) {
		char path[SKU_PATH_MAX], *text;
		sku_instance_report_path(in, a->dir, path, sizeof path);
		if (sku_read_file(path, &text, NULL) == SKU_OK) { printf("%s\n", text); free(text); }
		else printf("%s: no report yet\n", a->name);
	}
	return rc;
}

int main(int argc, char **argv)
{
	struct opts o = { NULL, false, false, false, false, false, 0, 0 };
	const char *cmd = NULL;
	char **names = NULL;
	int nnames = 0, i, rc = 0;
	char root[SKU_PATH_MAX];
	sku_instance in;
	sku_found_list found = {0};
	sku_addon **addons = NULL;
	int naddons = 0;
	sku_pack_list packs = {0};
	bool *in_pack;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--xp") == 0 && i + 1 < argc) o.xp = argv[++i];
		else if (strcmp(argv[i], "--full") == 0) o.full = true;
		else if (strcmp(argv[i], "--dry-run") == 0) o.dry_run = true;
		else if (strcmp(argv[i], "--verbose") == 0) o.verbose = true;
		else if (strcmp(argv[i], "--report") == 0) o.save_report = true;
		else if (strcmp(argv[i], "--create-members") == 0) o.create_members = true;
		else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) o.threads = atoi(argv[++i]);
		else if (strcmp(argv[i], "--downloads") == 0 && i + 1 < argc) o.downloads = atoi(argv[++i]);
		else if (strcmp(argv[i], "--version") == 0) { printf("sku %s\n%s\n", SKU_VERSION, sku_http_version()); return 0; }
		else if (argv[i][0] == '-') { usage(); return 2; }
		else if (!cmd) cmd = argv[i];
		else { names = &argv[i]; nnames = argc - i; break; }
	}
	if (!cmd) { usage(); return 2; }
	if (strcmp(cmd, "instances") == 0) return cmd_instances();
	if (strcmp(cmd, "list") && strcmp(cmd, "check") && strcmp(cmd, "update") && strcmp(cmd, "report") && strcmp(cmd, "changelog") && strcmp(cmd, "readme")) { usage(); return 2; }

	if (o.xp) sku_strlcpy(root, o.xp, sizeof root);
	else if (sku_instance_detect(root, sizeof root) != SKU_OK) {
		fprintf(stderr, "no X-Plane installation here: use --xp DIR (see \"sku instances\")\n");
		return 1;
	}
	if (sku_instance_open(&in, root, o.dry_run) != SKU_OK) { fprintf(stderr, "%s is not an X-Plane installation (or Output/ is not writable)\n", root); return 1; }
	sku_log_set_level(o.verbose ? SKU_LOG_DEBUG : SKU_LOG_INFO);
	sku_log_set_stderr(o.verbose);
	sku_http_global_init_default(NULL);
	if (o.dry_run) sku_info("dry run: nothing will be written");

	sku_scan_instance(&in, &found);
	addons = sku_xcalloc((size_t)found.n + 1, sizeof *addons);
	for (i = 0; i < found.n; i++) addons[naddons++] = sku_addon_open(&in, found.v[i].dir, found.v[i].genre);
	sku_packs_build(addons, naddons, &packs);
	printf("X-Plane: %s  (%d addon(s), %d pack(s))\n", in.root, naddons, packs.n);
	in_pack = sku_xcalloc((size_t)naddons + 1, sizeof *in_pack);
	for (i = 0; i < packs.n; i++) for (int m = 0; m < packs.v[i].nmembers; m++) in_pack[packs.v[i].members[m]] = true;

	/* standalone addons */
	for (i = 0; i < naddons; i++) {
		if (in_pack[i] || !name_matches(addons[i]->name, addons[i]->dir, names, nnames)) continue;
		if (run_command(cmd, addons[i], &o, &in, "")) rc = 1;
	}
	/* packs: matched by their own name/id or by a member's name */
	for (i = 0; i < packs.n; i++) {
		sku_pack *p = &packs.v[i];
		sku_pack_member_decl *missing = NULL;
		int nmissing;
		bool match = name_matches(p->name, p->id, names, nnames);
		for (int m = 0; m < p->nmembers && !match; m++) match = name_matches(addons[p->members[m]]->name, addons[p->members[m]]->dir, names, nnames);
		if (!match) continue;
		printf("%-8s %-16s %-36s %d member(s)\n", "Pack", "", p->name, p->nmembers);
		nmissing = sku_pack_missing(p, addons, &missing);
		for (int k = 0; k < nmissing; k++) {
			if (strcmp(cmd, "update") == 0 && o.create_members && !o.dry_run) {
				char dir[SKU_PATH_MAX];
				if (sku_pack_create_member(&in, p, &missing[k], dir, sizeof dir) == SKU_OK) {
					printf("    created member \"%s\" at %s\n", missing[k].member_id, missing[k].path);
					addons = sku_xrealloc(addons, ((size_t)naddons + 2) * sizeof *addons);
					addons[naddons] = sku_addon_open(&in, dir, SKU_GENRE_CUSTOM);
					p->members = sku_xrealloc(p->members, (size_t)(p->nmembers + 1) * sizeof *p->members);
					p->members[p->nmembers++] = naddons++;
				} else { printf("    cannot create member \"%s\" at %s\n", missing[k].member_id, missing[k].path); rc = 1; }
			} else printf("    missing member \"%s\" (%s) - use \"update --create-members\" to create it\n", missing[k].member_id, missing[k].path);
		}
		free(missing);
		for (int m = 0; m < p->nmembers; m++)
			if (run_command(cmd, addons[p->members[m]], &o, &in, "    ")) rc = 1;
	}

	free(in_pack);
	sku_pack_list_free(&packs);
	for (i = 0; i < naddons; i++) sku_addon_close(addons[i]);
	free(addons);
	sku_found_list_free(&found);
	sku_http_global_cleanup();
	sku_instance_close(&in);
	return rc;
}
