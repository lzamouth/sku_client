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

/* A combo box whose popup stands out from the window background, with a thin
 * separator between entries. colors (optional) gives one text color per item. */
int ui_combo_ex(app *ap, const char **items, const struct nk_color *colors, int n, int selected, int item_height, float w, float h)
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
		nk_layout_row_dynamic(ctx, (float)item_height, 1);
		for (int i = 0; i < n; i++) {
			struct nk_rect ib = nk_widget_bounds(ctx);
			if (colors) {
				nk_style_push_color(ctx, &ctx->style.contextual_button.text_normal, colors[i]);
				nk_style_push_color(ctx, &ctx->style.contextual_button.text_hover, colors[i]);
				nk_style_push_color(ctx, &ctx->style.contextual_button.text_active, colors[i]);
			}
			if (nk_combo_item_label(ctx, items[i], NK_TEXT_LEFT)) now = i;
			if (colors) { nk_style_pop_color(ctx); nk_style_pop_color(ctx); nk_style_pop_color(ctx); }
			if (i < n - 1) nk_stroke_line(nk_window_get_canvas(ctx), ib.x, ib.y + ib.h + spacing / 2, ib.x + ib.w, ib.y + ib.h + spacing / 2, 1, line);
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

int ui_combo(app *ap, const char **items, int n, int selected, int item_height, float w, float h)
{
	return ui_combo_ex(ap, items, NULL, n, selected, item_height, w, h);
}
