/* Colors: the dark theme of the 3.x client, a light counterpart, and the
 * status colors (same meaning as 3.x: green = ok, cyan = update, ...). */
#include "app.h"
#include "nuklear_config.h"
#include <string.h>

struct nk_color ui_channel_color(app *ap, const char *channel)
{
	if (strcmp(channel, "stable") == 0) return ap->dark ? nk_rgb(100, 180, 255) : nk_rgb(0, 110, 190);
	return ap->dark ? nk_rgb(255, 210, 60) : nk_rgb(170, 120, 0);
}

struct nk_color status_color(sku_status s, bool dark)
{
	switch (s) {
	case SKU_ST_LOCAL:            return dark ? nk_rgb(192, 192, 192) : nk_rgb(90, 90, 90);
	case SKU_ST_DISABLED:         return nk_rgb(255, 165, 0);
	case SKU_ST_CHECKING:
	case SKU_ST_UPDATING:         return dark ? nk_rgb(255, 218, 185) : nk_rgb(180, 110, 40);
	case SKU_ST_UP_TO_DATE:
	case SKU_ST_UPDATED:          return dark ? nk_rgb(64, 255, 64) : nk_rgb(0, 140, 0);
	case SKU_ST_VERSION_MISMATCH: return nk_rgb(186, 85, 211);
	case SKU_ST_REPAIR:           return dark ? nk_rgb(100, 149, 237) : nk_rgb(40, 90, 200);
	case SKU_ST_UPDATE:           return dark ? nk_rgb(0, 255, 255) : nk_rgb(0, 130, 160);
	case SKU_ST_LOCKED:
	case SKU_ST_CANCELLED:
	case SKU_ST_ERROR_FILES:      return dark ? nk_rgb(255, 255, 0) : nk_rgb(170, 130, 0);
	case SKU_ST_ERROR_CONFIG:
	case SKU_ST_ERROR_NET:        return nk_rgb(255, 60, 60);
	}
	return nk_rgb(255, 255, 255);
}

void theme_apply(struct nk_context *ctx, bool dark, bool relief)
{
	struct nk_color t[NK_COLOR_COUNT];
	memcpy(t, app_default_colors(), sizeof t);
	if (relief && dark) {
		/* classic 3D look: mid greys, no rounding, bevels drawn by widgets.c */
		t[NK_COLOR_TEXT]                    = nk_rgba(235, 235, 235, 255);
		t[NK_COLOR_WINDOW]                  = nk_rgba(46, 46, 46, 255);
		t[NK_COLOR_HEADER]                  = nk_rgba(46, 46, 46, 255);
		t[NK_COLOR_BORDER]                  = nk_rgba(20, 20, 20, 255);
		t[NK_COLOR_BUTTON]                  = nk_rgba(64, 64, 64, 255);
		t[NK_COLOR_BUTTON_HOVER]            = nk_rgba(80, 80, 80, 255);
		t[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba(50, 50, 50, 255);
		t[NK_COLOR_TOGGLE]                  = nk_rgba(36, 36, 36, 255);
		t[NK_COLOR_TOGGLE_HOVER]            = nk_rgba(48, 48, 48, 255);
		t[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(120, 200, 255, 255);
		t[NK_COLOR_SELECT]                  = nk_rgba(64, 64, 64, 255);
		t[NK_COLOR_SELECT_ACTIVE]           = nk_rgba(36, 36, 36, 255);
		t[NK_COLOR_SLIDER]                  = nk_rgba(36, 36, 36, 255);
		t[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(120, 200, 255, 255);
		t[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(150, 215, 255, 255);
		t[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(180, 230, 255, 255);
		t[NK_COLOR_PROPERTY]                = nk_rgba(36, 36, 36, 255);
		t[NK_COLOR_EDIT]                    = nk_rgba(36, 36, 36, 255);
		t[NK_COLOR_EDIT_CURSOR]             = nk_rgba(235, 235, 235, 255);
		t[NK_COLOR_COMBO]                   = nk_rgba(64, 64, 64, 255);
		t[NK_COLOR_CHART]                   = nk_rgba(36, 36, 36, 255);
		t[NK_COLOR_CHART_COLOR]             = nk_rgba(120, 200, 255, 255);
		t[NK_COLOR_SCROLLBAR]               = nk_rgba(48, 48, 48, 255);
		t[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba(100, 100, 100, 255);
		t[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba(120, 120, 120, 255);
		t[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(150, 150, 150, 255);
		t[NK_COLOR_TAB_HEADER]              = nk_rgba(64, 64, 64, 255);
	} else if (dark) {
		t[NK_COLOR_TEXT]                    = nk_rgba(240, 240, 240, 255);
		t[NK_COLOR_WINDOW]                  = nk_rgba(32, 32, 32, 255);
		t[NK_COLOR_HEADER]                  = nk_rgba(48, 48, 48, 255);
		t[NK_COLOR_BORDER]                  = nk_rgba(0, 0, 0, 255);
		t[NK_COLOR_BUTTON]                  = nk_rgba(64, 64, 64, 255);
		t[NK_COLOR_BUTTON_HOVER]            = nk_rgba(96, 96, 96, 255);
		t[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba(0, 150, 220, 255);
		t[NK_COLOR_TOGGLE]                  = nk_rgba(80, 80, 80, 255);
		t[NK_COLOR_TOGGLE_HOVER]            = nk_rgba(110, 110, 110, 255);
		t[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(0, 191, 255, 255);
		t[NK_COLOR_SELECT]                  = nk_rgba(58, 58, 58, 255);
		t[NK_COLOR_SELECT_ACTIVE]           = nk_rgba(70, 70, 110, 255);
		t[NK_COLOR_SLIDER]                  = nk_rgba(56, 56, 56, 255);
		t[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(0, 191, 255, 255);
		t[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(80, 210, 255, 255);
		t[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(120, 220, 255, 255);
		t[NK_COLOR_PROPERTY]                = nk_rgba(56, 56, 56, 255);
		t[NK_COLOR_EDIT]                    = nk_rgba(56, 56, 56, 255);
		t[NK_COLOR_EDIT_CURSOR]             = nk_rgba(240, 240, 240, 255);
		t[NK_COLOR_COMBO]                   = nk_rgba(56, 56, 56, 255);
		t[NK_COLOR_CHART]                   = nk_rgba(56, 56, 56, 255);
		t[NK_COLOR_CHART_COLOR]             = nk_rgba(0, 191, 255, 255);
		t[NK_COLOR_SCROLLBAR]               = nk_rgba(40, 40, 40, 255);
		t[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba(96, 96, 96, 255);
		t[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba(120, 120, 120, 255);
		t[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(150, 150, 150, 255);
		t[NK_COLOR_TAB_HEADER]              = nk_rgba(48, 48, 48, 255);
	} else {
		t[NK_COLOR_TEXT]                    = nk_rgba(30, 30, 30, 255);
		t[NK_COLOR_WINDOW]                  = nk_rgba(240, 240, 240, 255);
		t[NK_COLOR_HEADER]                  = nk_rgba(210, 210, 210, 255);
		t[NK_COLOR_BORDER]                  = nk_rgba(160, 160, 160, 255);
		t[NK_COLOR_BUTTON]                  = nk_rgba(205, 205, 205, 255);
		t[NK_COLOR_BUTTON_HOVER]            = nk_rgba(185, 185, 185, 255);
		t[NK_COLOR_BUTTON_ACTIVE]           = nk_rgba(120, 180, 230, 255);
		t[NK_COLOR_TOGGLE]                  = nk_rgba(190, 190, 190, 255);
		t[NK_COLOR_TOGGLE_HOVER]            = nk_rgba(170, 170, 170, 255);
		t[NK_COLOR_TOGGLE_CURSOR]           = nk_rgba(0, 110, 190, 255);
		t[NK_COLOR_SELECT]                  = nk_rgba(205, 205, 205, 255);
		t[NK_COLOR_SELECT_ACTIVE]           = nk_rgba(180, 200, 240, 255);
		t[NK_COLOR_SLIDER]                  = nk_rgba(200, 200, 200, 255);
		t[NK_COLOR_SLIDER_CURSOR]           = nk_rgba(0, 110, 190, 255);
		t[NK_COLOR_SLIDER_CURSOR_HOVER]     = nk_rgba(0, 130, 210, 255);
		t[NK_COLOR_SLIDER_CURSOR_ACTIVE]    = nk_rgba(0, 150, 230, 255);
		t[NK_COLOR_PROPERTY]                = nk_rgba(215, 215, 215, 255);
		t[NK_COLOR_EDIT]                    = nk_rgba(255, 255, 255, 255);
		t[NK_COLOR_EDIT_CURSOR]             = nk_rgba(0, 0, 0, 255);
		t[NK_COLOR_COMBO]                   = nk_rgba(215, 215, 215, 255);
		t[NK_COLOR_CHART]                   = nk_rgba(215, 215, 215, 255);
		t[NK_COLOR_CHART_COLOR]             = nk_rgba(0, 110, 190, 255);
		t[NK_COLOR_SCROLLBAR]               = nk_rgba(215, 215, 215, 255);
		t[NK_COLOR_SCROLLBAR_CURSOR]        = nk_rgba(150, 150, 150, 255);
		t[NK_COLOR_SCROLLBAR_CURSOR_HOVER]  = nk_rgba(130, 130, 130, 255);
		t[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(110, 110, 110, 255);
		t[NK_COLOR_TAB_HEADER]              = nk_rgba(210, 210, 210, 255);
	}
	nk_style_from_table(ctx, t);
	ctx->style.window.padding = nk_vec2(8, 6);
	ctx->style.window.spacing = nk_vec2(6, 6);
	ctx->style.button.rounding = relief ? 0 : 3;
	if (relief) {
		/* bevels are drawn by widgets.c: no flat borders, no rounding */
		ctx->style.button.border = 0;
		ctx->style.combo.rounding = 0; ctx->style.combo.border = 0;
		ctx->style.selectable.rounding = 0;
		ctx->style.checkbox.border = 0;
		ctx->style.slider.rounding = 0;
		ctx->style.window.group_border = 0;
		ctx->style.window.combo_border = 0;
		ctx->style.contextual_button.border = 0;
	} else {
		/* flat: a thin outline around buttons, combos and menu entries */
		struct nk_color line = dark ? nk_rgba(100, 100, 100, 255) : nk_rgba(150, 150, 150, 255);
		ctx->style.button.border = 1;            ctx->style.button.border_color = line;
		ctx->style.combo.border = 1;             ctx->style.combo.border_color = line;
		ctx->style.contextual_button.border = 0;   /* entries are separated by a line (widgets.c) */
		ctx->style.contextual_button.rounding = 0;
		ctx->style.window.combo_border = 1;      ctx->style.window.combo_border_color = line;
	}
}
