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

- `keepalived-bsd` — daemon binary (FreeBSD amd64 ELF)
- `keepalived-bsd.conf.example` — annotated config template
- `rc.d/keepalived_bsd` — FreeBSD rc.d startup script
- `scripts/` — DHCP backend helper scripts (ISC / Kea / dnsmasq)
- `opnsense-plugin.tar.gz` — OPNsense MVC plugin (models, controllers, views, configd actions)

## Install

```sh
# Daemon
fetch https://github.com/thomasesr/keepalived-bsd/releases/download/{{RELEASE_TAG}}/keepalived-bsd
install -m 0755 keepalived-bsd /usr/local/sbin/keepalived-bsd

# OPNsense UI plugin
fetch https://github.com/thomasesr/keepalived-bsd/releases/download/{{RELEASE_TAG}}/opnsense-plugin.tar.gz
tar -xzf opnsense-plugin.tar.gz -C /usr/local/opnsense
service configd restart
```

Or download the full release and use `make install-all` to deploy everything in one step.

## Changes

- `267a5ec` feat: initial scaffold — C daemon (FSM, UDP heartbeat, VIP ioctls, DHCP control), OPNsense MVC plugin, Makefile, rc.d script, conf.example
- `1c53cde` refactor: per-interface DHCP config toggle — modifies backend config + graceful reload instead of killing daemon; ISC/Kea via config.xml PHP helper, dnsmasq via no-dhcp-interface drop-in + SIGHUP
- `38fbde1` docs: RELEASE.md template with placeholder substitution for tag, FreeBSD version, and checksums — filled by Dockerfile at release time
- `32b166e` fix: include opnsense/ plugin files in release as opnsense-plugin.tar.gz so make install-opnsense works without cloning the repo
- `abe0346` fix: commit actions_keepalived.conf — was excluded by *.conf gitignore, causing configctl keepalived.status to return "not found"

## Checksums

```
{{CHECKSUMS}}
```
