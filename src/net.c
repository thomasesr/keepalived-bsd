/*
 * Raw IPv4 transport for VRRPv3 (IP protocol 112). See net.h for the design
 * rationale (no IP_HDRINCL; kernel builds the IP header, TTL forced to 255).
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include "net.h"
#include "vrrp.h"
#include "logger.h"

int net_vrrp_open(struct in_addr src_ip)
{
    struct sockaddr_in sa;
    int s, ttl, fl;

    s = socket(AF_INET, SOCK_RAW, VRRP_PROTO);
    if (s < 0) {
        log_err("vrrp: raw socket: %s", strerror(errno));
        return -1;
    }

    ttl = VRRP_TTL;
    if (setsockopt(s, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
        log_err("vrrp: set IP_TTL: %s", strerror(errno));
        close(s);
        return -1;
    }

    /* Bind to the source address: forces outbound source = unicast_src_ip and
     * limits inbound to datagrams addressed to us. */
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr = src_ip;
    if (bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        log_err("vrrp: bind to %s: %s", inet_ntoa(src_ip), strerror(errno));
        close(s);
        return -1;
    }

    fl = fcntl(s, F_GETFL, 0);
    if (fl >= 0)
        (void)fcntl(s, F_SETFL, fl | O_NONBLOCK);

    return s;
}

int net_vrrp_send(int sock, struct in_addr dst, const uint8_t *msg, size_t len)
{
    struct sockaddr_in sa;
    ssize_t n;

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr = dst;

    n = sendto(sock, msg, len, 0, (struct sockaddr *)&sa, sizeof(sa));
    if (n < 0) {
        log_warn("vrrp: sendto %s: %s", inet_ntoa(dst), strerror(errno));
        return -1;
    }
    return 0;
}

int net_vrrp_recv(int sock, uint8_t *buf, size_t buflen,
                  struct in_addr *src, int *ttl)
{
    uint8_t pkt[256];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    const struct ip *iph;
    size_t hl, payload;
    ssize_t n;
    int rttl;

    n = recvfrom(sock, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &fromlen);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return -1;
        log_warn("vrrp: recvfrom: %s", strerror(errno));
        return -1;
    }

    /* Raw IPv4 sockets deliver the IP header inline on input. */
    if ((size_t)n < sizeof(struct ip))
        return 0;
    iph = (const struct ip *)pkt;
    hl = (size_t)iph->ip_hl * 4;
    if (hl < sizeof(struct ip) || (size_t)n < hl)
        return 0;
    if (iph->ip_p != VRRP_PROTO)
        return 0;

    rttl = iph->ip_ttl;
    if (ttl)
        *ttl = rttl;
    if (src)
        *src = iph->ip_src;

    /* RFC 5798: a received advertisement MUST have TTL 255. */
    if (rttl != VRRP_TTL) {
        log_warn("vrrp: drop from %s: TTL %d != 255",
                 inet_ntoa(iph->ip_src), rttl);
        return 0;
    }

    payload = (size_t)n - hl;
    if (payload == 0 || payload > buflen)
        return 0;
    memcpy(buf, pkt + hl, payload);
    return (int)payload;
}
