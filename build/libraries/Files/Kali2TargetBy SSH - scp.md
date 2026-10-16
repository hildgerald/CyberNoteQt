## Copie d'un fichier du dossier courant vers une cible : ##
```
scp compile_emis_2.sh ghild@192.168.200.141:/home/ghild/DEV/gitlab/EMIS_V0/image-generation-scripts/
```
ici on copie le fichier **compile_emis_2.sh** se trouvant dans notre dossier actuel ver la machine **192.168.200.141**, utilisateur **ghild**, dans le dossier de destination **/home/ghild/DEV/gitlab/EMIS_V0/image-generation-scripts/**. 

Autre exemple :
```
scp client_source.zip kali@192.168.45.162:/home/kali/OSCP-2024/Exercises/18.3.4
```
## Copie d'un dossier du dossier courant vers une cible : ##
```
scp -rp ./ntp ghild@192.168.200.141:/home/ghild/DEV/gitlab/EMIS_V0/image-generation-scripts/
```

ici on copie le dossier **/ntp** se trouvant dans notre dossier actuel ver la machine **192.168.200.141**, utilisateur **ghild**, dans le dossier de destination **/home/ghild/DEV/gitlab/EMIS_V0/image-generation-scripts/** et ceci de manière récursive