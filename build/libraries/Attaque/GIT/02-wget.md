On peut récupérer un dépot GIT par :
```
wget --recursive --no-parent --no-host-directories --reject "index.html*" -e robots=off http://192.168.237.144/.git/

```

Puis on corrige le dépot des erreurs :
```
git fsck   
```

On doit pouvoir lire les logs :
```
git log

```

On  compare les deux derniers commits :
```
git log -p -2

```