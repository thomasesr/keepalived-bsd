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

## Install

```sh
fetch https://github.com/thomasesr/keepalived-bsd/releases/download/{{RELEASE_TAG}}/keepalived-bsd
install -m 0755 keepalived-bsd /usr/local/sbin/keepalived-bsd
```

Or clone and use `make install-all` to deploy everything in one step.

## Changes

<!-- fill in before tagging -->

## Checksums

```
{{CHECKSUMS}}
```
