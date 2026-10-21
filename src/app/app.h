/* Graphical client: shared state and the functions each ui_*.c provides.
 *
 * Threading: the core runs check/update jobs on worker threads (jobs.c).
 * The UI thread never touches an addon that has a job queued or running
 * (item.busy); it displays the snapshot taken when the addon was idle plus
 * the live status/progress, which the core publishes thread-safely. */
#ifndef SKU_APP_H
#define SKU_APP_H

#include <SDL3/SDL.h>
#include <stdatomic.h>
#include "sku.h"
#include "instance.h"
#include "scan.h"
#include "addon.h"
#include "pack.h"
#include "client.h"

/* Nuklear is included by main.c only; the views receive an opaque context. */
struct nk_context;
struct nk_font;

#define APP_NAME    "SkunkCrafts Updater"
#define APP_MAX_CHANNELS 8

/* Static widths are designed for a 16 px text: scale them with the text size. */
#define UI_W(ap, px) ((float)(px) * (float)(ap)->prefs.text_size / 16.0f)

typedef struct {
	sku_addon *a;
	atomic_int busy;                /* set by the submitter, cleared by the worker */
	/* snapshot of the addon, refreshed while idle */
	char name[SKU_NAME_MAX], version[64], remote_version[64], dir[SKU_PATH_MAX], source[SKU_URL_MAX];
	char channel[64], channels[APP_MAX_CHANNELS][64];
	int  nchannels;
	sku_genre genre;
	bool disabled, liveries, checked;
	int  n_new, n_changed, n_delete, n_ignored, n_error, n_unchanged;
	int64_t bytes;
	int  pack;                      /* index into app.packs, or -1 */
	/* live status (read every frame) */
	sku_status st;
	char st_text[256], st_detail[512];
	int  prog_done, prog_total;
	int64_t bytes_done, bytes_total;
} app_item;

typedef enum { VIEW_LIST, VIEW_DETAIL, VIEW_PACK, VIEW_OPTIONS, VIEW_ABOUT, VIEW_INSTANCE } app_view;
typedef enum { THEME_SYSTEM, THEME_DARK, THEME_LIGHT } app_theme;

typedef struct {
	app_theme theme;
	bool relief;           /* bevelled widgets instead of flat ones */
	int  video;            /* Linux: 0 auto (X11 first, Wayland if no X server), 1 Wayland, 2 X11; applied at start */
	float cols[4];         /* list columns (channel, name, version, status) as fractions of the width */
	int  text_size;        /* 10..32 */
	int  downloads;        /* 1..32 */
	int  crc_threads;      /* 1..64 */
	int  startup;          /* 0 nothing, 1 check all, 2 update all */
	bool verbose;
	char instance[SKU_PATH_MAX];      /* last used X-Plane root */
	sku_strlist instances;            /* roots added by hand (browse) */
	int  window_w, window_h;          /* the normal (not maximized) size */
	bool window_pos;                  /* a position was saved: window_x/y, and the display it was on */
	int  window_x, window_y;
	char window_display[128];         /* SDL's name of that display, "" if unknown... */
	int  window_dx, window_dy;        /* ...and the position relative to it */
	bool window_max;                  /* maximized when closed */
	bool client_check;                /* daily check for a new version of the updater */
	char client_url[SKU_URL_MAX];     /* its depot (follows the module the depot announces) */
	int64_t client_last;              /* last check, seconds since the epoch */
	char client_skip[64];             /* "Later" on this version: no banner until the next one */
} app_prefs;

typedef enum { CLIENT_IDLE, CLIENT_CHECKING, CLIENT_CHECKED, CLIENT_DOWNLOADING, CLIENT_DOWNLOADED, CLIENT_INSTALLED, CLIENT_FAILED } client_state;

typedef struct app {
	SDL_Window   *win;
	SDL_Renderer *ren;
	struct nk_context *ctx;
	struct nk_font *font, *bold, *mono;
	float scale;
	bool  running;
	bool  dark;                       /* effective theme */
	bool  relief;                     /* effective: prefs.relief, or SKU_RELIEF=1 for tests */

	app_prefs prefs;
	char prefs_path[SKU_PATH_MAX];

	sku_instance inst;
	bool inst_open;
	sku_strlist known_instances;      /* for the chooser */
	sku_strlist switch_roots;         /* the installations offered by the selector on the tab row... */
	char switch_for[SKU_PATH_MAX];    /* ...built for this current root */
	char window_note[192];            /* how the window was placed at start, for the log (opened later) */
	char tip[SKU_PATH_MAX];           /* tooltip to paint at the end of the frame (ui_tip_below), or "" */
	float tip_right, tip_top;
	char browse_result[SKU_PATH_MAX]; /* filled by the folder dialog callback */
	atomic_int browse_pending;        /* 1 = instance, 2 = custom addon folder */

	app_item *items;
	int nitems;
	int selected;                     /* index or -1 */
	char remove_channel[64];          /* detail view: channel of the selected addon awaiting confirmation of its removal, or "" */
	sku_pack_list packs;
	int selected_pack;                /* index into packs, or -1 */
	bool detail_from_pack;            /* "Back" from a member returns to its pack */
	sku_pack_member_decl *missing;    /* declared but absent members of selected_pack */
	int nmissing, missing_for;
	app_view view;
	bool filter_aircraft, filter_scenery, filter_plugins, filter_other;
	int  detail_tab;                  /* 0 files, 1 report, 2 changelog, 3 readme */
	char *report_text;                /* last report loaded for the detail view */
	int  report_for;                  /* item index the report belongs to, -1 */
	char *doc_text;                   /* changelog or readme loaded for the detail view */
	int  doc_for, doc_kind;           /* item index and tab the document belongs to */

	struct jobs *jobs;
	int  last_jobs_done;              /* to detect completed jobs and refresh snapshots */
	char message[256];                /* transient message in the status bar */
	int64_t message_until;
	/* the updater's own update (ui_client.c): one background thread at a time */
	sku_client_info client;           /* written by the thread, read by the UI once client_state is CHECKED */
	atomic_int client_state;          /* client_state */
	atomic_int client_cancel;
	_Atomic int64_t client_done;      /* download progress */
	char client_err[256];
	char client_file[SKU_PATH_MAX];   /* downloaded package */
	bool client_seen;                 /* the UI reacted to the last check */
	bool client_dismissed;            /* banner closed */
	bool client_manual;               /* "Check now": show failures too */
	int  about_mode;                  /* 0 license, 1 notices, 2 what's new */
	char **argv;                      /* the command line, for a relaunch after a self-update */
	int64_t frame_start, events_ms;   /* for the slow-frame trace in the debug log */
	bool resizing;                    /* a resize event was in the last batch */
	char shot_dir[SKU_PATH_MAX];      /* --screenshot DIR: one BMP per second, for testing */
	int64_t last_shot;
	int shot_index;
} app;

/* prefs.c */
void prefs_defaults(app_prefs *p);
int  prefs_load(app *ap);
int  prefs_save(app *ap);
void prefs_migrate_legacy(app *ap);       /* Output/preferences/skunkcrafts_updater_go.json + old log */

/* jobs.c */
typedef enum { JOB_CHECK, JOB_CHECK_FULL, JOB_UPDATE, JOB_RELOAD } job_kind;
struct jobs *jobs_create(app *ap, int nthreads);
void jobs_destroy(struct jobs *j);
void jobs_submit(struct jobs *j, app_item *it, job_kind kind);
int  jobs_running(struct jobs *j);
int  jobs_queued(struct jobs *j);
int  jobs_done(struct jobs *j);           /* completed since start (monotonic) */
void jobs_cancel_all(app *ap);

/* main.c helpers used by the views */
void app_open_instance(app *ap, const char *root);
void app_close_instance(app *ap);
void app_rescan(app *ap);
void app_refresh_item(app_item *it);      /* snapshot, only when idle */
void app_refresh_live(app_item *it);      /* status + progress */
void app_message(app *ap, const char *fmt, ...);
void app_post_notices(app *ap);   /* the core's notices (beta file absorbed, channel offered) in the status bar */
void app_set_text_size(app *ap, int size);
void app_apply_theme(app *ap);
bool app_any_busy(app *ap);
void app_browse_folder(app *ap, int what);
void app_relaunch(app *ap, const char *exe);      /* starts exe with this program's arguments, then quits */

/* theme.c */
struct nk_color;
const struct nk_color *app_default_colors(void);   /* main.c: Nuklear's default table */
struct nk_color status_color(sku_status s, bool dark);
void theme_apply(struct nk_context *ctx, bool dark, bool relief);

/* ui_*.c: each draws inside the already opened main window */
void ui_list(app *ap);
void ui_pack(app *ap);
void app_packs_rebuild(app *ap);
void app_pack_summary(app *ap, int pack, char *version, size_t vn, sku_status *st, char *text, size_t tn);
void ui_detail(app *ap);
void ui_options(app *ap);
void ui_about(app *ap);
void ui_instance(app *ap);
void ui_statusbar(app *ap);

/* ui_client.c */
void app_client_maybe_check(app *ap);     /* at startup: once a day */
void app_client_check_start(app *ap);
void app_client_download_start(app *ap);
void app_client_poll(app *ap);            /* every frame: reacts to a finished check */
bool ui_client_banner(app *ap);           /* above every view; true when a row was drawn */
void ui_client_options(app *ap);          /* in the Options view */
void ui_client_news(app *ap);             /* the published changelog, in About */

/* widgets.c: the standard widgets, with a bevel when the relief theme is on */
struct nk_rect;
void ui_left_truncate(app *ap, const char *path, float width, char *out, size_t n);
void ui_right_truncate(app *ap, const char *text, float width, char *out, size_t n);
void ui_bevel(app *ap, const struct nk_rect *r, bool raised);
bool ui_button(app *ap, const char *label);
bool ui_button_hl(app *ap, const char *label, bool highlight);   /* accent colours when highlight: "this is what to do next" */
bool ui_needs_update(sku_status st);                             /* update, repair or version mismatch */
/* A text line made displayable: leading blanks dropped, tabs and control characters replaced by spaces. */
const char *ui_tidy_line(const char *line, char *buf, size_t n);
/* A line of text wrapped between words at the panel's width, one row of height row per line;
 * the next lines of a "- " item start under its text. color NULL: the text color. */
void ui_text_wrapped(app *ap, const char *text, float row, const struct nk_color *color);
/* Height left in the current panel below the rows laid out so far, minus margin; at least min. */
float ui_fill_height(app *ap, float margin, float min);
/* Last row of a scrolled group: Nuklear clips the bottom padding, this keeps the last real line visible. */
void ui_group_tail(app *ap);
bool ui_tab(app *ap, const char *label, bool active);
int  ui_combo(app *ap, const char **items, int n, int selected, int item_height, float w, float h);
struct nk_color;
int  ui_combo_ex(app *ap, const char **items, const struct nk_color *colors, int n, int selected, int item_height, float w, float h);
int  ui_combo_tips(app *ap, const char **items, const char **tips, int n, int selected, int item_height, float w, float h);
/* Tooltip for the widget at r when the mouse is over it, painted below it at the end of the frame (ui_tip_flush). */
void ui_tip_below(app *ap, const struct nk_rect *r, const char *text);
void ui_tip_flush(app *ap);
struct nk_color ui_channel_color(app *ap, const char *channel);   /* stable blue, the others yellow */
#define UI_MAIN_CHANNEL "Main"                                     /* what the "stable" token is called on screen */
const char *ui_channel_label(const char *channel);               /* "stable" -> UI_MAIN_CHANNEL, others unchanged */
/* Column titles over a list; the boundaries can be dragged (double-click: defaults). cols is updated and saved. */
void ui_column_header(app *ap, const char **titles, const float *defaults, float *cols, int n, float row);
extern const float ui_list_default_cols[4];

#endif
