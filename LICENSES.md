# Licences des composants tiers

Vérifié le 23/09/2026 sur les versions figées par `deps/fetch.sh`. Toutes sont
permissives et compatibles avec une distribution binaire, fermée ou non, en
liaison statique. **Aucun composant GPL ou LGPL n'est lié** dans nos binaires.

| Composant | Version | Licence | Obligation à la distribution |
|---|---|---|---|
| SDL3 | 3.4.16 | zlib | aucune (ne pas prétendre en être l'auteur) ; mention de courtoisie |
| Nuklear (+ stb_truetype, stb_rect_pack) | 4.13.3 | MIT **ou** domaine public, au choix → domaine public | aucune |
| Police ProggyClean (embarquée par Nuklear) | — | MIT | copie de la notice |
| libcurl | 8.22.0 | curl (type MIT/X) | copie du copyright et de la notice de permission |
| mbedTLS (+ everest, p256-m) | 3.6.7 | Apache-2.0 **ou** GPL-2.0+ → **Apache-2.0** | copie de la licence Apache 2.0 et du copyright |
| zlib | 1.3.1 | zlib | aucune ; mention de courtoisie |
| Polices DejaVu Sans / Sans Mono (embarquées dans la GUI) | 2.37 | Bitstream Vera (permissive) + domaine public | copie de la notice |
| Bundle CA Mozilla (`cacert.pem`) | 13/08/2026 | MPL-2.0 | distribué tel quel avec sa notice ; MPL ne s'étend pas à notre code |
| Runtime mingw-w64 (Windows, statique) | gcc 13 | libgcc : GPL + *GCC Runtime Library Exception* ; winpthreads : MIT/BSD ; CRT : domaine public/ZPL | aucune (exception explicite pour la liaison statique) |
| Frameworks macOS, Schannel/Win32 | système | Apple / Microsoft | aucune (bibliothèques du système) |
| X11, Wayland, libdecor, xkbcommon (Linux) | système | MIT | aucune : chargés par `dlopen` à l'exécution, pas liés |

Remarques :
- SDL embarque `hidapi` (triple licence dont GPL3) mais nous le désactivons (`SDL_HIDAPI=OFF`) ; il n'est pas compilé.
- mbedTLS n'est utilisé que sur Linux et macOS (Windows : Schannel).
- Le texte complet des notices à livrer est généré dans `THIRD_PARTY_NOTICES.txt` par `deps/licenses.sh` ; il sera affiché dans l'onglet « About » et copié dans chaque archive.

Licence du client lui-même : à décider par SkunkCrafts (le code ne sort pas pour l'instant). Aucune des licences ci-dessus n'impose de choix.
