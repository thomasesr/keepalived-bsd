#ifndef CONFIG_H
#define CONFIG_H

#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>

/* forward-declared here; defined in dhcp.h to avoid circular include */
typedef enum {
    DHCP_BACKEND_ISC     = 0,
    DHCP_BACKEND_KEA     = 1,
    DHCP_BACKEND_DNSMASQ = 2,
    DHCP_BACKEND_NONE    = 3,
    DHCP_BACKEND_INHERIT = 255, /* iface-level: inherit global setting */
} dhcp_backend_t;

#define MAX_IFACES 8

typedef struct {
    char           iface[IFNAMSIZ];
    char           vip_str[64];     /* "x.x.x.x/prefix" */
    struct in_addr vip_addr;
    uint8_t        prefix_len;
    dhcp_backend_t dhcp_backend;    /* per-iface override; INHERIT = use global */
    char           alias_name[64];  /* OPNsense firewall alias to update; empty = skip */
} iface_cfg_t;

typedef struct {
    char           peer_str[64];
    struct in_addr peer_addr;
    uint16_t       port;
    uint8_t        priority;
    int            heartbeat_sec;
    int            timeout_sec;
    int            preempt;
    dhcp_backend_t dhcp_backend;
    iface_cfg_t    ifaces[MAX_IFACES];
    int            iface_count;
} config_t;

int config_load(const char *path, config_t *cfg);
void config_dump(const config_t *cfg);

#endif /* CONFIG_H */
