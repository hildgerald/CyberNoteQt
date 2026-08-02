Sur Kali :
On modifie le fichier de configuration /etc/proxychains4.conf et on ajoute :
```
# proxychains.conf  VER 4.x
#
...

[ProxyList]
# add proxy here ...
# meanwile
# defaults set to "tor"
socks5 	127.0.0.1 1080
```
On vérifie que dans /etc/ssh/sshd_config:
PasswordAuthentication yes

On démarre le serveur SSH sur Kali :
```
sudo systemctl start ssh
```
Sur Windows :
```
ssh -N -R 1080 kali@192.168.45.184
```
Sur Kali, on vérifie que la connexion a bien eu lieu :
```
$ sudo ss -ntplu
```
![[Pasted image 20250604161352.png]]
le port 1080 est bien ouvert.

ON accéde maintenant au réseau interne !!!