
Connexion en RDP avec une cible :
```bash
xfreerdp /u:student /p:lab /v:192.168.130.152

```
Avec toutes les options 
```
xfreerdp /v:IP /u:USERNAME /p:PASSWORD +clipboard /dynamic-resolution /drive:/usr/share/windows-resources,share

ex : 
$ xfreerdp /u:jason /p:lab /v:192.168.207.203 /drive:.,share


```


**Options pour xfreerdp**

/dynamic-resolution — permet d’ajuster la taille de la fenêtre, ajustant la résolution de la cible.  
/size:WIDTHxHEIGHT — règle une taille spécifique dans le cas où la celle-ci n’est pas ajustée automatiquement avec l’option /dynamic-resolution  
+clipboard — active le support pour le clipboard  
/drive:LOCAL_DIRECTORY,SHARE_NAME — créer un dossier partagé. Pour partager le dossier courant en l’appelant “share” : /drive:.,share, le point se reférant au répertoire actuel (.). Possible d’écrire et de lire