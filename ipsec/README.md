# Phase 6 — Transport IPsec + integration test runbook

On-box procedure for securing the VRRPv3 link and validating failover against
the real keepalived peer. Run on the OPNSense box (`192.168.1.1`) and the
OpenWRT/Linux peer (`192.168.1.3`). Templates: `swanctl-opnsense.conf`,
`swanctl-openwrt.conf`. Background: `PLAN.md` §6, README "Security".

VRRPv3 has **no in-packet authentication** — the IPsec SA is the only thing
authenticating the advertisement link. Use policy **`require`** so unprotected
VRRP is dropped (no auth bypass).

## 0. Prereqs

- Daemon built and installed on the OPNSense box (`make install-all`).
- Peer keepalived already running (VRID 10/66/13/20, `advert_int 5`, v3, unicast).
- A shared PSK on both hosts: `openssl rand -base64 32` → paste into both
  `secret = ...` lines.

## 1. Bring up the transport SA  (checklist §1–2)

**OPNSense (192.168.1.1):**
```sh
cp ipsec/swanctl-opnsense.conf /usr/local/etc/swanctl/conf.d/vrrp-ha.conf
# edit the PSK, then:
swanctl --load-all
swanctl --list-sas          # expect vrrp-ha ESTABLISHED, child 'vrrp' INSTALLED
```

**Peer (192.168.1.3):**
```sh
cp ipsec/swanctl-openwrt.conf /etc/swanctl/conf.d/vrrp-ha.conf
# edit the same PSK, then:
swanctl --load-all
swanctl --list-sas
```

Confirm the installed policy is transport + proto 112:
```sh
swanctl --list-conns        # child vrrp: local/remote ts 192.168.1.x/32[112]
setkey -DP                  # (FreeBSD) SPD: 192.168.1.1[any] 192.168.1.3[any] 112 ipsec esp/transport ... require
```
The `require` level means matching VRRP that arrives **without** ESP is dropped.

## 2. Verify VRRP is encrypted on the wire  (checklist §3)

Start the daemon (foreground so you see FSM logs):
```sh
./keepalived-bsd -c /usr/local/etc/keepalived-bsd.conf -f
```
In another shell, capture the external interface:
```sh
tcpdump -ni igb0 host 192.168.1.3 and \( esp or proto 112 \)
```
Expected: **ESP** packets only, **no** cleartext `proto 112` / `VRRPv3` frames
leaving the box. If you see cleartext proto-112, the SPD is not matching —
recheck `local_ts/remote_ts` proto `[112]`.

## 3. Confirm the daemon still sees plaintext + TTL=255  (checklist §4)

The kernel decrypts ESP *before* the raw proto-112 socket, so the daemon reads
plaintext VRRP. Confirm in the foreground log:
- adverts received from `192.168.1.3` are decoded (no checksum/version reject),
- no `TTL != 255` drops logged (net.c drops non-255).

Cross-check checksum on the wire (decrypted view) — capture on the peer's LAN
bridge or use `tcpdump -v` where the frame is cleartext and confirm
`VRRPv3 ... vrid N ... prio P` parses with a valid checksum. This closes the
**Phase 1 open item**: VRRPv3 pseudo-header checksum verified against a real
capture (PLAN Phase 1 last box, and "Open items" line 1).

## 4. SA-down behavior  (checklist §2, split-brain guard)

Tear the SA down on the peer and watch the OPNSense daemon:
```sh
# on peer:
swanctl --terminate --ike vrrp-ha
```
With policy `require`, adverts are now dropped by the kernel. Expected: OPNSense
instances stop receiving adverts → after `Master_Down_Interval` they promote to
Master (documented split-brain risk — monitor SA health in production). Restore:
```sh
swanctl --initiate --child vrrp        # or --load-all
```

## 5. Full failover: we win MASTER  (checklist §5)

Set our `master` instance (VRID 10) `priority = 110` (> peer's 100) with
`preempt` on, keep advert_int 5. Start the daemon.
- Expected: within ~1 advert we send higher-priority adverts; the peer keepalived
  moves VRID 10 to BACKUP; we go **MASTER** → VIP `192.165.1.2/24` added on our
  `dev`, gratuitous ARP sent, DHCP enabled for that iface.

Verify:
```sh
ifconfig igb0 | grep 192.165.1.2         # VIP present on MASTER
tcpdump -ni igb0 arp and host 192.165.1.2   # gARP burst on transition
cat /var/run/keepalived_bsd.status | python3 -m json.tool   # state=MASTER
```
On the peer: `cat /tmp/keepalived.data` (or its log) shows VRID 10 = BACKUP.

## 6. Kill our daemon → peer takes over  (checklist §6)

```sh
kill -TERM $(cat /var/run/keepalived-bsd.pid)
```
- Expected: on SIGTERM a MASTER instance sends a **priority-0 resign** advert,
  then releases the VIP / disables DHCP. The peer sees priority 0 → promotes to
  MASTER immediately (well under `Master_Down_Interval`).

Verify on the peer it now owns `192.165.1.2`; on our box the VIP is gone.

## 7. Load-share: MASTER on one VRID, BACKUP on another  (checklist §7)

Configure asymmetric priorities: e.g. VRID 10 (`master`) priority **110** (we
win), VRID 66 (`VI_666`) priority **90** (peer wins). Start both instances.
- Expected: we are **MASTER** for VRID 10 and **BACKUP** for VRID 66
  simultaneously — independent per-VRID FSMs, load sharing confirmed.

```sh
cat /var/run/keepalived_bsd.status | python3 -m json.tool
# instance master → MASTER, instance VI_666 → BACKUP
```

## 8. No checksum / TTL rejects in the peer log  (checklist §8)

On the peer keepalived, run at `--log-detail`/debug and confirm **zero**:
- `invalid checksum` / `ip checksum` errors on our adverts,
- `TTL` / `ip ttl` rejects,
- `receive advertisement of unknown version` / bad-version drops.

Clean logs on both sides = interop confirmed. Tick PLAN Phase 6, Phase 1's
checksum box, Phase 2's on-box send box, and the relevant "Open items".

## Checklist mapping → PLAN.md

| Runbook step | PLAN Phase 6 item |
|---|---|
| 1 | SA up; policy `require` drops unprotected VRRP |
| 2 | Outbound raw VRRP is ESP on the wire |
| 3 | Daemon sees plaintext + TTL=255; checksum vs real capture (Phase 1) |
| 4 | SA-down = drops → failover/split-brain per policy |
| 5 | priority 110 vs 100 → we win MASTER |
| 6 | Kill daemon → peer takes over (resign) |
| 7 | Load-share across VRIDs |
| 8 | No checksum/TTL rejects in peer log |
