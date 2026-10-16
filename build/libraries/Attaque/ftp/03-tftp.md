Le Tftp est semblable au ftp

# connexion
```
$ tftp 192.168.56.15 36969
tftp> ?
tftp-hpa 5.2
Commands may be abbreviated.  Commands are:

connect         connect to remote tftp
mode            set file transfer mode
put             send file
get             receive file
quit            exit tftp
verbose         toggle verbose mode
trace           toggle packet tracing
literal         toggle literal mode, ignore ':' in file name
status          show current status
binary          set mode to octet
ascii           set mode to netascii
rexmt           set per-packet transmission timeout
timeout         set total retransmission timeout
?               print help information
help            print help information
tftp>
```

Par défaut, on a la liste des commandes disponibles

# fichier de configuration de  vsftpd :
```
/etc/vsftpd.conf
```

