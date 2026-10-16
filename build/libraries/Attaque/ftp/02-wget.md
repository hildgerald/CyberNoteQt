Avec wget, il est possible de télécharger tous les fichiers d'un serveur ftp :
Autre commande permettant de faire un miroir du serveur ftp :
```
$ wget --no-passive-ftp --mirror 'ftp://anonymous:anonymous@192.168.56.15'

```
tout sera téléchargé dans le dossier 192.168.56.15 directement sur la machine d'attaque !