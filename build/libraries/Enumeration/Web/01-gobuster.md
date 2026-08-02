
Recherche des dossiers cachés
```bash
$ gobuster dir -w /usr/share/wordlists/dirbuster/directory-list-2.3-medium.txt -u http://192.168.56.27/
```

Recherche de fichiers cachés :
```
$ gobuster dir -w /usr/share/wordlists/dirbuster/directory-list-2.3-medium.txt -x htm,html,php,js,jpg,png -u http://192.168.56.101/ 
```

Options sympa en plus :

- -s "204,301,302,307,401,403" ==> Only show results with these status codes
    
- -b "302" ==> Exclude 302 status codes
    
- --exclude-length 3571 ==> Exclude server responses of length 3571
    
- -k ==> Disable HTTPS verification