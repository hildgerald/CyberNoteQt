# Recherche de dossier comme Gobuster
```bash
ffuf -w /usr/share/seclists/Discovery/Web-Content/raft-small-directories-lowercase.txt -u http://192.168.130.66/FUZZ -ac

```

```bash
ffuf -w /usr/share/seclists/Discovery/Web-Content/raft-medium-files.txt -u http://192.168.130.66/FUZZ -ac

```

# Brute force d'une page login avec le proxy Burp pour voir
```bash
wfuzz -c -z file,/usr/share/seclist/fuzzin/SQLi/quick-SQLi.txt -d="username=FUZZ&password=attempt" -u http://192.168.130.66/login.php -p "127.0.0.1.8080.http"

```



