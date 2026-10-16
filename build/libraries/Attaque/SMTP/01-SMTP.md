# Liste des commandes possibles :
```
nmap -p 25 --script smtp-commands 192.168.180.189

```
# Enumeration d'utilisateur
```
smtp-user-enum -M RCPT -U /usr/share/seclists/Usernames/xato-net-10-million-usernames.txt -D relia.com -t 192.168.180.189

```

```
nmap --script smtp-enum-users.nse --script-args smtp-enum-users.methods=VRFY -p25 192.168.180.189

```