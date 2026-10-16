# Redirection de port style socat
Sur la Raspi (192.168.198.4) on créé un port forwarding entre le port 8443 de la Raspi et le port 443 de l'EMIS (192.168.10.1 )
```
ssh -L 8443:192.168.10.1:443 emiscasa@192.168.198.4
```
# Redirection "local port forwarding"
```
ssh -N -L 0.0.0.0:4455:172.16.234.217:445 database_admin@10.4.234.215
```

```
```

```
```

```
```

```
```

```
```

```
```

```
```

```
```

