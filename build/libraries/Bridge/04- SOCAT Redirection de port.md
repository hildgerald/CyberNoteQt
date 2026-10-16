
![[Pasted image 20250124130949.png]]
Sur Confluence, on transfére tous les paquet du port 2345 vers le serveur PGDatabase01 (10.4.50.215) sur le port 5432
```
socat -ddd TCP-LISTEN:2345,fork TCP:10.4.50.215:5432
```