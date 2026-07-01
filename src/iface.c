#include <errno.h>
#include <stdio.h>
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
    if (prefix == 0)  return 0;
    if (prefix >= 32) return 0xffffffffu;          /* /32: all-ones, byte-order invariant */
    return htonl(~((1u << (32 - prefix)) - 1));     /* 1..31: safe shift range */
}

/* "a.b.c.d/p" for logging */
static void vip_str(const vip_t *vip, char *out, size_t n)
{
    char a[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &vip->addr, a, sizeof(a)))
        snprintf(a, sizeof(a), "?");
    snprintf(out, n, "%s/%u", a, vip->prefix_len);
}

int iface_vip_add(const vip_t *vip)
{
    struct in_aliasreq ifra;
    struct sockaddr_in *sin;
    char str[64];
    int s, ret = 0;

    s = ctl_sock();
    if (s < 0) return -1;

    memset(&ifra, 0, sizeof(ifra));
    strncpy(ifra.ifra_name, vip->dev, IFNAMSIZ - 1);

    sin = (struct sockaddr_in *)&ifra.ifra_addr;
    sin->sin_family = AF_INET;
    sin->sin_len    = sizeof(*sin);
    sin->sin_addr   = vip->addr;

    sin = (struct sockaddr_in *)&ifra.ifra_mask;
    sin->sin_family = AF_INET;
    sin->sin_len    = sizeof(*sin);
    sin->sin_addr.s_addr = prefix_to_mask(vip->prefix_len);

    sin = (struct sockaddr_in *)&ifra.ifra_broadaddr;
    sin->sin_family = AF_INET;
    sin->sin_len    = sizeof(*sin);
    /* broadcast = addr | ~mask */
    sin->sin_addr.s_addr = vip->addr.s_addr | ~prefix_to_mask(vip->prefix_len);

    vip_str(vip, str, sizeof(str));
    if (ioctl(s, SIOCAIFADDR, &ifra) < 0) {
        log_err("iface_vip_add %s %s: %s", vip->dev, str, strerror(errno));
        ret = -1;
    } else {
        log_info("iface: added VIP %s on %s", str, vip->dev);
    }

    close(s);
    return ret;
}

int iface_vip_del(const vip_t *vip)
{
    struct ifreq ifr;
    struct sockaddr_in *sin;
    char str[64];
    int s, ret = 0;

    s = ctl_sock();
    if (s < 0) return -1;

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, vip->dev, IFNAMSIZ - 1);
    sin = (struct sockaddr_in *)&ifr.ifr_addr;
    sin->sin_family = AF_INET;
    sin->sin_len    = sizeof(*sin);
    sin->sin_addr   = vip->addr;

    vip_str(vip, str, sizeof(str));
    if (ioctl(s, SIOCDIFADDR, &ifr) < 0) {
        log_err("iface_vip_del %s %s: %s", vip->dev, str, strerror(errno));
        ret = -1;
    } else {
        log_info("iface: removed VIP %s from %s", str, vip->dev);
    }

    close(s);
    return ret;
}
