Voici les commandes à executer dès le 1e accès sur une machine windows :

# USERNAME 
```
$ whoami

```

# USERNAME privilege 
```
$ whoami /priv

```

# Membre de quel groupe de l'utilisateur
```powershell
$ whoami /groups

#ou sous powershell :
PS> net user dave
```
# Les utilisateurs existant et leurs groupes
### Liste des utilisateur locaux
```powershell
PS > Get-LocalUser
```
### Listes des groupes locaux
```powershell
PS > Get-LocalGroup
```
### Lister les utilisateurs appartenant à un groupe local passé en paramètre
```powershell
PS> Get-LocalGroupMember adminteam
PS> Get-LocalGroupMember Administrators
```

```
PS > 
```
# l'OS, Version, architecture
```powershell
PS > systeminfo
```
# Information du réseau
### Lister les interfaces réseaux
```
ipconfig /all
```
### Lister la table de routage
```
route print
```
### Lister les connexions actives
```
netstat -ano

```
# Les applications installées
```powershell
Get-ItemProperty "HKLM:\SOFTWARE\Wow6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*" | select displayname

et la commande :
Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*" | select displayname
```
# Les processus lancés
``` powershell
Get-Process <nom_du_processus_recherché ou pas>
```
## Chercher le path des processus lancé :
```powershell
gwmi win32_process | select CommandLine
```
### Les fichiers utiles d'un utilisateur (ici dave)
```powershell
Get-ChildItem -Path C:\Users\dave\ -Include *.txt,*.pdf,*.xls,*.xlsx,*.doc,*.docx -File -Recurse -ErrorAction SilentlyContinue
```
