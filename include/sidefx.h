#ifndef SIDEFX_H
#define SIDEFX_H

#include "config.h"   /* vrrp_instance_t */

/*
 * Transition side-effects for one instance. Orchestrates iface (VIP add/del),
 * dhcp (enable/disable), arp (gratuitous ARP), and alias helpers.
 *
 * Split out of state.c so the FSM stays free of FreeBSD-only I/O and remains
 * portable for the unit tests, which link their own stubs of these two.
 */
void sidefx_enter_master(const vrrp_instance_t *in);
void sidefx_enter_backup(const vrrp_instance_t *in);

/*
 * Unload the kernel CARP module if present. CARP and VRRP share IP proto 112;
 * carp.ko owns the proto-112 input handler and swallows every inbound advert
 * before it reaches our raw socket. Called at daemon start and periodically so
 * a mid-run carp.ko reload can't silently starve receive. No-op if not loaded.
 */
void sidefx_carp_guard(void);

#endif /* SIDEFX_H */
