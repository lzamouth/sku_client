/* Colors: the dark theme of the 3.x client, a light counterpart, and the
 * status colors (same meaning as 3.x: green = ok, cyan = update, ...). */
#include "app.h"
#include "nuklear_config.h"
#include <string.h>

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

void theme_apply(struct nk_context *ctx, bool dark)
{
	struct nk_color t[NK_COLOR_COUNT];
	memcpy(t, app_default_colors(), sizeof t);
	if (dark) {
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
		t[NK_COLOR_SELECT]                  = nk_rgba(40, 40, 40, 255);
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
		t[NK_COLOR_SELECT]                  = nk_rgba(225, 225, 225, 255);
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
	ctx->style.button.rounding = 3;
}
