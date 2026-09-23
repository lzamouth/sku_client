#!/bin/bash
# Assemble THIRD_PARTY_NOTICES.txt from the license files of the pinned sources.
set -e
cd "$(dirname "$0")/.."
OUT=THIRD_PARTY_NOTICES.txt
sec() { printf '\n==============================================================================\n%s\n==============================================================================\n\n' "$1" >> "$OUT"; }

: > "$OUT"
echo "SkunkCrafts Updater embeds the following third-party components." >> "$OUT"

sec "SDL3 - zlib license"
cat deps/src/sdl/LICENSE.txt >> "$OUT"

sec "Nuklear - public domain (Unlicense), incl. stb_truetype / stb_rect_pack"
awk '/ALTERNATIVE B - Public Domain/{p=1} p && /^(\/\/\/ *)?-{20}/{exit} p{sub(/^\/\/\/ ?/,""); print}' deps/src/nuklear/nuklear.h >> "$OUT"

sec "ProggyClean font (embedded through Nuklear) - MIT"
cat <<'EOF' >> "$OUT"
Copyright (c) 2004, 2005 Tristan Grimmer
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions: The above copyright notice and this
permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
EOF

sec "libcurl - curl license"
cat deps/src/curl/COPYING >> "$OUT"

sec "Mbed TLS (incl. everest, p256-m) - used under the Apache License 2.0"
cat deps/src/mbedtls/LICENSE >> "$OUT"

sec "zlib - zlib license"
sed -n '/Copyright notice:/,$p' deps/src/zlib/LICENSE >> "$OUT"

sec "DejaVu fonts (DejaVu Sans, DejaVu Sans Mono) - Bitstream Vera license / public domain"
cat deps/src/fonts/LICENSE >> "$OUT"

sec "Mozilla CA certificate bundle (cacert.pem) - MPL 2.0"
cat <<'EOF' >> "$OUT"
The CA certificate bundle is extracted from Mozilla's root store by the curl
project (https://curl.se/docs/caextract.html) and is distributed unmodified
under the Mozilla Public License 2.0 (https://mozilla.org/MPL/2.0/).
EOF

echo "written $OUT ($(wc -l < "$OUT") lines)"
