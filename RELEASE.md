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

This release is the complete `feat/vrrpv3-rewrite` fork off master at **v0.1.21**
(commit `ea598a0`). All 16 commits from v0.1.21 → v0.2.0, newest first:

- `47245f4` docs: list the transport-IPsec commit in the release notes
- `3e20e2c` docs(ipsec): transport-mode IPsec templates (`swanctl-opnsense.conf` / `swanctl-openwrt.conf`, ESP/PSK, proto-112 selector, policy `require`) + on-box setup/failover runbook for securing the VRRPv3 link
- `a670d4b` fix(alias): include `<sys/socket.h>` for `AF_INET` (build fix)
- `f0f9c36` docs: correct the `scripts.tar.gz` contents listed in the release notes
- `4873332` fix(install): installer now writes VRRPv3 `[vrrp_instance]` config (was legacy `[iface]`)
- `2585bec` docs: rewrite CLAUDE.md / README / RELEASE.md for VRRPv3 + migration note (Phase 10)
- `79cabcd` feat(opnsense): replace the global+iface plugin model/UI with a per-instance VRRPv3 model — `vrrp_instance` grid + add/edit modal, add/del instance API, config generator, and a live VRRP status table
- `5a51de5` feat(opnsense): `status_detail` configd action + API surfacing the daemon's per-instance JSON status
- `cb821ef` feat(vrrp): atomic per-instance JSON status file (`/var/run/keepalived_bsd.status`)
- `0adf549` feat(vrrp): transition side-effects — VIP add/del, DHCP toggle, gratuitous ARP, firewall-alias update, per instance
- `74b6139` feat(vrrp): per-VRID FSM with RFC 5798 timers (Skew_Time / Master_Down_Interval), preempt and priority-0 resign
- `e4140ee` feat(config): `[vrrp_instance NAME]` INI parser (repeatable `vip`, per-instance priority/DHCP, legacy `[iface]` rejected)
- `d373f25` feat(vrrp): raw IP proto-112 unicast transport, TTL=255 on send, drop-on-recv for TTL != 255
- `78fdc4a` chore: gitignore the `tests/run_vrrp` build artifact
- `4ac31c2` feat(vrrp): VRRPv3 advert codec + IPv4 pseudo-header checksum (RFC 5798 §5.1)
- `17e7980` feat(vrrp): start the VRRPv3 rewrite — `PLAN.md` + `include/vrrp.h` protocol header

### What this fork changes vs v0.1.21 (KALV)

| Aspect | v0.1.21 (KALV) | v0.2.0 (VRRPv3) |
|---|---|---|
| Wire protocol | custom `KALV` UDP heartbeat (port 5405) | real VRRPv3 (RFC 5798), raw IP proto 112, TTL=255 |
| Election | single whole-node FSM, global priority | N independent per-VRID FSMs, per-instance priority |
| Config | `[global]` + `[iface]` | `[vrrp_instance NAME]` (keepalived-style) |
| Interop | none (proprietary) | native with stock keepalived peer |
| Security | none | transport-mode IPsec (ESP/PSK) templates + runbook |
| OPNsense UI | per-iface | per-instance grid + add/edit modal + live status table |

**Breaking:** no auto-migration — old KALV configs are rejected with a migration error.

## Checksums

```
{{CHECKSUMS}}
```
