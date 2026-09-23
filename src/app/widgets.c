/* Small drawing helpers shared by the views. */
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
