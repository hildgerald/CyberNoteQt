# Créer un fichier d'adresses IP du domaine
On crée un fichier ips.txt contenant les ips du domaine

# Création du fichier des hotes du domaine
On utilise nxc et le fichier **ips.txt** avec le nom de compte dont on dispose déjà :
```bash
nxc smb ips.txt -u jen -p 'Nexus123!' -d corp.com --generate-hosts-file /home/kali/Desktop/hosts
```

On ajoute le résultat dans le fichier **/etc/hosts** de kali afin de résoudre directement les noms d'hote. Attention au dédoublement des résultats.

# Mise à jour de resolv.conf
On rajoute le serveur DC1 en tant que DNS et ceux de google dans le fichier **/etc/resolv.conf** par ex :
```
search localdomain

# Serveur DC de l'Active Directory
nameserver 192.168.207.70

# Serveur Google pour maintient de la connexion internet
nameserver 8.8.8.8
nameserver 8.8.4.4
```
C'est important afin de récupérer des tickets avec **krb5-user**
