#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/* FreeBSD alias address struct */
#include <netinet/in_var.h>

#include "iface.h"
#include "logger.h"

static int ctl_sock(void)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        log_err("iface: socket: %s", strerror(errno));
    return s;
}

static uint32_t prefix_to_mask(uint8_t prefix)
{
    return prefix ? htonl(~((1u << (32 - prefix)) - 1)) : 0;
}

int iface_vip_add(const iface_cfg_t *iface)
{
    struct in_aliasreq ifra;
    struct sockaddr_in *sin;
    int s, ret = 0;

    s = ctl_sock();
    if (s < 0) return -1;

    memset(&ifra, 0, sizeof(ifra));
    strncpy(ifra.ifra_name, iface->iface, IFNAMSIZ - 1);

    sin = (struct sockaddr_in *)&ifra.ifra_addr;
    sin->sin_family = AF_INET;
    sin->sin_len    = sizeof(*sin);
    sin->sin_addr   = iface->vip_addr;

    sin = (struct sockaddr_in *)&ifra.ifra_mask;
    sin->sin_family = AF_INET;
    sin->sin_len    = sizeof(*sin);
    sin->sin_addr.s_addr = prefix_to_mask(iface->prefix_len);

    sin = (struct sockaddr_in *)&ifra.ifra_broadaddr;
    sin->sin_family = AF_INET;
    sin->sin_len    = sizeof(*sin);
    /* broadcast = addr | ~mask */
    sin->sin_addr.s_addr = iface->vip_addr.s_addr |
                           ~prefix_to_mask(iface->prefix_len);

    if (ioctl(s, SIOCAIFADDR, &ifra) < 0) {
        log_err("iface_vip_add %s %s: %s",
                iface->iface, iface->vip_str, strerror(errno));
        ret = -1;
    } else {
        log_info("iface: added VIP %s on %s", iface->vip_str, iface->iface);
    }

    close(s);
    return ret;
}

int iface_vip_del(const iface_cfg_t *iface)
{
    struct ifreq ifr;
    struct sockaddr_in *sin;
    int s, ret = 0;

    s = ctl_sock();
    if (s < 0) return -1;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface->iface, IFNAMSIZ - 1);
    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_len    = sizeof(*sin);
    sin->sin_addr   = iface->vip_addr;

    if (ioctl(s, SIOCDIFADDR, &ifr) < 0) {
        log_err("iface_vip_del %s %s: %s",
                iface->iface, iface->vip_str, strerror(errno));
        ret = -1;
    } else {
        log_info("iface: removed VIP %s from %s", iface->vip_str, iface->iface);
    }

    close(s);
    return ret;
}
