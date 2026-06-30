# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

C daemon (`keepalived-bsd`) that runs on OPNSense (FreeBSD) and implements VRRP-like gateway failover. Coordinates MASTER/BACKUP state with a peer (OpenWRT/Linux) via UDP heartbeats, then manages virtual IPs and DHCP server state on interfaces. Ships with an OPNSense MVC plugin for web UI configuration.

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
      Api/ServiceController.php   start/stop/restart/status API
      Api/SettingsController.php  settings CRUD + interface add/del
    views/OPNsense/Keepalived/
      index.volt                  Bootstrap UI: status, settings form, iface table
```

## C daemon architecture

```
main.c       → arg parse (-c config, -f foreground), daemonize, signals
config.c     → INI parser: [global] + [iface <name>] sections
heartbeat.c  → UDP socket open/send/recv, hb_packet_t wire format
state.c      → MASTER/BACKUP FSM, 100 ms poll loop, transition side-effects
iface.c      → SIOCAIFADDR / SIOCDIFADDR ioctls (FreeBSD in_aliasreq)
dhcp.c       → per-iface enable/disable via execv to backend helper scripts (no shell injection)
logger.c     → openlog / vsyslog, mirrors to stderr in foreground mode
```

## Key design constraints

- **FreeBSD target**: VIP via `SIOCAIFADDR`/`SIOCDIFADDR` with `struct in_aliasreq` — not Linux `ifreq`. DHCP via `/usr/local/sbin/configctl`.
- **No external deps**: pure C99, POSIX, BSD libc only.
- **Root required**: UDP bind + ioctl on interfaces.
- **Config file**: `/usr/local/etc/keepalived-bsd.conf` — INI, `[global]` + one `[iface X]` block per managed interface.
- **Wire protocol**: custom UDP (`HB_MAGIC = "KALV"`, versioned). Keep `hb_packet_t` backwards-compatible when adding fields; bump `HB_VERSION` on breaking changes.
- **DHCP is per-interface** (call-wise): `dhcp_enable_iface`/`dhcp_disable_iface` called for each interface in the FSM transition loop. Backends on OPNsense 26.1:
  - **dnsmasq** (26.1 default): writes/removes `no-dhcp-interface=<iface>` drop-in in `/usr/local/etc/dnsmasq.conf.d/` (the `conf-dir` OPNsense actually passes to dnsmasq — `*.conf`), then `configctl dnsmasq restart`. SIGHUP does **not** re-read conf-dir files, so a restart is required; modern dnsmasq re-reads its leases file on start so leases survive.
  - **kea**: no per-interface enable exists — Kea is one service bound to an interface list. The toggle flips the global enable flag at `$config['OPNsense']['Kea']['dhcp4']['general']['enabled']`, then `configctl template reload OPNsense/Kea` + `configctl kea start|stop`. Effectively whole-service: MASTER serves, BACKUP stops.
  - **ISC (dhcpd)** — *legacy*: moved out of core into the `os-isc-dhcp` plugin in 26.1 and absent on fresh installs. Toggles the legacy per-iface `$config['dhcpd'][<iface>]['enable']` then `configctl dhcpd restart`; the helper no-ops cleanly if the plugin is not installed.
  - Default backend is `none` (fail closed) — an unknown/omitted backend manages no DHCP rather than silently invoking dead ISC.

## State machine

BACKUP is default initial state. Transitions:

| From | Event | To | Side-effect |
|------|-------|----|-------------|
| BACKUP | peer silent ≥ timeout | MASTER | add VIPs, enable DHCP |
| MASTER | peer heartbeat with higher priority | BACKUP | remove VIPs, disable DHCP |
| MASTER | SIGTERM / shutdown | BACKUP | remove VIPs, disable DHCP, send goodbye packet |

## OPNSense plugin wiring

- `dhcp_backend` config key maps to `dhcp_backend_t` enum; `dhcp_backend_parse()` in `dhcp.c` converts strings at load time.
- DHCP helper scripts live in `/usr/local/libexec/keepalived-bsd/`. Called via `execv` (not `system`) to prevent shell injection. Iface names validated as alphanumeric-only before exec.
- configd actions (`actions_keepalived.conf`) bridge PHP → rc.d script.
- `ServiceController` calls `configdRun('keepalived <action>')`.
- `SettingsController` extends `ApiMutableModelControllerBase` — `get`/`set`/`addInterface`/`delInterface` are the only endpoints needed.
- Model XML lives at `//OPNsense/keepalived` in `config.xml`.
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
