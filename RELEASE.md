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

- `8581a3d` fix: OPNsense 26.1 compatibility + daemon failover correctness — graceful SIGTERM shutdown now removes VIPs, disables DHCP, and sends a GOODBYE (the loop previously ignored SIGTERM); DHCP backends fixed for 26.1 (dnsmasq drop-in dir `/usr/local/etc/dnsmasq.conf.d` + restart, Kea via `OPNsense/Kea` model, firewall alias via the MVC `Firewall/Alias` tree); ISC marked legacy (moved to `os-isc-dhcp`, default backend now `none`); interface delete/edit fixed (`delBase` 2-arg); menu moved to canonical `Menu/Menu.xml`, ACL consolidated; invalid-VIP and split-brain hardening

## Checksums

```
{{CHECKSUMS}}
```
