# Publier une version du client

Document interne (SkunkCrafts). Le guide destiné aux développeurs tiers est
[PUBLISHING.md](PUBLISHING.md), le guide utilisateur [USER_GUIDE.md](USER_GUIDE.md).

## Où

Tout est sur le CDN du store, le bucket S3 monté en `/remote/s3/aerobask`
et servi par `https://1228098873.rsc.cdn77.org/` :

1. **Le dépôt du client**, sous-dossier `sku_client/`, au protocole des
   addons : `skunkcrafts_updater.cfg`, whitelist, sizeslist, blacklist vide,
   `changelog.txt`, `readme.txt`, le guide, et les paquets sous leur **nom
   versionné** (`SkunkcraftsUpdater-4.0.0-beta8-linux.zip`, `sku-cli-4.0.0-beta8.zip`).
   Le client déduit le nom du paquet de la version publiée ; ainsi une liste
   servie périmée par le CDN ne peut jamais désigner un fichier d'une autre
   version. Les noms fixes restent acceptés en repli pour les anciens dépôts.
2. **La racine du bucket** : les cinq téléchargements sous nom fixe,
   `SkunkcraftsUpdater-windows.zip`, `-windows-setup.exe`, `-macos.zip`,
   `-linux.zip`, `-x86_64.AppImage`, plus `sku-cli.zip`, `USER_GUIDE.html` et
   `USER_GUIDE.md`. Ce sont les liens permanents de la page de téléchargement ;
   ils changent de contenu à chaque version, pas de nom.

L'ancien dépôt Bunny (`cdn.skunkcrafts.com/updater/skunkcrafts/sku_client`)
ne sert plus qu'à rediriger les clients antérieurs à la beta8 : son cfg
annonce `module|https://1228098873.rsc.cdn77.org/sku_client`, et ces clients
suivent. Il peut être supprimé quand plus personne ne l'interroge.

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
   `sku_client/`, copie les fichiers, écrit le cfg **déverrouillé** ;
2. copie les téléchargements sous nom fixe et le guide à la racine ;
3. vérifie que les URL fixes et le cfg du dépôt répondent 200 et que
   `sku client` voit la version publiée.

`docs/publish.sh <version> --dry-run` affiche les actions sans rien envoyer.

## Ce que le script ne fait pas

- Il ne supprime rien dans le bucket : les paquets versionnés des versions
  précédentes restent dans `sku_client/`, à purger à la main de temps en
  temps (rien ne les référence plus une fois le cfg passé à la version
  suivante).
- Il ne purge pas le cache CDN77 : les fichiers à nom fixe et les listes
  peuvent être servis périmés pendant la durée du cache. Les noms versionnés
  du dépôt rendent cela inoffensif pour les clients ; pour les liens fixes,
  un utilisateur peut recevoir la version précédente pendant ce délai.
- Il ne pousse pas le dépôt git : le code reste local.
- Il ne connaît aucun secret : le bucket est monté par s3fs avec ses propres
  identifiants.

## Après publication

Un client 4.0 antérieur, lancé dans l'heure, affiche le bandeau « version
disponible » avec le changelog et le téléchargement du paquet de sa
plateforme. Le réglage du dépôt interrogé est `client_url` dans les
préférences ; si le dépôt déménage encore, publier à l'ancienne adresse un
cfg dont `module` pointe sur la nouvelle suffit, les clients suivent.
