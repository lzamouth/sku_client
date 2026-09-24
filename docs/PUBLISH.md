# Publier une version du client

Document interne (SkunkCrafts). Le guide destiné aux développeurs tiers est
[PUBLISHING.md](PUBLISHING.md).

## Les deux canaux

1. **Le dépôt du client**, `sku_client` dans la zone « SkunkCrafts - Stable »
   (`/hdd1/depots/skunkcrafts`, poussée sur Bunny par `xp_depot`, servie par
   `https://cdn.skunkcrafts.com/updater/skunkcrafts/sku_client`). C'est lui
   que les clients installés interrogent au démarrage pour proposer une
   version plus récente. Il contient les livrables sous des noms fixes, sans
   numéro de version, plus le cfg, le changelog et le readme.
2. **Le CDN du store** pour les téléchargements en accès libre : le bucket S3
   monté en `/remote/s3/aerobask`, servi par
   `https://1228098873.rsc.cdn77.org/`. Un fichier par plateforme et mode
   d'installation, avec le numéro de version dans le nom, et le fichier
   `urls to updater v4.txt` qui liste les cinq URL courantes pour la page de
   téléchargement.

## La séquence

```bash
./release.sh 4.0.0-beta7          # compile les trois cibles, produit dist/4.0.0-beta7/
docs/publish.sh 4.0.0-beta7       # publie sur les deux canaux et vérifie
```

`release.sh` exige que `CHANGELOG.txt` commence par la ligne de la version,
comme un changelog d'avion : ajouter la section avant de lancer.

`publish.sh` refuse de partir si le dossier `dist/<version>/` est incomplet,
si le cfg du dépôt ne porte pas la version demandée, ou si le bucket n'est
pas monté. Puis, dans l'ordre :

1. remplace `/hdd1/depots/skunkcrafts/sku_client` par `dist/<version>/depot`
   et lance `xp_depot /hdd1/depots/skunkcrafts sku_client update`, qui
   verrouille, envoie, génère les listes, envoie le cfg en dernier et purge
   le cache Bunny ;
2. copie les cinq téléchargements dans le bucket ;
3. réécrit les numéros de version dans `urls to updater v4.txt`, libellés
   conservés ;
4. vérifie que les cinq URL répondent 200 et que `sku client` voit la
   version publiée.

`docs/publish.sh <version> --dry-run` affiche les actions sans rien
envoyer.

## Ce que le script ne fait pas

- Il ne supprime rien dans le bucket : les fichiers des versions précédentes
  y restent tant qu'on ne fait pas le ménage à la main.
- Il ne pousse pas le dépôt git : le code reste local.
- Il ne connaît aucun secret : `xp_depot` lit les siens dans `xp_zone.toml`,
  le bucket est monté par s3fs avec ses propres identifiants.

## Après publication

Un client 4.0 antérieur, lancé dans l'heure, affiche le bandeau « version
disponible » avec le changelog et le téléchargement du paquet de sa
plateforme. Le réglage du dépôt interrogé est `client_url` dans les
préférences ; si le dépôt déménage, publier à l'ancienne adresse un cfg
dont `module` pointe sur la nouvelle suffit, les clients suivent.
