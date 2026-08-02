# scan de tous les ports ouvert TCP


```bash
sudo nmap -sC -sV -T4 -v -O <target> -Pn -p1-65535 -o <pwd>/scanPortTCP_<target>.nmap
```
