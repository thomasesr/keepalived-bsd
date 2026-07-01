#ifndef DHCP_H
#define DHCP_H

#include "config.h"   /* dhcp_backend_t */

dhcp_backend_t dhcp_backend_parse(const char *str);
const char    *dhcp_backend_name(dhcp_backend_t b);

/* Per-interface enable/disable — each backend modifies its own config + reloads.
 * iface is a bare interface name (e.g. "igb0"); validated before exec. */
int dhcp_enable_iface(const char *iface, dhcp_backend_t backend);
int dhcp_disable_iface(const char *iface, dhcp_backend_t backend);

#endif /* DHCP_H */
