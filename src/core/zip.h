/* Minimal zip reader: extracts one entry (stored or deflated) from a zip file.
 * Enough for the client's own update packages; no zip64, no encryption. */
#ifndef SKU_ZIP_H
#define SKU_ZIP_H

#include "sku.h"

/* Writes the entry named `entry` (exact path inside the zip) to `dest`, verifying
 * its CRC. SKU_ERR_NOTFOUND when the entry is absent, SKU_ERR_FORMAT for an
 * unsupported or corrupt zip. err (may be NULL) gets a human readable reason. */
int sku_zip_extract(const char *zip_path, const char *entry, const char *dest, char *err, size_t en);

#endif
