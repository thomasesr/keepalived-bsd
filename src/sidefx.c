/*
 * Master/Backup transition side-effects for a single VRRP instance.
 *
 * Master : add every VIP on its host interface, gratuitous-ARP it, add it to
 *          the firewall alias, then enable DHCP on each interface serving a VIP.
 * Backup : mirror image, in reverse order (stop serving before releasing IPs).
 *
 * DHCP is toggled once per distinct VIP interface (dnsmasq/isc are per-iface;
 * kea's helper is whole-service and no-ops the duplicate).
 */

#include <errno.h>
#include <string.h>
#include <sys/param.h>
#include <sys/linker.h>

#include "sidefx.h"
#include "iface.h"
#include "dhcp.h"
#include "arp.h"
#include "alias.h"
#include "logger.h"

void sidefx_carp_guard(void)
{
    static int warned_inuse = 0;   /* throttle the "in use" warning to once */
    int fileid;

    fileid = kldfind("carp.ko");
    if (fileid < 0) {              /* not loaded — nothing to do */
        warned_inuse = 0;
        return;
    }
    if (kldunload(fileid) == 0) {
        log_warn("carp.ko unloaded: CARP and VRRP share IP proto 112, so the "
                 "kernel CARP handler would swallow all inbound adverts");
        warned_inuse = 0;
    } else if (!warned_inuse) {
        log_warn("carp.ko is loaded and in use (%s); remove CARP virtual IPs — "
                 "VRRP receive is blocked until then", strerror(errno));
        warned_inuse = 1;
    }
}

/* Enable or disable DHCP on each unique interface hosting one of the VIPs. */
static void dhcp_toggle_all(const vrrp_instance_t *in, int enable)
{
    char seen[VRRP_MAX_VIPS][IFNAMSIZ];
    int  nseen = 0, i, j, dup;

    if (in->dhcp_backend == DHCP_BACKEND_NONE)
        return;

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < in->vip_count; i++) {
        const char *dev = in->vips[i].dev;
        for (dup = 0, j = 0; j < nseen; j++)
            if (strcmp(seen[j], dev) == 0) { dup = 1; break; }
        if (dup)
            continue;
        strncpy(seen[nseen++], dev, IFNAMSIZ - 1);
        if (enable)
            dhcp_enable_iface(dev, in->dhcp_backend);
        else
            dhcp_disable_iface(dev, in->dhcp_backend);
    }
}

void sidefx_enter_master(const vrrp_instance_t *in)
{
    int i;

    log_info("vrrp: [%s] vrid %u -> MASTER", in->name, in->vrid);

    for (i = 0; i < in->vip_count; i++) {
        const vip_t *v = &in->vips[i];
        iface_vip_add(v);
        arp_send_gratuitous(v->dev, v->addr);
        alias_add_vip(in->alias_name, v->addr);
    }
    dhcp_toggle_all(in, 1);
}

void sidefx_enter_backup(const vrrp_instance_t *in)
{
    int i;

    log_info("vrrp: [%s] vrid %u -> BACKUP", in->name, in->vrid);

    dhcp_toggle_all(in, 0);
    for (i = 0; i < in->vip_count; i++) {
        const vip_t *v = &in->vips[i];
        alias_del_vip(in->alias_name, v->addr);
        iface_vip_del(v);
    }
}
