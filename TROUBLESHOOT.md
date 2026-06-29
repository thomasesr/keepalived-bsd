# Troubleshooting keepalived-bsd OPNsense Plugin

## Diagnostic commands

### 1. Verify file tree

```sh
find /usr/local/opnsense/mvc/app/*/OPNsense/Keepalived -type f
find /usr/local/opnsense/service/conf/actions.d -name '*keepalived*'
ls /usr/local/opnsense/mvc/app/controllers/OPNsense/Keepalived/menu.xml
```

### 2. Check PHP errors

```sh
tail -50 /var/log/lighttpd/error.log
tail -50 /tmp/PHP_errors.log
```

### 3. Test model loads

```sh
php -r "require_once('/usr/local/opnsense/mvc/app/models/OPNsense/Keepalived/Keepalived.php'); echo 'OK';"
```

### 4. Test configd action

```sh
configctl keepalived status
```

Expected: `running` or `stopped`. If `action keepalived.status not found` → see fix below.

### 5. Verify tarball contents

```sh
tar -tzf opnsense-plugin.tar.gz | sort
```

Must include `./service/conf/actions.d/actions_keepalived.conf`.

---

## Common fixes

### "action keepalived.status not found"

configd hasn't loaded the actions file. Either it's missing or configd wasn't restarted.

```sh
# Check if file exists
ls -la /usr/local/opnsense/service/conf/actions.d/actions_keepalived.conf

# If missing, create it manually
cat > /usr/local/opnsense/service/conf/actions.d/actions_keepalived.conf << 'EOF'
[keepalived.start]
command:/usr/local/etc/rc.d/keepalived_bsd onestart
parameters:
type:script
message:starting keepalived-bsd

[keepalived.stop]
command:/usr/local/etc/rc.d/keepalived_bsd onestop
parameters:
type:script
message:stopping keepalived-bsd

[keepalived.restart]
command:/usr/local/etc/rc.d/keepalived_bsd onerestart
parameters:
type:script
message:restarting keepalived-bsd

[keepalived.status]
command:/usr/local/etc/rc.d/keepalived_bsd onestatus
parameters:
type:script_output
message:getting keepalived-bsd status
EOF

# Restart configd — mandatory after any actions.d change
service configd restart

# Verify
configctl keepalived status
```

### "Unexpected error" on UI page

Clear OPNsense template and menu cache, then reload:

```sh
rm -rf /tmp/opnsense_cache
rm -f /tmp/*.cache
service configd restart
```

Refresh browser. If error persists, check `/var/log/lighttpd/error.log` for PHP exception details.

### "Keepalived HA" missing from Services menu

menu.xml must exist and cache must be cleared:

```sh
cat /usr/local/opnsense/mvc/app/controllers/OPNsense/Keepalived/menu.xml
rm -rf /tmp/opnsense_cache
```

Refresh browser with hard reload (Ctrl+Shift+R).

### Re-extract plugin from release tarball

```sh
fetch https://github.com/thomasesr/keepalived-bsd/releases/latest/download/opnsense-plugin.tar.gz
tar -xzf opnsense-plugin.tar.gz -C /usr/local/opnsense
service configd restart
```

### API endpoint test

```sh
curl -sk -u root:<password> https://localhost/api/keepalived/service/status
```

Expected: `{"status":"stopped"}` or `{"status":"running"}`.

---

## Symptom table

| Symptom | Cause | Fix |
|---|---|---|
| `action keepalived.status not found` | actions.d file missing or configd not restarted | Create file + `service configd restart` |
| `/ui/keepalived` → 404 | Files extracted to wrong path | Check tarball extracted to `/usr/local/opnsense` (not a subdir) |
| "Unexpected error" on page | PHP parse/runtime error | Check `/var/log/lighttpd/error.log` |
| Services menu missing "Keepalived HA" | menu.xml missing or cache stale | Verify file exists + `rm -rf /tmp/opnsense_cache` |
| API 401/403 | ACL not granted | System → Access → Groups → add "Keepalived HA" privilege |
| `configctl keepalived start` does nothing | rc.d script not installed | `install -m 0755 keepalived_bsd /usr/local/etc/rc.d/keepalived_bsd` |
