https://docs.oracle.com/cd/E24843_01/html/E22298/remotehowtoaccess-14.html
# connection

```
$ ftp 192.168.56.15    
```

# aide
```
ftp> help
Commands may be abbreviated.  Commands are:

!               chmod           exit            image           mls             nmap            proxy           reset           sndbuf          usage
$               close           features        lcd             mlsd            ntrans          put             restart         status          user
account         cr              fget            less            mlst            open            pwd             rhelp           struct          verbose
append          debug           form            lpage           mode            page            quit            rmdir           sunique         xferbuf
ascii           delete          ftp             lpwd            modtime         passive         quote           rstatus         system          ?
bell            dir             gate            ls              more            pdir            rate            runique         tenex
binary          disconnect      get             macdef          mput            pls             rcvbuf          send            throttle
bye             edit            glob            mdelete         mreget          pmlsd           recv            sendport        trace
case            epsv            hash            mdir            msend           preserve        reget           set             type
cd              epsv4           help            mget            newer           progress        remopts         site            umask
cdup            epsv6           idle            mkdir           nlist           prompt          rename          size            unset
```

# Commandes usuelles

	help    =>     liste des commandes acceptées
	ls      =>     lister les noms de fichiers présents
	cd      =>     changer le dossier courant
	get <nom fichier>
	mget <nom fichier>  =>     téléchargement des fichiers sur la machine cliente (attaque)
	bye => Quitter
	 