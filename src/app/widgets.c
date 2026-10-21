/* Small drawing helpers shared by the views, and the standard widgets with an
 * optional bevel (relief theme). */
#include "app.h"
#include "nuklear_config.h"
#include <string.h>

/* Keeps the END of a path visible: "…/objects/terrain/xyz.obj". */
void ui_left_truncate(app *ap, const char *path, float width, char *out, size_t n)
{
	const struct nk_user_font *f = ap->ctx->style.font;
	size_t len = strlen(path), start = 0;
	static const char ell[] = "\xE2\x80\xA6";   /* U+2026 */
	if (f->width(f->userdata, f->height, path, (int)len) <= width) { sku_strlcpy(out, path, n); return; }
	/* drop leading characters (at UTF-8 boundaries) until it fits */
	while (start < len) {
		float w;
		start++;
		while (start < len && ((unsigned char)path[start] & 0xC0) == 0x80) start++;
		sku_snprintf(out, n, "%s%s", ell, path + start);
		w = f->width(f->userdata, f->height, out, (int)strlen(out));
		if (w <= width) return;
	}
	sku_strlcpy(out, ell, n);
}

/* The same from the other end: keeps the beginning of a message and ends it
 * with an ellipsis when it is wider than width. */
void ui_right_truncate(app *ap, const char *text, float width, char *out, size_t n)
{
	const struct nk_user_font *f = ap->ctx->style.font;
	size_t len = strlen(text), end = len;
	static const char ell[] = "\xE2\x80\xA6";   /* U+2026 */
	if (f->width(f->userdata, f->height, text, (int)len) <= width) { sku_strlcpy(out, text, n); return; }
	/* drop trailing characters (at UTF-8 boundaries) until it fits */
	while (end > 0) {
		float w;
		end--;
		while (end > 0 && ((unsigned char)text[end] & 0xC0) == 0x80) end--;
		sku_snprintf(out, n, "%.*s%s", (int)end, text, ell);
		w = f->width(f->userdata, f->height, out, (int)strlen(out));
		if (w <= width) return;
	}
	sku_strlcpy(out, ell, n);
}

/* Two-tone frame: light on top/left and dark on bottom/right for a raised
 * widget, the other way round for a sunken one. Two pixels thick. */
void ui_bevel(app *ap, const struct nk_rect *r, bool raised)
{
	struct nk_command_buffer *out = nk_window_get_canvas(ap->ctx);
	struct nk_color hi  = ap->dark ? nk_rgb(100, 100, 100) : nk_rgb(255, 255, 255);
	struct nk_color hi2 = ap->dark ? nk_rgb(82, 82, 82)    : nk_rgb(236, 236, 236);
	struct nk_color lo  = ap->dark ? nk_rgb(14, 14, 14)    : nk_rgb(110, 110, 110);
	struct nk_color lo2 = ap->dark ? nk_rgb(34, 34, 34)    : nk_rgb(160, 160, 160);
	struct nk_color tl = raised ? hi : lo, tl2 = raised ? hi2 : lo2, br = raised ? lo : hi, br2 = raised ? lo2 : hi2;
	float x0 = r->x, y0 = r->y, x1 = r->x + r->w, y1 = r->y + r->h;
	if (!ap->relief || !out) return;
	nk_stroke_line(out, x0, y0 + 0.5f, x1, y0 + 0.5f, 1, tl);
	nk_stroke_line(out, x0 + 0.5f, y0, x0 + 0.5f, y1, 1, tl);
	nk_stroke_line(out, x0 + 1, y0 + 1.5f, x1 - 1, y0 + 1.5f, 1, tl2);
	nk_stroke_line(out, x0 + 1.5f, y0 + 1, x0 + 1.5f, y1 - 1, 1, tl2);
	nk_stroke_line(out, x0, y1 - 0.5f, x1, y1 - 0.5f, 1, br);
	nk_stroke_line(out, x1 - 0.5f, y0, x1 - 0.5f, y1, 1, br);
	nk_stroke_line(out, x0 + 1, y1 - 1.5f, x1 - 1, y1 - 1.5f, 1, br2);
	nk_stroke_line(out, x1 - 1.5f, y0 + 1, x1 - 1.5f, y1 - 1, 1, br2);
}

bool ui_button(app *ap, const char *label)
{
	struct nk_rect r = nk_widget_bounds(ap->ctx);
	bool down = ap->relief && nk_input_is_mouse_hovering_rect(&ap->ctx->input, r) && nk_input_is_mouse_down(&ap->ctx->input, NK_BUTTON_LEFT);
	bool hit = nk_button_label(ap->ctx, label) != 0;
	ui_bevel(ap, &r, !down);
	return hit;
}

const char *ui_tidy_line(const char *line, char *buf, size_t n)
{
	size_t k = 0;
	const char *p = line;
	while (*p == ' ' || *p == '\t') p++;
	for (; *p && k + 1 < n; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '\t') { buf[k++] = ' '; if (k + 1 < n) buf[k++] = ' '; }
		else if (c < 0x20) buf[k++] = ' ';
		else buf[k++] = (char)c;
	}
	buf[k] = 0;
	return buf;
}

/* How many bytes of s (len) fit on a line of width w: whole words, the spaces between them
 * counted; a first word wider than the line is cut between two characters (one at least). */
static size_t wrap_fit(const struct nk_user_font *f, const char *s, size_t len, float w)
{
	float space = f->width(f->userdata, f->height, " ", 1), x = 0;
	size_t p = 0, end = 0;
	while (p < len) {
		size_t q = p, r, k;
		float ww;
		while (q < len && s[q] == ' ') q++;
		if (q == len) break;
		for (r = q; r < len && s[r] != ' '; r++) ;
		ww = f->width(f->userdata, f->height, s + q, (int)(r - q));
		if (x + (float)(q - p) * space + ww <= w) { x += (float)(q - p) * space + ww; end = r; p = r; continue; }
		if (end) return end;
		/* one word wider than the line: as many characters as fit, one at least */
		k = q;
		do {
			size_t next = k + 1;
			while (next < r && ((unsigned char)s[next] & 0xC0) == 0x80) next++;
			if (k > q && f->width(f->userdata, f->height, s + q, (int)(next - q)) > w) break;
			k = next;
		} while (k < r);
		return k;
	}
	return end ? end : len;
}

void ui_text_wrapped(app *ap, const char *text, float row, const struct nk_color *color)
{
	struct nk_context *ctx = ap->ctx;
	const struct nk_user_font *f = ctx->style.font;
	struct nk_color c = color ? *color : ctx->style.text.color;
	size_t len = strlen(text), done = 0;
	float full, indent = 0, pad = 2 * ctx->style.text.padding.x + 2;
	nk_layout_row_dynamic(ctx, row, 1);
	if (!len) { nk_label(ctx, "", NK_TEXT_LEFT); return; }
	full = nk_widget_bounds(ctx).w;
	if (len > 2 && (text[0] == '-' || text[0] == '*') && text[1] == ' ') indent = f->width(f->userdata, f->height, text, 2);   /* a list item: the next lines under its text */
	while (done < len) {
		bool hang = done > 0 && indent > 0;   /* the next lines of a list item, under its text */
		float w = hang ? full - indent - ctx->style.window.spacing.x : full;
		size_t n = wrap_fit(f, text + done, len - done, w - pad);
		if (hang) {
			nk_layout_row_begin(ctx, NK_STATIC, row, 2);
			nk_layout_row_push(ctx, indent);
			nk_label(ctx, "", NK_TEXT_LEFT);
			nk_layout_row_push(ctx, w);
		} else if (done > 0) nk_layout_row_dynamic(ctx, row, 1);
		nk_text_colored(ctx, text + done, (int)n, NK_TEXT_LEFT, c);
		if (hang) nk_layout_row_end(ctx);
		done += n;
		while (done < len && text[done] == ' ') done++;
	}
}

float ui_fill_height(app *ap, float margin, float min)
{
	struct nk_context *ctx = ap->ctx;
	struct nk_rect r = nk_window_get_content_region(ctx);
	float used = ctx->current->layout->at_y - r.y;   /* rows already laid out, spacing included */
	float h = r.h - used - margin;
	return h < min ? min : h;
}

void ui_group_tail(app *ap)
{
	struct nk_context *ctx = ap->ctx;
	nk_layout_row_dynamic(ctx, ctx->style.window.padding.y + 2, 1);
	nk_label(ctx, "", NK_TEXT_LEFT);
}

bool ui_needs_update(sku_status st)
{
	return st == SKU_ST_UPDATE || st == SKU_ST_REPAIR || st == SKU_ST_VERSION_MISMATCH;
}

bool ui_button_hl(app *ap, const char *label, bool highlight)
{
	struct nk_context *ctx = ap->ctx;
	struct nk_color bg = ap->dark ? nk_rgb(0, 120, 190) : nk_rgb(0, 110, 190);
	struct nk_color hover = ap->dark ? nk_rgb(0, 150, 225) : nk_rgb(0, 130, 215);
	bool hit;
	if (!highlight) return ui_button(ap, label);
	nk_style_push_style_item(ctx, &ctx->style.button.normal, nk_style_item_color(bg));
	nk_style_push_style_item(ctx, &ctx->style.button.hover, nk_style_item_color(hover));
	nk_style_push_color(ctx, &ctx->style.button.text_normal, nk_rgb(255, 255, 255));
	nk_style_push_color(ctx, &ctx->style.button.text_hover, nk_rgb(255, 255, 255));
	hit = ui_button(ap, label);
	nk_style_pop_color(ctx); nk_style_pop_color(ctx);
	nk_style_pop_style_item(ctx); nk_style_pop_style_item(ctx);
	return hit;
}

/* A tab: raised when inactive, sunken when it is the current one. */
bool ui_tab(app *ap, const char *label, bool active)
{
	struct nk_rect r = nk_widget_bounds(ap->ctx);
	nk_bool sel = active;
	bool hit = nk_selectable_label(ap->ctx, label, NK_TEXT_CENTERED, &sel) != 0;
	if (ap->relief) ui_bevel(ap, &r, !active);
	else nk_stroke_rect(nk_window_get_canvas(ap->ctx), r, 3, 1, ap->dark ? nk_rgb(100, 100, 100) : nk_rgb(150, 150, 150));
	return hit;
}

/* A tooltip painted beside a widget rather than Nuklear's: that one is a window opened under
 * the mouse, which takes the next click, and a popup (a combo's list) cannot have one. The
 * box ends at x = right, starts at y = top, and is moved to stay inside the main window. */
static void tip_paint(app *ap, struct nk_command_buffer *out, float right, float top, const char *text)
{
	struct nk_context *ctx = ap->ctx;
	const struct nk_user_font *f = ctx->style.font;
	struct nk_window *mw = nk_window_find(ctx, "main");
	struct nk_vec2 pad = ctx->style.window.padding;
	struct nk_color bg = ap->dark ? nk_rgb(64, 64, 64) : nk_rgb(255, 255, 225);
	struct nk_color line = ap->dark ? nk_rgb(120, 120, 120) : nk_rgb(150, 150, 150);
	struct nk_rect clip = out->clip, all, box;
	int len = (int)strlen(text);
	float tw = f->width(f->userdata, f->height, text, len);
	if (!mw) return;
	all = mw->bounds;
	box = nk_rect(right - tw - 2 * pad.x, top, tw + 2 * pad.x, f->height + 2 * pad.y);
	if (box.x + box.w > all.x + all.w) box.x = all.x + all.w - box.w;
	if (box.x < all.x) box.x = all.x;
	if (box.y + box.h > all.y + all.h) box.y = all.y + all.h - box.h;
	nk_push_scissor(out, all);
	nk_fill_rect(out, box, 0, bg);
	nk_stroke_rect(out, box, 0, 1, line);
	nk_draw_text(out, nk_rect(box.x + pad.x, box.y + pad.y, tw, f->height), text, len, f, bg, ctx->style.text.color);
	nk_push_scissor(out, clip);
}

void ui_tip_below(app *ap, const struct nk_rect *r, const char *text)
{
	struct nk_context *ctx = ap->ctx;
	if (!text || !*text || ctx->current->popup.win || !nk_input_is_mouse_hovering_rect(&ctx->input, *r)) return;
	sku_strlcpy(ap->tip, text, sizeof ap->tip);
	ap->tip_right = r->x + r->w;
	ap->tip_top = r->y + r->h + 2;
}

void ui_tip_flush(app *ap)
{
	if (!ap->tip[0]) return;
	tip_paint(ap, nk_window_get_canvas(ap->ctx), ap->tip_right, ap->tip_top, ap->tip);
	ap->tip[0] = 0;
}

/* A combo box whose popup stands out from the window background, with a thin
 * separator between entries. colors (optional) gives one text color per item,
 * tips (optional) a tooltip per item, shown left of the list. */
static int combo(app *ap, const char **items, const struct nk_color *colors, const char **tips, int n, int selected, int item_height, float w, float h)
{
	struct nk_context *ctx = ap->ctx;
	struct nk_rect r = nk_widget_bounds(ctx);
	struct nk_color popup = ap->dark ? (ap->relief ? nk_rgb(64, 64, 64) : nk_rgb(52, 52, 52)) : (ap->relief ? nk_rgb(212, 212, 212) : nk_rgb(255, 255, 255));
	struct nk_color line = ap->dark ? nk_rgb(100, 100, 100) : nk_rgb(170, 170, 170);
	int now = selected;

	if (colors) {
		nk_style_push_color(ctx, &ctx->style.combo.label_normal, colors[selected]);
		nk_style_push_color(ctx, &ctx->style.combo.label_hover, colors[selected]);
		nk_style_push_color(ctx, &ctx->style.combo.label_active, colors[selected]);
	}
	/* a combo popup is a "dynamic" panel: Nuklear paints it row by row with window.background */
	nk_style_push_color(ctx, &ctx->style.window.background, popup);
	nk_style_push_style_item(ctx, &ctx->style.contextual_button.normal, nk_style_item_color(popup));
	nk_style_push_float(ctx, &ctx->style.window.combo_border, 1);
	nk_style_push_color(ctx, &ctx->style.window.combo_border_color, line);
	if (nk_combo_begin_label(ctx, items[selected], nk_vec2(w, h))) {
		float spacing = ctx->style.window.spacing.y;
		struct nk_rect tip_at = {0};
		int tip = -1;
		nk_layout_row_dynamic(ctx, (float)item_height, 1);
		for (int i = 0; i < n; i++) {
			struct nk_rect ib = nk_widget_bounds(ctx);
			if (tips && tips[i] && nk_input_is_mouse_hovering_rect(&ctx->input, ib)) { tip = i; tip_at = ib; }
			if (colors) {
				nk_style_push_color(ctx, &ctx->style.contextual_button.text_normal, colors[i]);
				nk_style_push_color(ctx, &ctx->style.contextual_button.text_hover, colors[i]);
				nk_style_push_color(ctx, &ctx->style.contextual_button.text_active, colors[i]);
			}
			if (nk_combo_item_label(ctx, items[i], NK_TEXT_LEFT)) now = i;
			if (colors) { nk_style_pop_color(ctx); nk_style_pop_color(ctx); nk_style_pop_color(ctx); }
			if (i < n - 1) nk_stroke_line(nk_window_get_canvas(ctx), ib.x, ib.y + ib.h + spacing / 2, ib.x + ib.w, ib.y + ib.h + spacing / 2, 1, line);
		}
		if (tip >= 0) {
			/* left of the list, level with the entry; below the list when too wide for that side */
			struct nk_rect pb = nk_window_get_bounds(ctx);
			const struct nk_user_font *f = ctx->style.font;
			float need = f->width(f->userdata, f->height, tips[tip], (int)strlen(tips[tip])) + 2 * ctx->style.window.padding.x + 2;
			if (pb.x - need >= 0) tip_paint(ap, nk_window_get_canvas(ctx), pb.x - 2, tip_at.y, tips[tip]);
			else tip_paint(ap, nk_window_get_canvas(ctx), pb.x + pb.w, pb.y + pb.h + 2, tips[tip]);
		}
		nk_combo_end(ctx);
	}
	nk_style_pop_color(ctx);
	nk_style_pop_float(ctx);
	nk_style_pop_style_item(ctx);
	nk_style_pop_color(ctx);
	if (colors) { nk_style_pop_color(ctx); nk_style_pop_color(ctx); nk_style_pop_color(ctx); }
	ui_bevel(ap, &r, true);
	return now;
}

int ui_combo_ex(app *ap, const char **items, const struct nk_color *colors, int n, int selected, int item_height, float w, float h)
{
	return combo(ap, items, colors, NULL, n, selected, item_height, w, h);
}

int ui_combo(app *ap, const char **items, int n, int selected, int item_height, float w, float h)
{
	return combo(ap, items, NULL, NULL, n, selected, item_height, w, h);
}

int ui_combo_tips(app *ap, const char **items, const char **tips, int n, int selected, int item_height, float w, float h)
{
	return combo(ap, items, NULL, tips, n, selected, item_height, w, h);
}

const float ui_list_default_cols[4] = { 0.12f, 0.32f, 0.10f, 0.46f };

void ui_column_header(app *ap, const char **titles, const float *defaults, float *cols, int n, float row)
{
	struct nk_context *ctx = ap->ctx;
	struct nk_input *in = &ctx->input;
	static int dragging = -1;
	static float start_x, start[8];
	static SDL_Cursor *ew_cursor;
	static bool cursor_set;
	struct nk_rect cell[8];
	struct nk_command_buffer *out;
	struct nk_color line = ap->dark ? nk_rgb(100, 100, 100) : nk_rgb(150, 150, 150);
	struct nk_color accent = ap->dark ? nk_rgb(0, 191, 255) : nk_rgb(0, 110, 190);
	struct nk_color title = ap->dark ? nk_rgb(170, 170, 170) : nk_rgb(90, 90, 90);
	float x0, x1, total;
	int hover = -1;

	if (n > 8) n = 8;
	nk_layout_row(ctx, NK_DYNAMIC, row, n, cols);
	for (int i = 0; i < n; i++) {
		cell[i] = nk_widget_bounds(ctx);
		nk_label_colored(ctx, titles[i], NK_TEXT_LEFT, title);
	}
	out = nk_window_get_canvas(ctx);
	x0 = cell[0].x; x1 = cell[n - 1].x + cell[n - 1].w; total = x1 - x0;
	if (total <= 0) return;

	for (int i = 0; i < n - 1; i++) {
		float bx = cell[i].x + cell[i].w + ctx->style.window.spacing.x / 2;
		struct nk_rect zone = nk_rect(bx - 5, cell[i].y, 10, cell[i].h);
		bool over = nk_input_is_mouse_hovering_rect(in, zone);
		if (over) hover = i;
		nk_stroke_line(out, bx, cell[i].y + 2, bx, cell[i].y + cell[i].h - 2, 1, (over || dragging == i) ? accent : line);
		if (over && nk_input_is_mouse_pressed(in, NK_BUTTON_DOUBLE)) { memcpy(cols, defaults, (size_t)n * sizeof *cols); prefs_save(ap); dragging = -1; }
		else if (dragging < 0 && over && nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT)) { dragging = i; start_x = in->mouse.pos.x; memcpy(start, cols, (size_t)n * sizeof *cols); }
	}
	if (dragging >= 0) {
		if (nk_input_is_mouse_down(in, NK_BUTTON_LEFT)) {
			int i = dragging;
			float d = (in->mouse.pos.x - start_x) / total, a = start[i] + d, b = start[i + 1] - d;
			if (a < 0.05f) { b += a - 0.05f; a = 0.05f; }
			if (b < 0.05f) { a += b - 0.05f; b = 0.05f; }
			cols[i] = a; cols[i + 1] = b;
		} else { dragging = -1; prefs_save(ap); }
	}
	nk_stroke_line(out, x0, cell[0].y + cell[0].h + 1, x1, cell[0].y + cell[0].h + 1, 1, line);

	/* resize cursor over a boundary */
	if (hover >= 0 || dragging >= 0) {
		if (!ew_cursor) ew_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_EW_RESIZE);
		if (ew_cursor && !cursor_set) { SDL_SetCursor(ew_cursor); cursor_set = true; }
	} else if (cursor_set) { SDL_SetCursor(SDL_GetDefaultCursor()); cursor_set = false; }
}
