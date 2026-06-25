#ifndef DHCP_H
#define DHCP_H

#include "config.h"

/* dhcp_backend_t is defined in config.h */
dhcp_backend_t dhcp_backend_parse(const char *str);
const char    *dhcp_backend_name(dhcp_backend_t b);

/* Start/stop the configured backend — called once per state transition */
int dhcp_enable(const config_t *cfg);
int dhcp_disable(const config_t *cfg);

#endif /* DHCP_H */
