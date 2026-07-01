#ifndef ARP_H
#define ARP_H

#include <netinet/in.h>

/*
 * Broadcast a gratuitous ARP for `ip` out of interface `ifname` via BPF, so
 * LAN peers refresh their ARP cache to our MAC the instant we become Master.
 * Called once per VIP on the Backup->Master transition. Returns 0 on success,
 * -1 on error (missing /dev/bpf access, unknown interface, write failure).
 */
int arp_send_gratuitous(const char *ifname, struct in_addr ip);

#endif /* ARP_H */
