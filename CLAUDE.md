# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

C daemon (`keepalived-bsd`) that runs on OPNSense (FreeBSD) and implements **real VRRPv3 (RFC 5798)** gateway failover, so it interoperates natively with a stock keepalived peer (OpenWRT/Linux). Runs N independent per-VRID instances; each elects MASTER/BACKUP via unicast VRRPv3 adverts (IP proto 112), then manages that instance's virtual IPs, gratuitous ARP, and DHCP server state. Ships with an OPNSense MVC plugin for web UI configuration.

> **History:** earlier releases used a custom `KALV` UDP heartbeat protocol with a single whole-node FSM. The `feat/vrrpv3-rewrite` branch replaced that with real VRRPv3 — a hard break of both the wire protocol and the config format (see "Config file" below and RELEASE.md migration note).

## Build & run

```sh
make                        # build binary: ./keepalived-bsd
make clean
make install                # /usr/local/sbin/keepalived-bsd + default conf
make install-rc             # /usr/local/etc/rc.d/keepalived_bsd
make install-opnsense       # OPNSense MVC plugin files
make install-all            # all three above

# Run (root required)
./keepalived-bsd -c keepalived-bsd.conf -f   # foreground / stderr logging
./keepalived-bsd -c keepalived-bsd.conf      # daemonize / syslog
```

## Repository layout

```
src/            C daemon source
include/        C headers
rc.d/           FreeBSD rc.d startup script
opnsense/       OPNSense plugin (install with make install-opnsense)
  +PLUGIN.php               plugin manifest, menu + ACL registration
  service/conf/actions.d/   configd action definitions (start/stop/status)
  mvc/app/
    models/OPNsense/Keepalived/   Keepalived.xml (field defs) + Keepalived.php
    controllers/OPNsense/Keepalived/
      IndexController.php         serves UI page
      Api/ServiceController.php   start/stop/restart/status/statusDetail/apply API
      Api/SettingsController.php  settings CRUD + VRRP instance add/del
  scripts/OPNsense/Keepalived/
      reconfigure.php             config.xml → keepalived-bsd.conf generator
    views/OPNsense/Keepalived/
      index.volt                  Bootstrap UI: service controls, general settings,
                                  VRRP instance grid + add/edit modal, VRRP status table
```

## C daemon architecture

```
main.c       → arg parse (-c config, -f foreground), daemonize, signals, multi-instance init
config.c     → INI parser: [global] + N [vrrp_instance <NAME>] sections
vrrp.c       → VRRPv3 advert encode/decode + pseudo-header checksum (RFC 5798 §5.1)
net.c        → raw IP proto-112 socket: IP_TTL=255 send, recv-validate (drop TTL != 255)
state.c      → per-VRID FSM (Initialize/Backup/Master), RFC 5798 timers, 50 ms poll loop
sidefx.c     → transition orchestration: calls iface/dhcp/arp/alias (keeps state.c portable)
iface.c      → SIOCAIFADDR / SIOCDIFADDR ioctls (FreeBSD in_aliasreq), VIP on its own dev
arp.c        → gratuitous ARP via BPF (/dev/bpf) on becoming Master
dhcp.c       → per-iface enable/disable via execv to backend helper scripts (no shell injection)
alias.c      → OPNsense firewall-alias membership update per instance
status.c     → atomic JSON status file writer (/var/run/keepalived_bsd.status)
logger.c     → openlog / vsyslog, mirrors to stderr in foreground mode
```

`state.c` links against no-op side-effect stubs in the unit tests, so the FSM stays
host-portable and testable; `sidefx.c` provides the real FreeBSD implementation.

## Key design constraints

- **FreeBSD target**: VIP via `SIOCAIFADDR`/`SIOCDIFADDR` with `struct in_aliasreq` — not Linux `ifreq`. DHCP via `/usr/local/sbin/configctl`. gARP via `/dev/bpf`.
- **No external deps**: pure C99, POSIX, BSD libc only.
- **Root required**: raw socket (proto 112), ioctl on interfaces, `/dev/bpf`.
- **Config file**: `/usr/local/etc/keepalived-bsd.conf` — INI, `[global]` (fallback `priority`/`dhcp_backend`) + one `[vrrp_instance NAME]` block per VRRP instance. Keys mirror keepalived: `state`, `interface`, `unicast_src_ip`, `unicast_peer`, `virtual_router_id`, `priority`, `advert_int` (seconds), `preempt`, repeatable `vip = ADDR/prefix [dev IF] [label L]`, `alias`, `dhcp_backend`. Old `[iface X]` blocks are rejected with a migration error. See `keepalived-bsd.conf.example`.
- **Wire protocol**: real **VRRPv3 (RFC 5798)**, unicast, IP proto 112, TTL **must be 255** (enforced on send AND drop-on-recv). Checksum includes the IPv4 pseudo-header (§5.1.1.4) — differs from VRRPv2. No in-packet auth; secure the link with **transport-mode IPsec** (ESP/PSK) between the two hosts — kernel-level, transparent to the daemon's raw socket. `net.c` deliberately avoids `IP_HDRINCL` (kernel builds the IP header via `IP_TTL`+`bind`, sidestepping the BSD `ip_len`/`ip_off` byte-order hazard).
- **DHCP is per-interface** (call-wise): `dhcp_enable_iface`/`dhcp_disable_iface` invoked from `sidefx.c` on each instance's Master/Backup transition, deduplicated by distinct VIP interface. Backends on OPNsense 26.1:
  - **dnsmasq** (26.1 default): writes/removes `no-dhcp-interface=<iface>` drop-in in `/usr/local/etc/dnsmasq.conf.d/` (the `conf-dir` OPNsense actually passes to dnsmasq — `*.conf`), then `configctl dnsmasq restart`. SIGHUP does **not** re-read conf-dir files, so a restart is required; modern dnsmasq re-reads its leases file on start so leases survive.
  - **kea**: no per-interface enable exists — Kea is one service bound to an interface list. The toggle flips the global enable flag at `$config['OPNsense']['Kea']['dhcp4']['general']['enabled']`, then `configctl template reload OPNsense/Kea` + `configctl kea start|stop`. Effectively whole-service: MASTER serves, BACKUP stops.
  - **ISC (dhcpd)** — *legacy*: moved out of core into the `os-isc-dhcp` plugin in 26.1 and absent on fresh installs. Toggles the legacy per-iface `$config['dhcpd'][<iface>]['enable']` then `configctl dhcpd restart`; the helper no-ops cleanly if the plugin is not installed.
  - Default backend is `none` (fail closed) — an unknown/omitted backend manages no DHCP rather than silently invoking dead ISC.

## State machine (per VRID, RFC 5798 §6)

States: **Initialize → Backup / Master**. Each instance runs independently → load
sharing (Master on one VRID, Backup on another). Timers in centiseconds:
`Skew_Time = ((256 - priority) * Master_Adver_Interval)/256`,
`Master_Down_Interval = 3*Master_Adver_Interval + Skew_Time`.

| From | Event | To | Side-effect |
|------|-------|----|-------------|
| Initialize | `priority == 255` (owner) | Master | send advert + gARP, add VIPs, enable DHCP |
| Initialize | otherwise | Backup | arm Master_Down_Timer |
| Backup | Master_Down_Timer fires | Master | send advert, gARP, add VIPs, enable DHCP |
| Backup | recv advert, priority 0 | Backup | set timer to Skew_Time (peer resigning) |
| Backup | recv advert, priority ≥ own (or preempt off) | Backup | reset Master_Down_Timer |
| Master | recv advert, higher priority (or equal + higher src IP) | Backup | remove VIPs, disable DHCP |
| Master | SIGTERM / shutdown | Backup | send **priority-0** resign advert, then release |

## OPNSense plugin wiring

- `dhcp_backend` config key maps to `dhcp_backend_t` enum; `dhcp_backend_parse()` in `dhcp.c` converts strings at load time.
- DHCP helper scripts live in `/usr/local/libexec/keepalived-bsd/`. Called via `execv` (not `system`) to prevent shell injection. Iface names validated as alphanumeric-only before exec.
- configd actions (`actions_keepalived.conf`) bridge PHP → rc.d script.
- `ServiceController` calls `configdRun('keepalived <action>')`: `start`/`stop`/`restart`/`status`, plus `statusDetail` (decodes the daemon's JSON status file) and `apply` (runs the `reconfigure` action).
- configd `status_detail` action just `cat`s `/var/run/keepalived_bsd.status` (`|| echo '{}'`); `reconfigure` runs `scripts/OPNsense/Keepalived/reconfigure.php`, which renders `config.xml` → `keepalived-bsd.conf` `[vrrp_instance]` blocks and restarts the daemon if enabled.
- `SettingsController` extends `ApiMutableModelControllerBase` — `get`/`set`/`getInterfaces`/`addInstance`/`delInstance` (ArrayField path `vrrp_instances.vrrp_instance`).
- Model XML lives at `//OPNsense/keepalived` in `config.xml`; the `vrrp_instance` ArrayField uses `UniqueConstraint` on `name` and `virtual_router_id`. Model version is 2.0.0 (VRRPv3 rewrite).
- The InterfaceField `interface` stores an OPNsense iface key (`lan`, `opt1`); `reconfigure.php` maps it to the real FreeBSD device (`igb0`, `igb0.10`). VIP `dev` is typed as a raw BSD ifname (manual mapping — VLAN sub-ifaces may not be OPNsense-managed).
- **Security:** VRRPv3 has no in-packet auth. Protect the proto-112 unicast link with transport-mode IPsec (ESP/PSK, policy `require`) between the two hosts — see RELEASE.md / PLAN.md §6. Managed by OPNsense's IPsec subsystem, not this daemon.
- rc.d script uses underscores (`keepalived_bsd`), binary uses hyphens (`keepalived-bsd`).
- After `install-opnsense`, restart configd: `service configd restart`.

## RELEASE.md maintenance

**When tagging a new release, replace the `## Changes` section** with only the commits since the previous release tag. Do not carry forward commits from older releases.

Get the range with:
```sh
git log <prev-tag>..HEAD --oneline
```

Format each commit as:
```
- `<short-sha>` <conventional-type>: <what changed and why, one sentence>
```

Example (tagging v0.1.2 after v0.1.1):
```
- `abe0346` fix: commit actions_keepalived.conf — excluded by *.conf gitignore
- `984afee` docs: update RELEASE.md for v0.1.2
```

Rules:
- Only commits since the **previous tag** — not the full history.
- Use the actual short SHA from `git log --oneline`.
- Keep wording tight — focus on user-visible effect, not implementation detail.
- Do not remove or reformat the placeholder lines (`{{RELEASE_TAG}}` etc.) — filled at release time by the Dockerfile.
- Skip pure doc/meta commits unless they affect the install or upgrade experience.
