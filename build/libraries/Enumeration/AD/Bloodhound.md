Lancement de bloodhound
```
sudo bloodhound-python -u JOHN_DOE -p 'P455w0rd4567' -ns 192.168.200.18 - d CORP -c all --zip
```

Recherche des machine avec des OS plus supporté :
```
MATCH (H:Computer) WHERE H.operratingsystem =~ '.*(2000|2003|2008|xp|vista|7|me)*.' RETURN H
```
Recherche des utilisateurs qui pourraient être victime de Kerberoasting et qui pouraient avoir accès à un compte administrateur :
```
MATCH (u:User {hasspn:true}) MATCH (g:Group) WHERE g.name CONTAINS 'DOMAIN ADMINS' MATCH p = shortestpath( (u)-[*1..]->(g) ) return p
```