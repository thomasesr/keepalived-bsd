/*
 * VRRPv3 (RFC 5798) advertisement encode/decode and checksum.
 *
 * IPv4 only. The advertisement is a fixed 8-byte header followed by the
 * virtual IPv4 addresses (4 bytes each), so a VRRP message is always an even
 * number of bytes. Unlike VRRPv2, the VRRPv3 checksum covers the IPv4
 * pseudo-header (RFC 5798 s5.1.1.4), so encode/decode take src and dst.
 */

#include <string.h>
#include <arpa/inet.h>
#include "vrrp.h"

/* Accumulate 16-bit big-endian words for the internet checksum. */
static uint32_t csum_add(uint32_t sum, const uint8_t *p, size_t len)
{
    size_t i;
    for (i = 0; i + 1 < len; i += 2)
        sum += (uint32_t)((p[i] << 8) | p[i + 1]);
    if (i < len)                          /* odd trailing byte (VRRP: never) */
        sum += (uint32_t)(p[i] << 8);
    return sum;
}

static uint16_t csum_fold(uint32_t sum)
{
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

/*
 * Returns the folded ~sum over the IPv4 pseudo-header plus the VRRP message.
 * When msg carries a correct checksum the result is 0; with the checksum
 * field zeroed the result is the value to store (host order; store htons()).
 */
uint16_t vrrp_checksum(const void *msg, size_t msg_len,
                       struct in_addr src, struct in_addr dst)
{
    uint8_t ph[12];
    uint32_t sum;

    memcpy(ph + 0, &src.s_addr, 4);       /* already network byte order */
    memcpy(ph + 4, &dst.s_addr, 4);
    ph[8]  = 0;
    ph[9]  = VRRP_PROTO;
    ph[10] = (uint8_t)(msg_len >> 8);
    ph[11] = (uint8_t)(msg_len & 0xff);

    sum = csum_add(0, ph, sizeof(ph));
    sum = csum_add(sum, (const uint8_t *)msg, msg_len);
    return csum_fold(sum);
}

int vrrp_advert_encode(uint8_t *buf, size_t buflen, const vrrp_advert_t *adv,
                       struct in_addr src, struct in_addr dst)
{
    vrrp_hdr_t *h = (vrrp_hdr_t *)buf;
    size_t msg_len;
    int i;

    if (adv->vip_count < 0 || adv->vip_count > VRRP_MAX_VIPS)
        return -1;
    msg_len = VRRP_HDR_LEN + (size_t)adv->vip_count * 4;
    if (buflen < msg_len)
        return -1;

    h->ver_type  = VRRP_VT(VRRP_VERSION3, VRRP_TYPE_ADVERT);
    h->vrid      = adv->vrid;
    h->priority  = adv->priority;
    h->count_ip  = (uint8_t)adv->vip_count;
    h->max_adver = htons(adv->adver_cs & VRRP_MAXADV_MASK);
    h->checksum  = 0;
    for (i = 0; i < adv->vip_count; i++)
        memcpy(buf + VRRP_HDR_LEN + i * 4, &adv->vips[i].s_addr, 4);

    h->checksum = htons(vrrp_checksum(buf, msg_len, src, dst));
    return (int)msg_len;
}

int vrrp_advert_decode(const uint8_t *buf, size_t len,
                       struct in_addr src, struct in_addr dst,
                       vrrp_advert_t *out)
{
    const vrrp_hdr_t *h = (const vrrp_hdr_t *)buf;
    size_t msg_len;
    int i;

    if (len < VRRP_HDR_LEN)
        return -1;
    if (VRRP_VT_VER(h->ver_type) != VRRP_VERSION3 ||
        VRRP_VT_TYPE(h->ver_type) != VRRP_TYPE_ADVERT)
        return -1;
    if (h->count_ip > VRRP_MAX_VIPS)
        return -1;
    msg_len = VRRP_HDR_LEN + (size_t)h->count_ip * 4;
    if (len < msg_len)
        return -1;
    if (vrrp_checksum(buf, msg_len, src, dst) != 0)
        return -1;

    memset(out, 0, sizeof(*out));
    out->vrid     = h->vrid;
    out->priority = h->priority;
    out->adver_cs = ntohs(h->max_adver) & VRRP_MAXADV_MASK;
    out->vip_count = h->count_ip;
    for (i = 0; i < out->vip_count; i++)
        memcpy(&out->vips[i].s_addr, buf + VRRP_HDR_LEN + i * 4, 4);
    out->src = src;
    return 0;
}
