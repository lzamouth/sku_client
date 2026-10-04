# Publier une version du client

Document interne (SkunkCrafts). Le guide destiné aux développeurs tiers est
[PUBLISHING.md](PUBLISHING.md), le guide utilisateur [USER_GUIDE.md](USER_GUIDE.md).

## Où

Tout est sur le CDN du store, le bucket S3 monté en `/remote/s3/aerobask`
et servi par `https://1228098873.rsc.cdn77.org/`, dans son dossier `updater/` :

1. **Le dépôt du client**, sous-dossier `updater/depot/`, au protocole des
   addons : `skunkcrafts_updater.cfg`, whitelist, sizeslist, blacklist vide,
   `changelog.txt`, `readme.txt`, le guide, et les paquets sous leur **nom
   versionné** (`SkunkcraftsUpdater-4.0.0-beta8-linux.zip`, `sku-cli-4.0.0-beta8.zip`).
   Le client déduit le nom du paquet de la version publiée ; ainsi une liste
   servie périmée par le CDN ne peut jamais désigner un fichier d'une autre
   version. Les noms fixes restent acceptés en repli pour les anciens dépôts.
2. **`updater/`** : les cinq téléchargements sous nom fixe,
   `SkunkcraftsUpdater-windows.zip`, `-windows-setup.exe`, `-macos.zip`,
   `-linux.zip`, `-x86_64.AppImage`, plus `sku-cli.zip`, `USER_GUIDE.html` et
   `USER_GUIDE.md`. Ce sont les liens permanents de la page de téléchargement ;
   ils changent de contenu à chaque version, pas de nom. La liste des liens
   pour la page du store, `updater/urls to updater v4.txt`, n'a donc plus à
   être modifiée à chaque version ; le script ne la touche pas.

Deux anciennes adresses ne servent plus qu'à rediriger, par un cfg dont
`module` annonce `https://1228098873.rsc.cdn77.org/updater/depot` : le dépôt
Bunny `cdn.skunkcrafts.com/updater/skunkcrafts/sku_client` (clients jusqu'à
la beta7) et `1228098873.rsc.cdn77.org/sku_client` (beta8). Les clients
suivent ; ces adresses peuvent disparaître quand plus personne ne les
interroge. Le dépôt garde aussi, pour ces clients qui ne connaissent que les
noms fixes, une copie à nom fixe de chaque paquet, à retirer plus tard.

## La séquence

```bash
./release.sh 4.0.0-beta8          # compile les trois cibles, produit dist/4.0.0-beta8/
docs/publish.sh 4.0.0-beta8       # publie sur le bucket et vérifie
```

`release.sh` exige que `CHANGELOG.txt` commence par la ligne de la version,
comme un changelog d'avion, et produit `dist/<version>/depot/` prêt à
publier, y compris `USER_GUIDE.html` généré par `tools/md2html.py`.

`publish.sh` refuse de partir si `dist/<version>/` est incomplet, si le cfg
du dépôt ne porte pas la version ou pas l'URL du dépôt S3, ou si le bucket
n'est pas monté. Puis :

1. génère whitelist et sizeslist du dépôt, écrit le cfg **verrouillé** dans
   `updater/depot/`, copie les fichiers, écrit le cfg **déverrouillé** ;
2. copie les téléchargements sous nom fixe et le guide à la racine ;
3. vérifie que les URL fixes et le cfg du dépôt répondent 200 et que
   `sku client` voit la version publiée.

`docs/publish.sh <version> --dry-run` affiche les actions sans rien envoyer.

## Ce que le script ne fait pas

- Il ne supprime rien dans le bucket : les paquets versionnés des versions
  précédentes restent dans `updater/depot/`, à purger à la main de temps en
  temps (rien ne les référence plus une fois le cfg passé à la version
  suivante).
- Il ne purge pas le cache CDN77 : les fichiers à nom fixe et les listes
  peuvent être servis périmés pendant la durée du cache. Les noms versionnés
  du dépôt rendent cela inoffensif pour les clients ; pour les liens fixes,
  un utilisateur peut recevoir la version précédente pendant ce délai.
- Il ne pousse pas le dépôt git : le push vers le miroir GitHub privé
  (`lzamouth/sku_client`) se fait à part, à la main.
- Il ne connaît aucun secret : le bucket est monté par s3fs avec ses propres
  identifiants.

## Après publication

Un client 4.0 antérieur, lancé dans l'heure, affiche le bandeau « version
disponible » avec le changelog et « Update » : remplacement en place puis
« Restart now » (AppImage, binaire Linux, exe Windows posé, bundle macOS),
ou téléchargement du setup et « Install now » (Windows installé). Les
clients antérieurs à la beta9 (AppImage), beta12 (Linux posé), beta14
(Windows) et beta15 (macOS) ne savent que télécharger : une dernière
installation à la main pour eux. Le réglage du dépôt interrogé est
`client_url` dans les préférences ; si le dépôt déménage encore, publier à
l'ancienne adresse un cfg dont `module` pointe sur la nouvelle suffit, les
clients suivent.
