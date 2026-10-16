Scan d'un compte sur plusieurs IP d'un domaine :
```bash
for ip in $(cat ips.txt); do echo "Trying ${ip} ..."; impacket-rdp_check corp.com/stephanie:'LegmanTeamBenzoin!!'@${ip}; echo "\n"; done
```
