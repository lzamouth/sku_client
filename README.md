# SkunkCrafts Updater 4.0 (sku_client)

Réécriture en C du client SkunkCrafts Updater pour X-Plane. Analyse et décisions : [ANALYSE.md](ANALYSE.md).

## Arborescence

```
src/core/     bibliothèque C11 sans GUI : plateforme (POSIX/Win32), chemins et sécurité, cfg v1/v4,
              CRC32, log, strmap, http (curl), protocole (niveaux 1 et 2), cache CRC, pool de threads,
              instance X-Plane, scan, addon (check / update, listes distantes en GET conditionnel), packs,
              changelog/readme, rapport ; bundle CA embarqué
src/cli/      sku : client en ligne de commande, même cœur que la future GUI
src/app/      client graphique SDL3 + Nuklear : main (boucle, polices, instance), prefs, jobs (file de travaux),
              theme, ui_list / ui_detail / ui_pack / ui_options (options, about, choix d'instance)
tests/        tests unitaires + bout-en-bout (faux X-Plane, dépôt servi par python), exécutés dans le conteneur
spike/        preuve de la chaîne de build (conservée, option SKU_BUILD_SPIKE)
deps/         sources tierces figées et leurs builds (ignorés par git, voir deps/fetch.sh)
cmake/, docker/, macos/, tools/   toolchains, image de build Linux, bundle .app, scripts utilitaires
```

## Apparence

Police d'interface Roboto (Apache 2.0), DejaVu Sans Mono pour le texte à chasse fixe, DejaVu Sans fusionnée pour les deux symboles ▶ et ▫ que Roboto n'a pas. Deux rendus au choix dans Options : plat (liséré fin autour des boutons, onglets et menus, fond des menus distinct) ou relief (préférence `relief`, biseaux à deux tons dessinés par `widgets.c` par-dessus les widgets Nuklear, qui ne connaît ni dégradés ni ombres). Les largeurs fixes sont exprimées pour un texte de 16 px et suivent la taille de texte (`UI_W`), les colonnes de la liste et les lignes d'Options sont proportionnelles à la fenêtre. Dans la liste, la première colonne est un menu déroulant de canal, présent seulement quand l'addon en déclare un autre que le principal ; le jeton interne `stable` s'affiche « Main » (`UI_MAIN_CHANNEL`). La liste et celle des membres de pack ont une ligne de titres de colonnes dont les frontières se tirent à la souris (double-clic : proportions par défaut), proportions mémorisées (`list_cols`). `SKU_FONT_FILE=<ttf>` essaie une autre police sans recompiler, `SKU_RELIEF=1` force le relief.

Système de fenêtrage sous Linux : X11 d'abord (XWayland sous un bureau Wayland), Wayland si aucun serveur X n'est joignable, préférence `video` dans Options. Raison : sur GNOME Wayland le redimensionnement interactif bloque par à-coups à travers la voie Wayland de SDL (la trace des images lentes montre des trous de 40 à 70 ms sans événement alors que nos images coûtent 1 à 2 ms), et il est fluide par XWayland, qui apporte en prime la barre de titre du bureau au lieu de celle de libdecor. Contrepartie : sur un écran en échelle fractionnaire, XWayland rend flou ; choisir Wayland dans ce cas. `SDL_VIDEO_DRIVER` dans l'environnement prime sur la préférence. En mode verbose, toute image lente ou attente d'événement anormale pendant un redimensionnement est tracée (`slow frame:`), `SKU_NOVSYNC=1` coupe la synchronisation verticale pour diagnostiquer.

## Documentation

- [docs/USER_GUIDE.md](docs/USER_GUIDE.md) : guide utilisateur (anglais), livré dans chaque paquet.
- [docs/PUBLISHING.md](docs/PUBLISHING.md) : guide de publication pour les développeurs (dépôts, betas, packs).
- [docs/PUBLISH.md](docs/PUBLISH.md) : publier une version du client (interne).

## Packs

Guide de publication pour les développeurs dont le produit occupe plusieurs dossiers : [docs/PUBLISHING.md](docs/PUBLISHING.md) (anglais : dépôts, betas multiples, packs). Les clés de pack du cfg distant sont adoptées à la vérification, comme l'identité du dépôt.

## Données par instance

Dans chaque addon : `skunkcrafts_updater.cfg` reste **le** fichier de configuration, à sa place et en `clé|valeur` pur, parce que les avions y lisent leur version et que le 3.x et les outils de publication le lisent aussi ; le 4.0 y ajoute ses clés de canaux (`channel|`, `channel.<n>.module|`, `channel.<n>.zone|`) et absorbe les fichiers beta. `skunkcrafts_updater_ignore.txt` reste la liste d'exclusion de l'utilisateur. `skunkcrafts/crc_cache.txt` (cache CRC : `chemin|taille|mtime|crc32`, une ligne par fichier, invalidé par taille ou date) est le seul fichier du dossier privé `skunkcrafts/`, jamais publié ni supprimé par un dépôt. Les installations converties par les betas 1 à 9 (`skunkcrafts/updater.cfg`, `skunkcrafts/ignore.txt`) sont remigrées vers ces noms au premier chargement. `Output/skunkcrafts/updater/` dans le dossier X-Plane : `updater_log.txt` (rotation à 2 Mo), `cache/<dossier de l'addon>-<empreinte>.<liste>` et `.meta` (copies des listes distantes avec leurs validateurs HTTP), `cache/<…>.changelog.txt` / `.readme.txt`, `reports/<…>.txt` (dernier rapport), `custom_addons.txt`. L'empreinte est celle du chemin complet, deux addons de même nom ne se confondent donc pas.

## Mise à jour du client lui-même

Le client vérifie à chaque démarrage, au plus une fois par heure (préférence `client_check`, case dans Options) un dépôt au même protocole que les addons, `SKU_CLIENT_DEPOT_URL` à la compilation, `client_url` dans les préférences ensuite : si le cfg distant annonce un autre `module`, le client le suit, ce qui permet de déménager le dépôt sans recompiler. Le dépôt (`updater/depot/` sur le CDN du store, voir [docs/PUBLISH.md](docs/PUBLISH.md)) publie les livrables sous leur nom versionné, que le client déduit de la version annoncée (`SkunkcraftsUpdater-<v>-linux.zip`, …, `sku-cli-<v>.zip`), les noms fixes restant acceptés en repli ; `release.sh` les prépare dans `dist/<version>/depot/` avec le cfg et le changelog (`CHANGELOG.txt`, première ligne = version, comme un changelog d'avion). Quand une version plus récente existe, un bandeau au-dessus de toutes les vues propose « What's new » (changelog dans About, nouveautés en couleur), « Download » (le paquet de la plateforme et du mode d'installation, CRC vérifié, déposé dans `<config utilisateur>/downloads`) et « Later ». Palier 2, remplacement en place : fait pour l'AppImage (`$APPIMAGE` désigne le fichier), pour le binaire Linux posé et pour l'exe Windows posé (extraits du zip par le lecteur minimal `zip.c`, entrées stockées ou dégonflées via zlib, CRC vérifié, installés par-dessus l'exécutable courant) : copie à côté, `chmod`, `rename` par-dessus ; sous Windows un exécutable en cours ne peut pas être remplacé mais peut être renommé, donc `.exe` → `.exe.old` puis le nouveau prend le nom, et `.old` est supprimé au démarrage suivant. « Restart now » relance par `sku_spawn_detached` (`fork`/`execv`, ou `ShellExecuteEx` sous Windows). Windows installé : le setup est téléchargé et « Install now » le lance (élévation demandée par son manifeste) après avoir quitté ; sa page finale propose de relancer. macOS reste en téléchargement manuel. `SKU_NO_CLIENT_CHECK=1` désactive la vérification au démarrage (tests).

## CLI

```bash
sku instances                        # installations connues de X-Plane (x-plane_install_12.txt / _11.txt)
sku --xp DIR list                    # addons gérés
sku --xp DIR --dry-run check [nom]   # vérification sans rien écrire (ni cache, ni log, ni conversion), liste prévisionnelle
sku --xp DIR check --report [nom]    # vérification, cache CRC et rapport écrits dans Output/skunkcrafts/updater
sku --xp DIR update [nom]            # vérification puis mise à jour / réparation, liste effective des fichiers
sku --xp DIR report [nom]            # dernier rapport
sku --xp DIR changelog [nom]         # changelog (sections plus récentes que la version installée marquées ">")
sku --xp DIR readme [nom]            # readme
sku client [download [DIR]]          # une version plus récente du client est-elle publiée ? (download : la récupère)
# options : --full (ignore le cache CRC) --verbose (log de débogage sur stderr) --threads N --downloads N
#           --create-members (update : crée les membres de pack déclarés mais absents)
```

Sans `--xp`, l'instance est celle du dossier courant ou de l'exécutable. `nom` est une sous-chaîne du nom ou du dossier de l'addon, ou du nom/identifiant d'un pack (tous ses membres sont alors traités). `list` regroupe les membres sous leur pack et signale les membres déclarés absents.

## Client graphique

```bash
./docker/run.sh ./build.sh linux -DSKU_BUILD_APP=ON     # build/linux/src/app/SkunkcraftsUpdater_lin
./build.sh windows -DSKU_BUILD_APP=ON                   # build/windows/src/app/SkunkcraftsUpdater.exe
./build.sh macos   -DSKU_BUILD_APP=ON                   # build/macos/src/app/SkunkcraftsUpdater (universel)
```

`SkunkcraftsUpdater_lin [--xp DIR]` : sans `--xp`, l'instance vient du dossier courant/exécutable, puis de la dernière utilisée, puis de `x-plane_install_12.txt` ; sinon le sélecteur s'affiche. Préférences dans le dossier de config utilisateur (`prefs.cfg`), migration automatique de `skunkcrafts_updater_go.json`.

Aides de test : `--screenshot DIR` (une capture BMP par seconde) et `SKU_AUTOPILOT=fichier` (lignes `<n> click <x> <y>` en pixels de capture, injectées dans Nuklear sur trois images : survol, appui, relâchement, parce que Nuklear n'active une fenêtre que survolée bouton relâché). `SKU_NO_CLIENT_CHECK=1` évite la vérification réseau du client au démarrage. Voir `tests/` pour l'usage avec le faux X-Plane de `make_fixture.py`.

## Release

Publication d'une version empaquetée sur le dépôt du client et le CDN du store : `docs/publish.sh <version>`, décrit dans [docs/PUBLISH.md](docs/PUBLISH.md).

```bash
./release.sh 4.0.0            # ou 4.0.0-beta1 : version estampillée dans les trois binaires
```

Produit dans `dist/<version>/`, un téléchargement par plateforme et mode d'installation : `SkunkcraftsUpdater-<v>-windows.zip` et `-linux.zip` (le programme seul, à poser dans X-Plane ou ailleurs), `SkunkcraftsUpdater-<v>-windows-setup.exe` (NSIS, `makensis` requis), `SkunkcraftsUpdater-<v>-macos.zip` (`.app` universel, arm64 signé ad hoc — jamais de `strip` après l'édition de liens, les symboles sont retirés par le linker), `SkunkcraftsUpdater-<v>-x86_64.AppImage` (runtime statique officiel + squashfs gzip fabriqué dans le conteneur, pas d'`appimagetool` ; gzip parce que libappimage, donc l'intégration AppImageLauncher, ne lit pas zstd), `sku-cli-<v>.zip`.

Icônes : `packaging/icons/icon.png` → `.ico` (ImageMagick) et `.icns` (`tools/make_icns.py`). Ressource Windows (icône, infos de version) : `packaging/windows/app.rc.in`.

Piège de test : cette machine a **AppImageLauncher**, qui intercepte toute exécution d'AppImage via `binfmt_misc` (y compris dans les conteneurs Docker, où l'exécution échoue alors avec « No such file or directory »). Pour tester une AppImage sans lui, mettre à zéro les 3 octets magiques à l'offset 8 d'une copie (`printf '\x00\x00\x00' | dd of=copie bs=1 seek=8 conv=notrunc`), puis l'exécuter normalement ou avec `APPIMAGE_EXTRACT_AND_RUN=1`.

Incompatibilité connue : **AppImageLauncher 2.2.0** (dernière version stable, 2020, celle des dépôts et de cette machine) ne sait pas lancer les AppImages construites avec le runtime statique moderne, le nôtre comme celui d'`appimagetool` (« fuse: memory allocation failed », ticket amont AppImage/type2-runtime#121). L'intégration dans le menu marche, le lancement non. Corrigé seulement dans AppImageLauncher 3.0 alpha. Revenir à l'ancien runtime dynamique n'est pas une option : il exige libfuse2, absent d'Ubuntu 22.04 et suivants. Le zip avec `SkunkcraftsUpdater_lin` reste la solution universelle.

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
