# Mysql

### Connexion avec l'utilisateur root, mot de passe root :
```SHELL
mysql -u root -p'root -h 192.168.167.16 -P 3306

si un problème de certificat TLS :
$ mysql -u root -p'root' -h 192.168.167.16 -P 3306 --ssl-verify-server-cert=FALSE

```

### Version
```MYSQL
select version();
```

### Lister les bases de données
```MYSQL
show databases;
```

### sélectionner une base
```MYSQL
use <nom_de_la_base>;
```

### Lister les tables de la base courante
```MYSQL
show tables;
```

### Lister les colonnes d'une table
```MYSQL
show columns from user;
```

### Sélection d'une colonne en fonction de la table et d'une info de
```mysql
select plugin from user where user = 'offsec';
```

# MSSQL

### Connexion
```SHELL
$ impacket-mssqlclient Administrator:Lab123@192.168.167.18 -windows-auth

```

### Version
```SQL
select @@version;

```

### Liste des bases de données
```SQL
select name from sys.databases;

```

### Liste des tables d'une base de données
```SQL
select @@version;

```
### Liste des utilisateurs système
```SQL
select * from sys.sysusers;

```
### Version
```SQL
select @@version;

```
### Version
```SQL
select @@version;

```