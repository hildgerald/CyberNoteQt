```
where ssh
```

```
dir /s /b ssh
```

### Les fichiers utiles d'un utilisateur (ici dave)
```powershell
Get-ChildItem -Path C:\Users\ -Include *.txt,*.pdf,*.xls,*.xlsx,*.doc,*.docx -File -Recurse -ErrorAction SilentlyContinue

```

recherche à l'intérieur d'un fichier
```
findstr /SIM /C:"pass" *.ini *.cfg *.xml
```
