#ifndef NET_H
#define NET_H

#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Raw IPv4 transport for VRRP (IP protocol 112).
 *
 * Send uses a plain raw socket with IP_TTL=255 and bind() to the source
 * address, letting the kernel build the IP header. This deliberately avoids
 * IP_HDRINCL and the BSD ip_len/ip_off host-byte-order quirk. Receive parses
 * the IP header the kernel includes on raw IPv4 input to recover the source
 * address and TTL.
 */

/* Open a VRRP raw socket bound to src_ip, TTL forced to 255, non-blocking.
 * Returns the fd, or -1 on error. */
int net_vrrp_open(struct in_addr src_ip);

/* Send a fully-built VRRP message unicast to dst. Source address is the one
 * the socket was bound to. Returns 0 on success, -1 on error. */
int net_vrrp_send(int sock, struct in_addr dst,
                  const uint8_t *msg, size_t len);

/*
 * Read one datagram. Return values:
 *   >0  valid VRRP payload length copied into buf; *src and *ttl are set
 *    0  a datagram was read but rejected (too short / TTL != 255) — the caller
 *       should keep draining
 *   -1  no datagram available (would block) or a socket error — stop draining
 */
int net_vrrp_recv(int sock, uint8_t *buf, size_t buflen,
                  struct in_addr *src, int *ttl);

#endif /* NET_H */
