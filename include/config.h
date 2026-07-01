#ifndef CONFIG_H
#define CONFIG_H

#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include "vrrp.h"   /* vip_t, vrrp_state_t */

/* DHCP backend selector; INHERIT means an instance uses the global default. */
typedef enum {
    DHCP_BACKEND_ISC     = 0,
    DHCP_BACKEND_KEA     = 1,
    DHCP_BACKEND_DNSMASQ = 2,
    DHCP_BACKEND_NONE    = 3,
    DHCP_BACKEND_INHERIT = 255,
} dhcp_backend_t;

#define MAX_INSTANCES 8
#define MAX_IFACES    8   /* legacy; see iface_cfg_t */

/*
 * Legacy per-interface config from the KALV era. Retained only because dhcp.h
 * and iface.c still reference the type; both are rehooked to per-instance VIPs
 * in Phase 5, after which this struct is removed.
 */
typedef struct {
    char           iface[IFNAMSIZ];
    char           vip_str[64];
    struct in_addr vip_addr;
    uint8_t        prefix_len;
    dhcp_backend_t dhcp_backend;
    char           alias_name[64];
} iface_cfg_t;

/* One VRRPv3 instance == one keepalived vrrp_instance block. */
typedef struct {
    char           name[VRRP_NAME_LEN];  /* [vrrp_instance NAME]        */
    uint8_t        vrid;                 /* virtual_router_id 1..255    */
    uint8_t        priority;             /* 0 = unset -> global default */
    vrrp_state_t   initial;              /* configured initial state    */
    char           adv_if[IFNAMSIZ];     /* advertisement interface     */
    char           src_str[64];
    char           peer_str[64];
    struct in_addr src_ip;               /* unicast_src_ip              */
    struct in_addr peer_ip;              /* unicast_peer                */
    uint16_t       adver_cs;             /* advert interval, centiseconds */
    int            preempt;
    vip_t          vips[VRRP_MAX_VIPS];
    int            vip_count;
    dhcp_backend_t dhcp_backend;         /* INHERIT -> global default   */
    char           alias_name[64];       /* OPNsense firewall alias; empty = skip */
} vrrp_instance_t;

typedef struct {
    uint8_t         def_priority;        /* [global] priority fallback     */
    dhcp_backend_t  def_dhcp_backend;    /* [global] dhcp_backend fallback */
    vrrp_instance_t inst[MAX_INSTANCES];
    int             inst_count;
} config_t;

int  config_load(const char *path, config_t *cfg);
void config_dump(const config_t *cfg);

#endif /* CONFIG_H */
