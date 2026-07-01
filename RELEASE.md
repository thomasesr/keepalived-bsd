# keepalived-bsd {{RELEASE_TAG}}

Real **VRRPv3 (RFC 5798)** high-availability daemon for OPNsense. Runs N
independent per-VRID instances that elect MASTER/BACKUP via unicast VRRPv3
advertisements (IP protocol 112) and manage virtual IPs, gratuitous ARP and
DHCP on transition. Interoperates natively with a stock keepalived peer.

> **Breaking change:** this release replaces the old custom `KALV` UDP protocol
> and `[global]`+`[iface]` config with real VRRPv3 and `[vrrp_instance NAME]`
> config. Old configs are rejected with a migration error — see README
> "Migrating". VRRPv3 has no in-packet auth; protect the proto-112 link with
> transport-mode IPsec (ESP/PSK). There is no automatic migration.

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
- `scripts.tar.gz` — helper scripts: DHCP backends (dnsmasq / Kea / ISC) + firewall-alias update
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

Since **v0.2.1**, newest first:

- `12b37f3` feat(ui): replace the freeform VIP textarea with a per-row table — address/prefix textbox plus a device dropdown listing OPNsense interfaces by label. New `getVipDevices` API maps each interface to its FreeBSD device (`igb0`, `igb0.10`); the on-disk config format is unchanged, so existing instances round-trip untouched.

## Checksums

```
{{CHECKSUMS}}
```
