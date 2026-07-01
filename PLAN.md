# PLAN — VRRPv3 rewrite (real RFC 5798 interop) + VRRP status view

## Goal

Convert `keepalived-bsd` from its custom **KALV UDP** protocol to **real VRRPv3
unicast (RFC 5798)**, multi-instance, so it interoperates natively with a stock
keepalived peer. Then add a LuCI-style **VRRP Status** view in the OPNsense UI.

This is a multi-phase rewrite of `heartbeat.c`, `state.c`, and `config.c`, plus
a large OPNsense model/UI change. The status page is the tail end — a readout on
top of the new VRRP engine.

## Confirmed decisions

- **Protocol:** real VRRPv3 (version 3), unicast. Replaces KALV. IP proto 112.
- **Config model:** named `[vrrp_instance NAME]` INI blocks mirroring keepalived's
  `vrrp_instance`.
- **Priority:** per-instance `priority`; existing global `priority` = fallback default.
- **Backward compat:** hard break of the old `[global]`+`[iface]` config and the
  KALV wire format. Bump project + config version. No auto-migration (documented).
- **Scope:** IPv4 only (peer is IPv4). IPv6 out of scope.
- **Auth/security:** VRRPv3 has no in-packet auth (RFC 5798 §9). Secure the proto-112
  unicast link with **OS transport IPsec** (ESP, PSK) between the two hosts. The
  "passcode" = the IPsec pre-shared key. **Daemon wire protocol unchanged** — IPsec is
  kernel-level and transparent to the raw socket. See §6.

## Peer we must interoperate with (reference)

Stock keepalived on OpenWRT/Linux (`router_id Firewall2`), VRRP v3, unicast:

- `unicast_src_ip 192.168.1.3` → `unicast_peer 192.168.1.1` (= our FreeBSD box)
- Advert interface on peer: `trunk`; all instances `state BACKUP`, `priority 100`,
  `advert_int 5`, `version 3`. No `use_vmac` (real MAC + gratuitous ARP).

| Instance | VRID | priority | state  | advert_int | VIP              | dev (peer) |
|----------|------|----------|--------|------------|------------------|------------|
| master   | 10   | 100      | BACKUP | 5s         | 192.165.1.2/24   | br-lan     |
| VI_666   | 66   | 100      | BACKUP | 5s         | 10.6.6.2/24      | br-666     |
| VI_IOT   | 13   | 100      | BACKUP | 5s         | 192.166.1.2/24   | br-iot     |
| VI_WAN   | 20   | 100      | BACKUP | 5s         | 192.167.1.2/24   | br-wan     |

> **Interface names differ on FreeBSD.** The peer's `br-lan/br-666/…` do not exist
> on OPNsense. Each instance's advert interface and VIP `dev` must be operator-set
> to the local FreeBSD interface (e.g. `igb0`, `igb0.10`). This mapping is manual.

## Current vs target

| Aspect     | KALV today                     | VRRPv3 target                          |
|------------|--------------------------------|----------------------------------------|
| Transport  | UDP port 5405                  | raw IP proto 112, TTL=255              |
| Format     | custom `KALV` 12-byte struct   | VRRPv3 advert + IPv4 pseudo-hdr cksum  |
| Instances  | whole-node, 1 FSM state        | per-VRID FSM, N independent instances  |
| Timing     | 1s heartbeat / 3s timeout      | advert_int + Master_Down + Skew_Time   |
| Election   | global priority + IP tiebreak  | per-instance priority (255=owner,0=stop)|

## Architecture overview

```
main.c        arg parse, daemonize, signals            KEEP (minor: multi-instance init)
logger.c      syslog/stderr                            KEEP
iface.c       SIOCAIFADDR/SIOCDIFADDR VIP add/del       KEEP (call per-instance, VIP on any dev)
dhcp.c        per-iface DHCP enable/disable             KEEP (hook to per-instance transitions)
arp.c   NEW   gratuitous ARP via BPF on becoming Master
vrrp.c  NEW    VRRPv3 advert encode/decode + checksum   (was heartbeat.c)
net.c   NEW    raw proto-112 socket send/recv, TTL=255
config.c      REWRITE  [vrrp_instance NAME] INI parser
state.c       REWRITE  per-VRID FSM (Init/Backup/Master), timers, transitions
status.c NEW   atomic JSON status file writer
```

### Core data structures (include/*.h)

```c
/* one configured VRRP instance */
typedef struct {
    char        name[32];        /* [vrrp_instance NAME] */
    uint8_t     vrid;            /* virtual_router_id 1..255 */
    uint8_t     priority;        /* 1..254 normal, 255=owner; 0 reserved (resign) */
    node_state_t initial;        /* configured state: BACKUP/MASTER */
    char        adv_if[IFNAMSIZ];/* advert interface (peer's "trunk") */
    struct in_addr src_ip;       /* unicast_src_ip */
    struct in_addr peer_ip;      /* unicast_peer */
    uint16_t    adver_cs;        /* Max Adver Int, centiseconds (advert_int*100) */
    int         preempt;         /* preempt flag */
    /* VIPs */
    vip_t       vips[MAX_VIPS];  /* addr/prefix + dev (may differ from adv_if) */
    int         vip_count;
    dhcp_backend_t dhcp_backend;
} vrrp_instance_t;

/* per-instance runtime (feeds FSM + status page) */
typedef struct {
    vrrp_instance_t *cfg;
    node_state_t  state;             /* Initialize/Backup/Master */
    int           sock;              /* raw proto-112 socket (per adv_if or shared) */
    time_t        master_down_at;    /* Master_Down_Timer deadline */
    time_t        last_adv_sent;
    time_t        last_transition;
    uint64_t      probes_sent;       /* adverts sent for this vrid */
    uint64_t      probes_received;   /* adverts recv'd matching this vrid */
} vrrp_rt_t;
```

## Reuse audit (file by file)

| File | Verdict | Note |
|------|---------|------|
| `main.c` | MODIFY | init N instances; signal handling drives per-instance shutdown (resign advert) |
| `logger.c` | KEEP | unchanged |
| `iface.c` | KEEP | `SIOCAIFADDR`/`SIOCDIFADDR` reused; VIP added to its own `dev`, not adv_if |
| `dhcp.c` | KEEP | `dhcp_enable_iface`/`disable` now called on per-instance Master/Backup |
| `heartbeat.c/.h` | REWRITE→`vrrp.c` | KALV struct gone; VRRPv3 encode/decode + checksum |
| `state.c/.h` | REWRITE | single FSM → array of per-VRID FSMs, RFC 5798 timers |
| `config.c/.h` | REWRITE | INI grammar `[vrrp_instance NAME]` |
| `arp.c` | NEW | gratuitous ARP (BPF) |
| `net.c` | NEW | raw socket setup, TTL=255 send + recv-validate |
| `status.c` | NEW | JSON status file |

## Design detail

### 1. Wire layer — `vrrp.c` / `net.c`

VRRPv3 IPv4 advert (RFC 5798 §5.1), on IP proto 112, TTL **must be 255**:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|Ver=3|Type=1 |    VRID       |   Priority    | Count IPv4 Addr |
+-------------+---------------+-------+-----------------------+-+
|(rsvd=0)|     Max Adver Int  |          Checksum             |
+-------------+---------------+-------------------------------+
|                       IPv4 Address(es)  (4 bytes each)       |
+-------------------------------------------------------------+
```

- First byte `0x31` (ver=3, type=1 ADVERTISEMENT). `Max Adver Int` = centiseconds
  (advert_int 5s → **500**), low 12 bits.
- **Checksum (§5.1.1.4): VRRPv3 includes the IPv4 pseudo-header** (src, dst, zero,
  proto=112, VRRP length) + the VRRP message, standard internet 16-bit ones-complement.
  Differs from VRRPv2 (which excluded the pseudo-header). **Must verify with a test
  vector against the real peer.**
- **Timers (§5.2):** `Skew_Time = ((256 - priority) * Master_Adver_Interval)/256`;
  `Master_Down_Interval = 3*Master_Adver_Interval + Skew_Time`. Interval in centiseconds.
- **Send:** raw `socket(AF_INET, SOCK_RAW, 112)`, `IP_HDRINCL` to control TTL=255 +
  src; `sendto` peer_ip (unicast). *(FreeBSD `ip_len`/`ip_off` byte-order quirk on
  `IP_HDRINCL` — verify at impl time.)*
- **Recv:** raw proto-112 socket delivers the IPv4 header inline for IPv4; read `ip_ttl`
  directly, **drop if != 255**. Demux by VRID → matching `vrrp_rt_t`. Unicast peer
  operation: no multicast join strictly required; join `224.0.0.18` only if a peer
  ever multicasts (flag as optional).

### 2. State machine — `state.c` (per VRID, RFC 5798 §6)

States: **Initialize → Backup / Master**.

- Init: if `priority == 255` → Master (send advert + gARP); else → Backup, arm
  `Master_Down_Timer`.
- **Backup:** Master_Down_Timer fires → **Master** (send advert, gARP for VIPs, add
  VIPs, enable DHCP). Recv advert: priority 0 → set timer to `Skew_Time`; priority ≥
  own (or preempt=false) → reset Master_Down_Timer, stay Backup.
- **Master:** send advert every `adver_cs`. Recv advert with higher priority (or equal
  + higher src IP) → **Backup** (remove VIPs, disable DHCP). Shutdown → send
  **priority-0** advert, then Backup side-effects.
- Side-effects reuse `iface.c` (VIP add/del on the VIP's own `dev`) + `dhcp.c` + new
  `arp.c` (gARP). Each instance independent → load sharing (Master on one VRID, Backup
  on another).

### 3. Config — `config.c` (`[vrrp_instance NAME]` INI)

Grammar mirrors keepalived. Example matching the peer (FreeBSD ifaces are placeholders
— operator must set real ones):

```ini
[global]
  # optional fallback default priority for instances that omit it
  priority = 100

[vrrp_instance master]
  state          = BACKUP
  interface      = igb0          # advert iface (peer's "trunk")
  unicast_src_ip = 192.168.1.1
  unicast_peer   = 192.168.1.3
  virtual_router_id = 10
  priority       = 110           # >100 to be preferred MASTER; =100 to match peer
  advert_int     = 5
  vip            = 192.165.1.2/24 dev igb0    # local equivalent of br-lan
  dhcp_backend   = dnsmasq

[vrrp_instance VI_666]
  interface = igb0 ; unicast_src_ip = 192.168.1.1 ; unicast_peer = 192.168.1.3
  virtual_router_id = 66 ; priority = 110 ; advert_int = 5
  vip = 10.6.6.2/24 dev igb0.666

# … VI_IOT (vrid 13), VI_WAN (vrid 20) likewise …
```

- Repeatable `vip` key for multiple VIPs per instance; `dev` suffix optional (defaults
  to `interface`).
- Validation: vrid 1–255 unique, priority 1–254 (255 = owner only if it truly owns the
  addr), src/peer required, advert_int ≥ 1. Fail-closed on parse error.
- **Breaking:** old `[iface X]` blocks rejected with a clear error pointing to migration.

### 4. Status page

Daemon → PHP via atomic JSON status file (chosen over socket/signal: no IPC lifecycle,
matches DHCP file-drop idiom).

- **Path:** `/var/run/keepalived_bsd.status`. Write temp + `fsync` + `rename()` (atomic).
- **Cadence:** on every transition + at most once/second (gated timer in the poll loop).
- **`written` epoch** included → UI flags rows stale if `now - written > 5s`.
- **configd action** `status_detail`: `cat` the file, `|| echo '{}'` when absent.
- **API** `ServiceController::statusDetailAction()` → `GET /api/keepalived/service/statusDetail`,
  returns the decoded array (`{written, instances:[…]}`).
- **UI** `index.volt`: new `content-box` panel, header text *"This overview shows the
  current status of the VRRP instances on this device."*, `table table-striped
  table-condensed`, `#vrrp-status-tbody` filled by a `loadVrrpStatus()` poller on
  `setInterval(…, 2000)`. `.text()` cells (XSS-safe); MASTER→`label-success`, else
  `label-default`.

Column → per-instance field:

| Column | Field |
|--------|-------|
| Name | `cfg->name` |
| Interface | `cfg->adv_if` (or VIP dev) |
| weight | `cfg->priority` |
| Active State | `rt->state` |
| Initial State | `cfg->initial` |
| Probes Sent | `rt->probes_sent` |
| Probes Received | `rt->probes_received` |
| Last Transition | `rt->last_transition` (epoch → UI formats) |

### 5. OPNsense plugin changes (large)

- **Model** `Keepalived.xml`: replace `[global]`+`[iface]` shape with a `vrrp_instance`
  ArrayField grid (name, vrid, priority, state, interface, unicast_src_ip, unicast_peer,
  advert_int, vip list, dhcp_backend).
- **`SettingsController`**: `addInstance`/`delInstance` (replace `addInterface`/`delInterface`).
- **`IndexController` + `index.volt`**: instance grid + add/edit modal; VIP sub-list;
  the new status table.
- **Config template**: generator that renders `config.xml` → `keepalived-bsd.conf`
  `[vrrp_instance]` blocks.
- After `install-opnsense`: `service configd restart`.

### 6. Security — transport IPsec (keeps VRRPv3)

Because VRRPv3 carries no authentication, the VRRP unicast (IP proto 112) between the
two peers is protected by an **OS-level IPsec transport-mode SA**. The daemon does not
implement any of this — it sends/receives plaintext VRRP on its raw socket; the kernel
IPsec layer encrypts/authenticates on the wire per the Security Policy Database (SPD).

- **Mode:** transport (host-to-host), not tunnel. Preserves the original IP header, so
  the daemon's **TTL=255** requirement is unaffected (TTL travels in the outer/only
  header; ESP encrypts payload, AH would exclude mutable TTL from its ICV).
- **Protocol:** **ESP** (encrypt+authenticate) recommended; AH (auth-only) acceptable.
  **PSK** = the operator "passcode".
- **Selector:** local `192.168.1.1` ↔ remote `192.168.1.3`, protocol `112` (VRRP). Can
  broaden to all traffic between the hosts, but scoping to proto 112 is tightest.
- **OPNsense side:** strongSwan. A host-to-host transport connection with PSK and a
  child SA selecting proto 112. The stock IPsec UI is tunnel-oriented; a transport-mode
  single-protocol SA may need a `swanctl.conf` / manual connection — document as advanced.
- **Peer side (OpenWRT):** matching strongSwan/swanctl (or racoon) transport SA, same
  PSK, mirror selectors.
- **Failure mode:** if the SA is down, adverts are dropped by the kernel → both nodes
  eventually go Master (split brain) or both Backup depending on policy. Use IPsec policy
  `require` (not `use`) so unprotected VRRP is never accepted, and monitor SA health.
- **This is config + docs, not daemon code.** The plugin may optionally surface an IPsec
  status hint later, but IPsec itself is managed by OPNsense's existing IPsec subsystem.

Example (illustrative strongSwan `swanctl.conf`, OPNsense host `192.168.1.1`):

```
connections {
  vrrp-ha {
    local_addrs  = 192.168.1.1
    remote_addrs = 192.168.1.3
    local  { auth = psk }
    remote { auth = psk }
    children {
      vrrp {
        mode = transport
        local_ts  = 192.168.1.1[112]     # VRRP = IP proto 112
        remote_ts = 192.168.1.3[112]
        esp_proposals = aes256gcm16
        start_action = trap
      }
    }
    version = 2                          # IKEv2
  }
}
secrets { ike-vrrp { secret = "<PASSCODE / PSK>" } }
```

## Risks / correctness hazards

- **Checksum**: VRRPv3 pseudo-header inclusion is the #1 interop risk. Validate against
  a captured real-peer advert (tcpdump) before trusting the FSM.
- **TTL=255**: enforce on send AND drop-on-recv; keepalived rejects non-255.
- **FreeBSD `IP_HDRINCL` byte order**: `ip_len`/`ip_off` historically host-order on BSD
  raw sends — verify on the target OPNsense 26.1.
- **Kernel CARP**: FreeBSD has native CARP (different protocol); confirm no conflict on
  proto 112 raw socket. No `use_vmac` needed (peer uses real MAC).
- **Gratuitous ARP** via BPF: needs `/dev/bpf` access + correct Ethernet ARP frame;
  without it, LAN takeover is slow (stale ARP caches).
- **Multi-instance timing**: N instances × per-instance timers in one poll loop; keep the
  loop cheap, compute deadlines not per-tick scans.
- **IPsec × raw socket**: confirm the kernel applies IPsec SPD to raw `IP_HDRINCL` sends
  on FreeBSD (output policy lookup in `ip_output`) — else outbound VRRP would bypass
  IPsec. Use policy `require` so inbound unprotected VRRP is dropped (no auth bypass).
- **IPsec SA down = no adverts**: dropped adverts → failover/split-brain per policy;
  monitor SA health, prefer `require` over `use`.
- **Config migration**: hard break — existing installs must rewrite their config; ship a
  clear error + migration note in RELEASE.md.
- **Status file staleness** when daemon dead: `written` epoch + `|| echo '{}'` cover it.
- **IPv4 only**: reject/ignore IPv6 VIPs for now.

## TO DO — phased build order

Each phase independently buildable + testable. One task at a time; each file write
< ~150 lines (split large files across passes). Test against the real peer
(`192.168.1.3`) where noted. Check items off as completed.

### Phase 0 — Scaffolding
- [x] Add `VRRP_PROTO` (112), `VRRP_VERSION3` (3), `VRRP_MCAST_ADDR` (224.0.0.18), priority/TTL consts to new `include/vrrp.h`
- [x] Define `vip_t`, `vrrp_state_t`, on-wire `vrrp_hdr_t`, decoded `vrrp_advert_t` in `vrrp.h`; declare codec prototypes
- [~] `vrrp_instance_t` → deferred to `config.h` (Phase 3), `vrrp_rt_t` → `state.h` (Phase 4): both need `dhcp_backend_t`, kept there to avoid an include cycle (`vrrp.h` stays system-headers-only)
- [~] `Makefile` object list: wired incrementally as each `.c` lands (Phases 1/2/4/7) — dropping `heartbeat.o` before `state.c` is rewritten would break the build
- [~] Version bump / `RELEASE.md`: deferred to Phase 10 (no in-C version constant; `RELEASE.md ## Changes` is filled at release time per CLAUDE.md)
- [x] `vrrp.h` syntax-checked on Linux with project CFLAGS (`-D_BSD_SOURCE`)

### Phase 1 — VRRPv3 wire codec (`vrrp.c`) — pure, unit-testable
- [x] `vrrp_advert_encode()` — pack ver/type/vrid/priority/count/max_adver/VIP list
- [x] `vrrp_checksum()` — internet checksum over IPv4 pseudo-header + message
- [x] `vrrp_advert_decode()` — validate ver=3/type=1, checksum, length; extract fields
- [x] Test harness `tests/test_vrrp.c` + `make check` — 16 checks: round-trip, checksum
      self-verify, all reject paths. Wired `src/vrrp.c` into Makefile.
- [ ] **Verify checksum against a tcpdump capture from the real peer** — deferred to
      Phase 6 (needs the live peer; self-consistency proven, pseudo-header algorithm per
      RFC 5798 §5.1.1.4 still to be confirmed on the wire)

### Phase 2 — Raw socket transport (`net.c`) [FreeBSD]
- [ ] `net_open()` — `SOCK_RAW` proto 112, `IP_HDRINCL`, bind src, TTL=255
- [ ] `net_send()` — unicast `sendto` peer, verify TTL/hdr byte order on OPNsense box
- [ ] `net_recv()` — read incl. IP header, extract src + `ip_ttl`, **drop if != 255**
- [ ] Test: send adverts for one VRID; confirm the **peer logs seeing us** (send-only)

### Phase 3 — Config parser (`config.c` rewrite)
- [ ] Parse `[vrrp_instance NAME]` sections + all keys; repeatable `vip … dev …`
- [ ] Resolve per-instance priority (fallback to `[global] priority`)
- [ ] Validate (unique vrid, ranges, required src/peer); reject old `[iface]` w/ migration error
- [ ] Ship `keepalived-bsd.conf.example` with all 4 peer instances (placeholder ifaces)
- [ ] Test: load example, dump parsed instances

### Phase 4 — Per-VRID FSM (`state.c` rewrite)
- [ ] Per-instance timers: `Skew_Time`, `Master_Down_Interval` (centiseconds)
- [ ] Init → Backup/Master; Backup Master_Down → Master; Master recv-higher → Backup
- [ ] priority-0 (resign) handling on recv; equal-priority src-IP tiebreak
- [ ] Poll loop drives all instances off deadlines (no per-tick scan)
- [ ] Test: one instance, no peer → goes Master after Master_Down_Interval

### Phase 5 — Transition side-effects
- [ ] Hook `iface.c` VIP add/del per instance (VIP on its own `dev`)
- [ ] Hook `dhcp.c` enable/disable per instance transition
- [ ] `arp.c`: gratuitous ARP via BPF on becoming Master (one gARP per VIP)
- [ ] Shutdown: send priority-0 advert per Master instance, then release
- [ ] Test: Master transition adds VIP + gARP visible on wire

### Phase 6 — Transport IPsec + integration vs real peer (`192.168.1.3`)
- [ ] Bring up strongSwan transport SA `192.168.1.1↔192.168.1.3`, proto 112, PSK (§6)
- [ ] Set IPsec policy to `require`; confirm unprotected VRRP is dropped
- [ ] Verify outbound raw `IP_HDRINCL` VRRP is IPsec-protected on the wire (tcpdump: ESP)
- [ ] Confirm daemon still sees plaintext VRRP (kernel decrypts before raw socket) + TTL=255
- [ ] Full failover test: our priority 110 vs peer 100 → we win MASTER, peer BACKUP
- [ ] Kill our daemon → peer takes over (we sent resign) within Master_Down
- [ ] Load-share test: MASTER on one VRID, BACKUP on another simultaneously
- [ ] Verify no checksum/TTL rejects in peer keepalived logs

### Phase 7 — Status file (`status.c`)
- [ ] `status_write()` — build per-instance JSON, temp + `fsync` + `rename()`
- [ ] Include `written` epoch + all 8 columns' fields
- [ ] Call on every transition + gated once/second in poll loop
- [ ] Test: `cat /var/run/keepalived_bsd.status | python3 -m json.tool`

### Phase 8 — OPNsense API plumbing
- [ ] `actions_keepalived.conf`: add `[status_detail]` (`cat … || echo '{}'`)
- [ ] `ServiceController::statusDetailAction()` → JSON array
- [ ] `service configd restart`; test `GET /api/keepalived/service/statusDetail`

### Phase 9 — OPNsense model + UI
- [ ] `Keepalived.xml`: `vrrp_instance` grid model (replaces global+iface fields)
- [ ] `SettingsController`: `addInstance`/`delInstance`; config-template generator
- [ ] `index.volt`: instance grid + add/edit modal + VIP sub-list
- [ ] `index.volt`: VRRP Status table (8 cols) + `loadVrrpStatus()` 2s poller + stale flag
- [ ] Test in browser: create instance, apply, status table populates/refreshes

### Phase 10 — Docs & release
- [ ] Update `CLAUDE.md` (protocol, config model, file layout)
- [ ] Update `README`/config docs; migration note (old config breaks)
- [ ] Document transport-IPsec setup (§6): OPNsense + peer strongSwan, PSK, proto-112 SA
- [ ] `RELEASE.md`: changelog entries for the range
- [ ] Verify `make install-all` clean on OPNsense 26.1

## Open items to verify during build (not from memory)
- [ ] VRRPv3 checksum exact algorithm — confirm vs RFC 5798 §5.1.1.4 + real capture
- [ ] FreeBSD `IP_HDRINCL` `ip_len`/`ip_off` byte order on OPNsense 26.1
- [ ] Raw proto-112 recv: header-inclusion + TTL read path on FreeBSD
- [ ] Gratuitous ARP frame format via `/dev/bpf`
- [ ] Native CARP non-conflict on proto 112
- [ ] IPsec SPD applies to raw `IP_HDRINCL` output on FreeBSD/OPNsense 26.1
- [ ] strongSwan transport-mode single-protocol (proto 112) SA config on OPNsense




