# SkunkCrafts Updater 4.0 (sku_client)

Réécriture en C du client SkunkCrafts Updater pour X-Plane. Analyse et décisions : [ANALYSE.md](ANALYSE.md).

## Arborescence

```
src/core/     bibliothèque C11 sans GUI : plateforme (POSIX/Win32), chemins et sécurité, cfg v1/v4,
              CRC32, log, strmap, http (curl), protocole (niveaux 1 et 2), cache CRC, pool de threads,
              instance X-Plane, scan, addon (check / update), rapport ; bundle CA embarqué
src/cli/      sku : client en ligne de commande, même cœur que la future GUI
src/app/      client graphique SDL3 + Nuklear : main (boucle, polices, instance), prefs, jobs (file de travaux),
              theme, ui_list / ui_detail / ui_options (options, about, choix d'instance)
tests/        tests unitaires + bout-en-bout (faux X-Plane, dépôt servi par python), exécutés dans le conteneur
spike/        preuve de la chaîne de build (conservée, option SKU_BUILD_SPIKE)
deps/         sources tierces figées et leurs builds (ignorés par git, voir deps/fetch.sh)
cmake/, docker/, macos/, tools/   toolchains, image de build Linux, bundle .app, scripts utilitaires
```

## CLI

```bash
sku instances                        # installations connues de X-Plane (x-plane_install_12.txt / _11.txt)
sku --xp DIR list                    # addons gérés
sku --xp DIR --dry-run check [nom]   # vérification sans rien écrire (ni cache, ni log, ni conversion)
sku --xp DIR check --report [nom]    # vérification, cache CRC et rapport écrits dans Output/skunkcrafts/updater
sku --xp DIR update [nom]            # vérification puis mise à jour / réparation
sku --xp DIR report [nom]            # dernier rapport
# options : --full (ignore le cache CRC) --verbose --threads N --downloads N
```

Sans `--xp`, l'instance est celle du dossier courant ou de l'exécutable. `nom` est une sous-chaîne du nom ou du dossier de l'addon.

## Client graphique

```bash
./docker/run.sh ./build.sh linux -DSKU_BUILD_APP=ON     # build/linux/src/app/SkunkcraftsUpdater_lin
./build.sh windows -DSKU_BUILD_APP=ON                   # build/windows/src/app/SkunkcraftsUpdater.exe
./build.sh macos   -DSKU_BUILD_APP=ON                   # build/macos/src/app/SkunkcraftsUpdater (universel)
```

`SkunkcraftsUpdater_lin [--xp DIR]` : sans `--xp`, l'instance vient du dossier courant/exécutable, puis de la dernière utilisée, puis de `x-plane_install_12.txt` ; sinon le sélecteur s'affiche. Préférences dans le dossier de config utilisateur (`prefs.cfg`), migration automatique de `skunkcrafts_updater_go.json`.

Aides de test : `--screenshot DIR` (une capture BMP par seconde) et `SKU_AUTOPILOT=fichier` (lignes `<n> click <x> <y>` en pixels de capture, injectées dans Nuklear). Voir `tests/` pour l'usage avec le faux X-Plane de `make_fixture.py`.

## Tests

```bash
./docker/run.sh bash -c './build.sh linux && cd build/linux && ctest --output-on-failure'
```

## Chaîne de build

Tout se compile depuis la machine Linux de Lionel, pour les trois cibles :

| Cible | Compilateur | Où | Sortie |
|---|---|---|---|
| Linux x86_64 | gcc d'Ubuntu 22.04 (glibc 2.35) | conteneur Docker `sku-build-linux` | binaire ne dépendant que de libc/libm |
| Windows x86_64 | mingw-w64 (`-posix`), lien statique | hôte | exe n'important que des DLL système |
| macOS universel | osxcross (o64-clang, SDK 11.3), `-arch x86_64 -arch arm64` | hôte | binaire fat, minos 10.15 (x86_64) / 11.0 (arm64), signé ad hoc |

Bibliothèques (sources figées dans `deps/`, toutes en statique) : SDL3 (fenêtre, entrées, rendu, dialogues), Nuklear (UI), libcurl (HTTP/HTTPS), mbedTLS (TLS sur Linux et macOS ; Schannel sur Windows), zlib (compression de transport).

```bash
./deps/fetch.sh                        # télécharge et dépaquette les sources (une fois)
./deps/build.sh windows                # bibliothèques pour Windows
./deps/build.sh macos                  # bibliothèques pour macOS (universel)
./docker/run.sh deps/build.sh linux    # bibliothèques pour Linux, dans le conteneur

./build.sh windows [projet]            # projet = spike (défaut) ou, plus tard, l'application
./build.sh macos   [projet]
./docker/run.sh ./build.sh linux [projet]

./check.sh linux   build/linux/spike/sku_spike       # glibc max, libs dynamiques
./check.sh windows build/windows/spike/sku_spike.exe # DLL importées
./check.sh macos   build/macos/spike/sku_spike       # archs, minos, dylibs
```

`docker/run.sh` construit l'image la première fois (Ubuntu 22.04 + paquets de développement X11/Wayland) et lance la commande avec l'UID de l'appelant.

## Particularités

- **compiler-rt absent d'osxcross** : les tests `@available` d'Objective-C dans SDL référencent `___isPlatformVersionAtLeast`. `spike/apple_compat.c` fournit cette fonction (version lue par `sysctl kern.osproductversion`) ; à lier dans tout build macOS.
- **Schannel** : ne jamais lui passer de bundle CA (`CURLOPT_CAINFO*`), il utilise le magasin Windows. Sur Linux/macOS, mbedTLS reçoit le bundle Mozilla (`deps/dl/cacert.pem`, à embarquer dans l'application).
- **zlib sur macOS** : CMake préfère `libz.tbd` du SDK (bibliothèque système présente sur tout macOS) à notre `libz.a` ; sans conséquence.
- Windows : l'exe du spike est en sous-système console pour voir la sortie de `--cli`.

## Spike (validation de la chaîne, 23/09/2026)

`spike/` : fenêtre SDL3 + Nuklear (thème sombre/clair), GET HTTPS dans un thread, mode `--cli [url]`. Résultats :

- Linux : 5,4 Mo, GLIBC_2.34 max, NEEDED = libc, libm. GET HTTPS OK (mbedTLS) sur l'hôte et dans le conteneur 22.04. Fenêtre OK sous Wayland.
- Windows : 6,0 Mo, DLL système uniquement. GET HTTPS OK (Schannel) sous wine.
- macOS : 8,2 Mo universel, minos 10.15/11.0, frameworks système + libSystem/libz/libobjc, tranche arm64 signée ad hoc par ld64. À tester sur Mac.
