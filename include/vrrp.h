#ifndef VRRP_H
#define VRRP_H

/*
 * VRRPv3 (RFC 5798) wire definitions and shared data types.
 *
 * This is the base layer of the daemon: it pulls in only system headers so
 * that config.h and state.h can include it without creating a circular
 * dependency. This replaced the daemon's original custom "KALV" UDP protocol
 * with real VRRPv3 unicast; see PLAN.md.
 */

#include <net/if.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

/* IP protocol number carrying VRRP; not always defined in system headers. */
#ifndef IPPROTO_VRRP
#define IPPROTO_VRRP 112
#endif
#define VRRP_PROTO IPPROTO_VRRP

#define VRRP_VERSION3    3            /* we implement VRRP version 3 only     */
#define VRRP_TYPE_ADVERT 1            /* the only defined VRRP packet type    */
#define VRRP_MCAST_ADDR  "224.0.0.18" /* VRRP multicast group (unused in       *
                                       * pure-unicast operation)              */
#define VRRP_TTL         255          /* MUST be 255 on send; drop otherwise  */

/* Priority special values (RFC 5798 s5.2.4). */
#define VRRP_PRIO_STOP    0           /* a master resigning                   */
#define VRRP_PRIO_OWNER   255         /* owns the address                     */
#define VRRP_PRIO_DEFAULT 100

#define VRRP_MAX_VIPS  16            /* max virtual IPs per instance          */
#define VRRP_HDR_LEN   8             /* fixed advert header, IPv4, no addrs   */
#define VRRP_NAME_LEN  32

/* Pack version and type into / out of the first advert byte. */
#define VRRP_VT(ver, type) (uint8_t)(((ver) << 4) | ((type) & 0x0f))
#define VRRP_VT_VER(b)     (uint8_t)((b) >> 4)
#define VRRP_VT_TYPE(b)    (uint8_t)((b) & 0x0f)

/* Max Adver Int occupies the low 12 bits of a 16-bit field (centiseconds). */
#define VRRP_MAXADV_MASK 0x0fffu

/* Per-instance state (RFC 5798 s6.3). Distinct from the legacy 2-value
 * node_state_t in state.h, which is retired when state.c is rewritten. */
typedef enum {
    VRRP_STATE_INIT   = 0,
    VRRP_STATE_BACKUP = 1,
    VRRP_STATE_MASTER = 2
} vrrp_state_t;

/* On-wire VRRPv3 advertisement header (IPv4), followed by count_ip IPv4
 * addresses (4 bytes each). All multi-byte fields are network byte order. */
typedef struct __attribute__((packed)) {
    uint8_t  ver_type;   /* (version << 4) | type ; 0x31 = v3 advert */
    uint8_t  vrid;       /* virtual router id, 1..255                */
    uint8_t  priority;   /* 0 = resign, 255 = owner, else 1..254     */
    uint8_t  count_ip;   /* number of IPv4 addresses that follow     */
    uint16_t max_adver;  /* low 12 bits = advertisement interval, cs */
    uint16_t checksum;   /* internet checksum incl. IPv4 pseudo-hdr  */
} vrrp_hdr_t;

/* A decoded advertisement (host byte order), plus the sender address the
 * receive path pulls from the IP header for the priority tiebreak. */
typedef struct {
    uint8_t        vrid;
    uint8_t        priority;
    uint16_t       adver_cs;                 /* centiseconds */
    struct in_addr vips[VRRP_MAX_VIPS];
    int            vip_count;
    struct in_addr src;                      /* sender's source IP */
} vrrp_advert_t;

/* A virtual IP managed by an instance. May live on a different interface
 * than the instance's advert interface (keepalived "dev"). */
typedef struct {
    struct in_addr addr;
    uint8_t        prefix_len;
    char           dev[IFNAMSIZ];  /* host interface; empty = instance adv_if */
    char           label[64];      /* optional keepalived-style label; cosmetic */
} vip_t;

/*
 * Wire codec (implemented in vrrp.c, Phase 1).
 * vrrp_checksum() computes the RFC 5798 IPv4 checksum, which unlike VRRPv2
 * covers the IPv4 pseudo-header (src, dst, zero, proto, length) plus msg.
 */
uint16_t vrrp_checksum(const void *msg, size_t msg_len,
                       struct in_addr src, struct in_addr dst);
int vrrp_advert_encode(uint8_t *buf, size_t buflen, const vrrp_advert_t *adv,
                       struct in_addr src, struct in_addr dst);
/* src/dst come from the received IP header and are needed to verify the
 * pseudo-header checksum; out->src is set to src on success. */
int vrrp_advert_decode(const uint8_t *buf, size_t len,
                       struct in_addr src, struct in_addr dst,
                       vrrp_advert_t *out);

#endif /* VRRP_H */
