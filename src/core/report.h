/* Plain-text report of the last check/update of an addon, for the UI and support. */
#ifndef SKU_REPORT_H
#define SKU_REPORT_H

#include "addon.h"

/* Renders the current plan/results; the buffer is malloc'd. */
char *sku_report_render(sku_addon *a, const char *title);

/* Renders and writes to <data_dir>/reports/<addon>.txt. */
int   sku_report_save(sku_addon *a, const char *title);

#endif
