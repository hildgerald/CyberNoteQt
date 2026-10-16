# SCAN ARP :
```
sudo nmap -PR -sn 192.168.200.0/23
```

# Scan ICMP :
```
sudo nmap -sn -PP 192.168.128.1/24
```

# Scan de port
```
sudo nmap -sV -T4 -oN nmap.txt -iL hosts -Pn --open
```

# Scan de vulnérabilité sur systèmes obsolétes identifiés
```
nmap --script smb-vuln-ms08-067.nse -p445 -iL TargetOsUnsupported.txt
```
# Détection d'un utilisateur existant dans un système Windows AD
```
nmap -p 88 --script=krb5-enum-users --script-args="krb5-enum-users.realm='CORP',userdb=concatenated_names.txt" 192.168.200.18 -Pn 
```
L'exemple de fichier concatenated_names.txt est donné dans [[Linkedin]]

# Recherche de vulnérabilité smb
```
nmap --script smb-vuln* -p139,445 -iL TargetOsUnsupported.txt
```

# Recherche de machines
```bash
$ sudo nmap  -sT 192.168.56.0/24

```

# Analyse des ports ouvert :
```bash
$ sudo nmap -sV 192.168.56.15 -Pn -p1-65535


```
# Analyse détaillé des ports ouverts
```
$ sudo nmap -sC -sV -T4 -v -O 192.168.56.15 -Pn -p1-65535

```

# analyse UDP des 20 ports principaux :
```
$ sudo nmap -Pn -sU -A --top-ports=20 -v -oN udp.nmap 192.168.56.15

```
