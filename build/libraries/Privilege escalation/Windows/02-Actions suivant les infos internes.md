# Recherche d'un type de fichier :
ici les bases de données de Keepass :
```powershell
PS> Get-ChildItem -Path C:\ -Include *.kdbx -File -Recurse -ErrorAction SilentlyContinue

# Pour plusieurs type de fichiers en même temps :

PS> Get-ChildItem -Path C:\xampp -Include *.txt,*.ini -File -Recurse -ErrorAction SilentlyContinue
```

### Changement d'utilisateur on passe de Steve à backupadmin :
```powershell
PS C:\Users\steve> runas /user:backupadmin cmd
```

