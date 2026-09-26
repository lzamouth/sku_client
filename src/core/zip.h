/* Minimal zip reader: extracts one entry (stored or deflated) from a zip file.
 * Enough for the client's own update packages; no zip64, no encryption. */
#ifndef SKU_ZIP_H
#define SKU_ZIP_H

#include "sku.h"

/* Writes the entry named `entry` (exact path inside the zip) to `dest`, verifying
 * its CRC. SKU_ERR_NOTFOUND when the entry is absent, SKU_ERR_FORMAT for an
 * unsupported or corrupt zip. err (may be NULL) gets a human readable reason. */
int sku_zip_extract(const char *zip_path, const char *entry, const char *dest, char *err, size_t en);

/* Extracts every entry under dest_dir (created), keeping directories and the executable bit
 * (Unix mode of the entry, or any file under a "MacOS/" folder). Entries escaping dest_dir are refused. */
int sku_zip_extract_all(const char *zip_path, const char *dest_dir, char *err, size_t en);

#endif
