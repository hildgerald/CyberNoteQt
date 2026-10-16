# Lien de téléchargement

# Chargement de powerview
```
Import-Module .\PowerView.ps1
```

# Liste des infos du domaine
```
Get-NetDomain
```

# Liste des utilisateurs du domaine
```
Get-NetUser
```

# Liste des utilisateurs du domaine avec des filtres
Filtre sur le CN, la date de dernière modification du Mot de passe ainsi que la dernière connexion
```
Get-NetUser | select cn,pwdlastset,lastlogon
```

# Liste des menbres d'un groupe particulier
```
Get-NetGroup "Sales department" | select member
```

# Liste des infos sur les machines
```
Get-NetComputer
```

# Liste des infos sur les machines avec filtre
Filtre sur l'OS et sur le dns hostname
```
Get-NetComputer | select operatingsystem,dnshostname
```

# Verification si l'utilisateur actuel est admin sur une machine du domaine
```
Find-LocalAdminAccess
```

# Voir les utilisateurs connecté sur une machine du domaine
```
Get-NetSession -ComputerName client74 -Verbose
```
Bien mettre l'option verbose afin de voir les erreurs et si on a le droit d'executer la commande

# Voir les permissions requise pour enumerer une session (commande précédente)
```
Get-Acl -Path HKLM:SYSTEM\CurrentControlSet\Services\LanManServer\DefaultSecurity\ | fl
```
Si ds la liste "Access", on a les groupes :
    - BuiltIn
    - NTAuthority
    - Creator Owner
    - Application Package Authority
    
On ne pourra pas enumerer un ordinateur distant

# 
```
```

# 
```
```

# 
```
```

# 
```
```
