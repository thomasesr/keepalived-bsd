# keepalived-bsd {{RELEASE_TAG}}

VRRP-like high-availability daemon for OPNsense. Coordinates MASTER/BACKUP
state between gateways via UDP heartbeats, manages virtual IPs and DHCP
per interface on state transition.

## Target platform

| | |
|---|---|
| **OPNsense** | {{OPNSENSE_VER}} |
| **FreeBSD**  | {{FREEBSD_VER}} |
| **Arch**     | amd64 |

## What's in this release

- `install.sh` — one-shot installer (fetches and places all files)
- `keepalived-bsd` — daemon binary (FreeBSD amd64 ELF)
- `keepalived-bsd.conf.example` — annotated config template
- `keepalived_bsd` — FreeBSD rc.d startup script
- `scripts.tar.gz` — DHCP backend helper scripts (ISC / Kea / dnsmasq)
- `opnsense-plugin.tar.gz` — OPNsense MVC plugin (models, controllers, views, configd actions)

## Install

```sh
fetch https://github.com/thomasesr/keepalived-bsd/releases/download/{{RELEASE_TAG}}/install.sh
sh install.sh
```

Then:

```sh
# Edit config
vi /usr/local/etc/keepalived-bsd.conf

# Enable + start
sysrc keepalived_bsd_enable=YES
service keepalived_bsd start
```

## Changes

- `0147da7` fix: install.sh always reinstalls all files on every run — no skips except active .conf
- `16163d9` fix: dhcp-opnsense-toggle.php — replace pfSense-only config_set_path/del_path with global $config array
- `4a12b17` feat: per-interface DHCP backend override — set dhcp_backend per [iface] section; omit or 'global' inherits from [global]
- `b9290f4` feat: install.sh config migration — adds missing required keys with defaults on upgrade; non-destructive
- `4b93dec` feat: firewall alias update on VIP state change — add/remove VIP from named OPNsense alias + filter reload on MASTER/BACKUP

## Checksums

```
{{CHECKSUMS}}
```
