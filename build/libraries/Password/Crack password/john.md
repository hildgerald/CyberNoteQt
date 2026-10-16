```
john --wordlist=/usr/share/wordlists/rockyou.txt ssh.hash
```

Pour un hash NTLM :
```
john --wordlist=/usr/share/wordlists/rockyou.txt --format=NT --rules wario.hashcat 

```