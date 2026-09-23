/* sku: command-line client. Same core as the graphical client.
 *
 *   sku [options] instances            list the X-Plane installations X-Plane knows about
 *   sku [options] list                 list the managed addons of an instance
 *   sku [options] check  [name...]     check for updates (all addons, or those matching)
 *   sku [options] update [name...]     check then update/repair
 *   sku [options] report [name...]     print the last saved report(s)
 *
 * Options: --xp DIR  --full  --dry-run  --verbose  --threads N  --downloads N  --report
 */
#include "sku.h"
#include "platform.h"
#include "paths.h"
#include "log.h"
#include "http.h"
#include "instance.h"
#include "scan.h"
#include "addon.h"
#include "report.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct opts {
	const char *xp;
	bool full, dry_run, verbose, save_report;
	int threads, downloads;
};

static void usage(void)
{
	fputs("usage: sku [--xp DIR] [--full] [--dry-run] [--verbose] [--threads N] [--downloads N] [--report]\n"
	      "           instances | list | check [name...] | update [name...] | report [name...]\n", stderr);
}

static int cmd_instances(void)
{
	sku_strlist l = {0};
	char here[SKU_PATH_MAX];
	sku_instance_known(&l);
	for (int i = 0; i < l.n; i++) printf("%s\n", l.v[i]);
	if (sku_instance_detect(here, sizeof here) == SKU_OK && !sku_strlist_has(&l, here)) printf("%s  (current)\n", here);
	if (!l.n && !*here) { fprintf(stderr, "no X-Plane installation found\n"); return 1; }
	sku_strlist_free(&l);
	return 0;
}

static bool matches(const sku_addon *a, char **names, int n)
{
	if (n == 0) return true;
	for (int i = 0; i < n; i++)
		if (sku_str_istarts(a->name, names[i]) || strstr(a->name, names[i]) || strstr(a->dir, names[i])) return true;
	return false;
}

static void print_addon(sku_addon *a)
{
	char text[256], detail[512];
	sku_status st = sku_addon_status(a, text, sizeof text, detail, sizeof detail);
	printf("%-8s %-16s %-36s %-14s %s\n", sku_genre_name(a->genre), sku_status_name(st), a->name, a->version, text);
	if (*detail) printf("%-8s %-16s %-36s %-14s   %s\n", "", "", "", "", detail);
}

static void print_plan(const sku_addon *a)
{
	for (int i = 0; i < a->plan.n; i++) {
		const sku_plan_item *it = &a->plan.v[i];
		if (it->reason == SKU_PLAN_IGNORED) continue;
		printf("    %-8s %s%s%s\n", sku_reason_name(it->reason), it->path, it->detail ? "  # " : "", it->detail ? it->detail : "");
	}
}

int main(int argc, char **argv)
{
	struct opts o = { NULL, false, false, false, false, 0, 0 };
	const char *cmd = NULL;
	char **names = NULL;
	int nnames = 0, i, rc = 0;
	char root[SKU_PATH_MAX];
	sku_instance in;
	sku_found_list found = {0};

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--xp") == 0 && i + 1 < argc) o.xp = argv[++i];
		else if (strcmp(argv[i], "--full") == 0) o.full = true;
		else if (strcmp(argv[i], "--dry-run") == 0) o.dry_run = true;
		else if (strcmp(argv[i], "--verbose") == 0) o.verbose = true;
		else if (strcmp(argv[i], "--report") == 0) o.save_report = true;
		else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) o.threads = atoi(argv[++i]);
		else if (strcmp(argv[i], "--downloads") == 0 && i + 1 < argc) o.downloads = atoi(argv[++i]);
		else if (strcmp(argv[i], "--version") == 0) { printf("sku %s\n%s\n", SKU_VERSION, sku_http_version()); return 0; }
		else if (argv[i][0] == '-') { usage(); return 2; }
		else if (!cmd) cmd = argv[i];
		else { names = &argv[i]; nnames = argc - i; break; }
	}
	if (!cmd) { usage(); return 2; }
	if (strcmp(cmd, "instances") == 0) return cmd_instances();

	if (o.xp) sku_strlcpy(root, o.xp, sizeof root);
	else if (sku_instance_detect(root, sizeof root) != SKU_OK) {
		fprintf(stderr, "no X-Plane installation here: use --xp DIR (see \"sku instances\")\n");
		return 1;
	}
	if (sku_instance_open(&in, root, o.dry_run) != SKU_OK) { fprintf(stderr, "%s is not an X-Plane installation (or Output/ is not writable)\n", root); return 1; }
	sku_log_set_level(o.verbose ? SKU_LOG_DEBUG : SKU_LOG_INFO);
	sku_log_set_stderr(o.verbose || o.dry_run);
	sku_http_global_init_default(NULL);
	if (o.dry_run) sku_info("dry run: nothing will be written");

	sku_scan_instance(&in, &found);
	printf("X-Plane: %s  (%d addon(s))\n", in.root, found.n);

	for (i = 0; i < found.n; i++) {
		sku_addon *a = sku_addon_open(&in, found.v[i].dir, found.v[i].genre);
		if (!matches(a, names, nnames)) { sku_addon_close(a); continue; }
		if (strcmp(cmd, "list") == 0) {
			print_addon(a);
		} else if (strcmp(cmd, "check") == 0) {
			sku_check_opts co = { o.full, o.dry_run, o.threads };
			if (sku_addon_check(a, &co) != SKU_OK) rc = 1;
			print_addon(a);
			if (o.verbose) print_plan(a);
			if (o.save_report && !o.dry_run) sku_report_save(a, "Check");
		} else if (strcmp(cmd, "update") == 0) {
			sku_check_opts co = { o.full, o.dry_run, o.threads };
			sku_update_opts uo = { o.dry_run, o.downloads, 0 };
			if (sku_addon_check(a, &co) == SKU_OK && a->checked) {
				if (o.dry_run) print_plan(a);
				if (sku_addon_update(a, &uo) != SKU_OK) rc = 1;
			} else rc = 1;
			print_addon(a);
			if (!o.dry_run) { print_plan(a); sku_report_save(a, "Update"); }
		} else if (strcmp(cmd, "report") == 0) {
			char path[SKU_PATH_MAX], *text;
			sku_instance_report_path(&in, a->dir, path, sizeof path);
			if (sku_read_file(path, &text, NULL) == SKU_OK) { printf("%s\n", text); free(text); }
			else printf("%s: no report yet\n", a->name);
		} else { usage(); rc = 2; }
		sku_addon_close(a);
		if (rc == 2) break;
	}
	sku_found_list_free(&found);
	sku_http_global_cleanup();
	sku_instance_close(&in);
	return rc;
}
