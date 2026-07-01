/*
 * Gratuitous ARP via BPF (FreeBSD). On becoming Master an instance broadcasts
 * one gratuitous ARP request per VIP (sender = target = VIP, sender MAC = the
 * advertising interface's real MAC) so switches and hosts update their caches
 * immediately instead of waiting for stale entries to expire.
 *
 * FreeBSD-specific: /dev/bpf + BIOCSETIF for raw L2 output, AF_LINK sockaddr_dl
 * for the interface MAC. Not compiled by the portable unit tests.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include "arp.h"
#include "logger.h"

/* Fetch the link-layer (MAC) address of ifname into mac[6]. */
static int get_hwaddr(const char *ifname, uint8_t mac[ETHER_ADDR_LEN])
{
    struct ifaddrs *ifap, *ifa;
    int found = 0;

    if (getifaddrs(&ifap) != 0) {
        log_err("arp: getifaddrs: %s", strerror(errno));
        return -1;
    }
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_LINK &&
            strcmp(ifa->ifa_name, ifname) == 0) {
            struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
            if (sdl->sdl_alen == ETHER_ADDR_LEN) {
                memcpy(mac, LLADDR(sdl), ETHER_ADDR_LEN);
                found = 1;
                break;
            }
        }
    }
    freeifaddrs(ifap);
    if (!found)
        log_err("arp: no L2 address for interface %s", ifname);
    return found ? 0 : -1;
}

/* Open a BPF device and bind it to ifname for writing. */
static int open_bpf(const char *ifname)
{
    struct ifreq ifr;
    int fd, i;

    fd = open("/dev/bpf", O_WRONLY);      /* cloning device on modern FreeBSD */
    if (fd < 0) {
        char dev[16];
        for (i = 0; i < 32; i++) {
            snprintf(dev, sizeof(dev), "/dev/bpf%d", i);
            fd = open(dev, O_WRONLY);
            if (fd >= 0 || errno != EBUSY)
                break;
        }
    }
    if (fd < 0) {
        log_err("arp: open bpf: %s", strerror(errno));
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, BIOCSETIF, &ifr) < 0) {
        log_err("arp: BIOCSETIF %s: %s", ifname, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

int arp_send_gratuitous(const char *ifname, struct in_addr ip)
{
    uint8_t mac[ETHER_ADDR_LEN];
    struct {
        struct ether_header eh;
        struct ether_arp    ea;
    } __attribute__((packed)) frame;
    char ipstr[INET_ADDRSTRLEN];
    int fd;
    ssize_t w;

    if (get_hwaddr(ifname, mac) != 0)
        return -1;

    memset(&frame, 0, sizeof(frame));
    memset(frame.eh.ether_dhost, 0xff, ETHER_ADDR_LEN);   /* broadcast */
    memcpy(frame.eh.ether_shost, mac, ETHER_ADDR_LEN);
    frame.eh.ether_type = htons(ETHERTYPE_ARP);

    frame.ea.arp_hrd = htons(ARPHRD_ETHER);
    frame.ea.arp_pro = htons(ETHERTYPE_IP);
    frame.ea.arp_hln = ETHER_ADDR_LEN;
    frame.ea.arp_pln = sizeof(struct in_addr);
    frame.ea.arp_op  = htons(ARPOP_REQUEST);
    memcpy(frame.ea.arp_sha, mac, ETHER_ADDR_LEN);
    memcpy(frame.ea.arp_spa, &ip, sizeof(ip));            /* sender = VIP */
    /* arp_tha stays zero; target = VIP makes it gratuitous */
    memcpy(frame.ea.arp_tpa, &ip, sizeof(ip));

    fd = open_bpf(ifname);
    if (fd < 0)
        return -1;

    w = write(fd, &frame, sizeof(frame));
    close(fd);
    if (w != (ssize_t)sizeof(frame)) {
        log_err("arp: write %s: %s", ifname, strerror(errno));
        return -1;
    }

    if (!inet_ntop(AF_INET, &ip, ipstr, sizeof(ipstr)))
        snprintf(ipstr, sizeof(ipstr), "?");
    log_info("arp: gratuitous ARP for %s on %s", ipstr, ifname);
    return 0;
}
