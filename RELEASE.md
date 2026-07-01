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

- `79cabcd` feat(opnsense): replace the global+iface plugin model/UI with a per-instance VRRPv3 model — `vrrp_instance` grid + add/edit modal, add/del instance API, config generator, and a live VRRP status table
- `5a51de5` feat(opnsense): `status_detail` configd action + API surfacing the daemon's per-instance JSON status
- `cb821ef` feat(vrrp): atomic per-instance JSON status file (`/var/run/keepalived_bsd.status`)
- `0adf549` feat(vrrp): transition side-effects — VIP add/del, DHCP toggle, gratuitous ARP, firewall-alias update, per instance
- `74b6139` feat(vrrp): per-VRID FSM with RFC 5798 timers (Skew_Time / Master_Down_Interval), preempt and priority-0 resign
- `e4140ee` feat(config): `[vrrp_instance NAME]` INI parser (repeatable `vip`, per-instance priority/DHCP, legacy `[iface]` rejected)
- `d373f25` feat(vrrp): raw IP proto-112 unicast transport, TTL=255 on send, drop-on-recv for TTL != 255
- `4ac31c2` feat(vrrp): VRRPv3 advert codec + IPv4 pseudo-header checksum (RFC 5798 §5.1)

## Checksums

```
{{CHECKSUMS}}
```
