https://hackviser.com/tactics/pentesting/services/snmp

# paquets à installer :
```
sudo apt install snmp -y

sudo apt install snmp-mibs-downloader

sudo download-mibs
```
Modifier /etc/snmp/snmp.conf en mettant mibs +ALL


# snmp check
```
snmp-check -t 192.168.1.1
```

# Using `snmpwalk`
##### For SNMPv1/v2c (most common for pentesting due to weaker security)
```
snmpwalk -c <community_string> -v1 <target_ip>
snmpwalk -c <community_string> -v2c <target_ip>
```
##### Example: Walking the entire MIB tree
```
snmpwalk -c public -v2c 192.168.1.1
```
##### Example: Walking a specific OID
```
snmpwalk -c public -v2c 192.168.1.1 .1.3.6.1.2.1.1.1.0 # sysDescr
```

```
snmpwalk -v2c -c public 192.168.122.156 NET-SNMP-EXTEND-MID::nsExtendObjects
```
Si ON n'a pas cette extension installé : https://cric.grenoble.cnrs.fr/Administrateurs/Outils/MIBS/?module=NET-SNMP-EXTEND-MIB&fournisseur=netsnmp

et on trouve :
```
snmpwalk -v2c -c public 192.168.122.156 .1.3.6.1.4.1.8072.1.3.2
```

# onesixtyone :
```
onesixtyone -c /usr/share/doc/onesixtyone/dict.txt 192.168.122.156

onesixtyone -c /usr/share/seclists/Discovery/SNMP/snmp-onesixtyone.txt 192.168.122.156

```

# nmap
Pour trouver les community_string :
```
nmap -sU -p 161 --script snmp-brute --script-args snmp-brute.communitiesdb=/usr/share/seclists/Discovery/SNMP/snmp-onesixtyone.txt 192.168.122.156

```