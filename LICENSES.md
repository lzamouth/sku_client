# Licences of the third-party components

Checked on 23/09/2026 against the versions pinned by `deps/fetch.sh`. All
are permissive and compatible with binary distribution, closed or not, with
static linking. **No GPL or LGPL component is linked** into our binaries.

| Component | Version | Licence | Obligation when distributing |
|---|---|---|---|
| SDL3 | 3.4.16 | zlib | none (do not claim authorship); courtesy mention |
| Nuklear (+ stb_truetype, stb_rect_pack) | 4.13.3 | MIT **or** public domain, at our choice → public domain | none |
| ProggyClean font (embedded by Nuklear) | — | MIT | copy of the notice |
| libcurl | 8.22.0 | curl (MIT/X style) | copy of the copyright and permission notice |
| mbedTLS (+ everest, p256-m) | 3.6.7 | Apache-2.0 **or** GPL-2.0+ → **Apache-2.0** | copy of the Apache 2.0 licence and the copyright |
| zlib | 1.3.1 | zlib | none; courtesy mention |
| DejaVu Sans / Sans Mono fonts (embedded in the GUI) | 2.37 | Bitstream Vera (permissive) + public domain | copy of the notice |
| Mozilla CA bundle (`cacert.pem`) | 13/08/2026 | MPL-2.0 | distributed as is with its notice; the MPL does not extend to our code |
| mingw-w64 runtime (Windows, static) | gcc 13 | libgcc: GPL + *GCC Runtime Library Exception*; winpthreads: MIT/BSD; CRT: public domain/ZPL | none (explicit exception for static linking) |
| macOS frameworks, Schannel/Win32 | system | Apple / Microsoft | none (system libraries) |
| X11, Wayland, libdecor, xkbcommon (Linux) | system | MIT | none: loaded with `dlopen` at run time, not linked |

Notes:
- SDL embeds `hidapi` (triple licence including GPL3) but we disable it (`SDL_HIDAPI=OFF`); it is not compiled.
- mbedTLS is used on Linux and macOS only (Windows: Schannel).
- The full text of the notices to ship is generated into `THIRD_PARTY_NOTICES.txt` by `deps/licenses.sh`; it is shown in the About tab and copied into every archive.

Licence of the client itself: for SkunkCrafts to decide (the source is in a private repository for now). None of the licences above imposes a choice.
