#ifndef DHCP_H
#define DHCP_H

#include "config.h"

/* dhcp_backend_t defined in config.h */
dhcp_backend_t dhcp_backend_parse(const char *str);
const char    *dhcp_backend_name(dhcp_backend_t b);

/* Per-interface enable/disable — each backend modifies its own config + reloads */
int dhcp_enable_iface(const iface_cfg_t *iface, dhcp_backend_t backend);
int dhcp_disable_iface(const iface_cfg_t *iface, dhcp_backend_t backend);

#endif /* DHCP_H */
