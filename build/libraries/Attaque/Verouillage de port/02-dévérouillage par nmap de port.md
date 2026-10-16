```
for x in 4000 5000 6000; do nmap -Pn --host-timeout 201 --max-retries 0 -p $x server_ip_address; done
```