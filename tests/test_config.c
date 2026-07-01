/* Unit test for src/config.c — [vrrp_instance] parsing. Run: make check */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "config.h"
#include "logger.h"

/* stub the dhcp helpers so config.c links without dhcp.c (FreeBSD-only bits) */
dhcp_backend_t dhcp_backend_parse(const char *s)
{
    if (!strcmp(s, "isc"))     return DHCP_BACKEND_ISC;
    if (!strcmp(s, "kea"))     return DHCP_BACKEND_KEA;
    if (!strcmp(s, "dnsmasq")) return DHCP_BACKEND_DNSMASQ;
    if (!strcmp(s, "none"))    return DHCP_BACKEND_NONE;
    return DHCP_BACKEND_INHERIT;
}
const char *dhcp_backend_name(dhcp_backend_t b)
{
    switch (b) {
    case DHCP_BACKEND_ISC:     return "isc";
    case DHCP_BACKEND_KEA:     return "kea";
    case DHCP_BACKEND_DNSMASQ: return "dnsmasq";
    case DHCP_BACKEND_NONE:    return "none";
    default:                   return "inherit";
    }
}

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); fails++; } \
                         else      { printf("ok:   %s\n", m); } } while (0)

static const char *CFG =
    "[global]\n"
    "  priority = 105\n"
    "  dhcp_backend = dnsmasq\n"
    "\n"
    "[vrrp_instance master]\n"
    "  state = MASTER\n"
    "  interface = igb0\n"
    "  unicast_src_ip = 192.168.1.1\n"
    "  unicast_peer = 192.168.1.3\n"
    "  virtual_router_id = 10\n"
    "  priority = 110\n"
    "  advert_int = 5\n"
    "  vip = 192.165.1.2/24 dev igb0 label lan:vip1 scope global\n"
    "  version = 3\n"
    "\n"
    "[vrrp_instance VI_666]\n"
    "  interface = igb0\n"
    "  unicast_src_ip = 192.168.1.1\n"
    "  unicast_peer = 192.168.1.3\n"
    "  virtual_router_id = 66\n"
    "  advert_int = 5\n"
    "  vip = 10.6.6.2/24\n";

static void write_cfg(const char *path, const char *body)
{
    FILE *f = fopen(path, "w");
    if (f) { fputs(body, f); fclose(f); }
}

int main(void)
{
    const char *path = "tests/_cfg_test.conf";
    config_t cfg;
    struct in_addr a;

    log_init("test", 1);
    write_cfg(path, CFG);
    CHECK(config_load(path, &cfg) == 0, "config_load succeeds");
    CHECK(cfg.inst_count == 2, "two instances parsed");

    CHECK(strcmp(cfg.inst[0].name, "master") == 0, "inst0 name = master");
    CHECK(cfg.inst[0].vrid == 10, "inst0 vrid = 10");
    CHECK(cfg.inst[0].priority == 110, "inst0 priority = 110 (explicit)");
    CHECK(cfg.inst[0].initial == VRRP_STATE_MASTER, "inst0 state = MASTER");
    CHECK(cfg.inst[0].adver_cs == 500, "inst0 advert_int 5s -> 500cs");
    CHECK(strcmp(cfg.inst[0].adv_if, "igb0") == 0, "inst0 interface = igb0");
    inet_pton(AF_INET, "192.168.1.3", &a);
    CHECK(cfg.inst[0].peer_ip.s_addr == a.s_addr, "inst0 unicast_peer parsed");
    CHECK(cfg.inst[0].dhcp_backend == DHCP_BACKEND_DNSMASQ, "inst0 dhcp inherit -> dnsmasq");
    CHECK(cfg.inst[0].vip_count == 1, "inst0 one vip");
    inet_pton(AF_INET, "192.165.1.2", &a);
    CHECK(cfg.inst[0].vips[0].addr.s_addr == a.s_addr, "inst0 vip addr");
    CHECK(cfg.inst[0].vips[0].prefix_len == 24, "inst0 vip prefix 24");
    CHECK(strcmp(cfg.inst[0].vips[0].dev, "igb0") == 0, "inst0 vip dev = igb0");
    CHECK(strcmp(cfg.inst[0].vips[0].label, "lan:vip1") == 0, "inst0 vip label parsed");

    CHECK(cfg.inst[1].vrid == 66, "inst1 vrid = 66");
    CHECK(cfg.inst[1].priority == 105, "inst1 priority = 105 (global fallback)");
    CHECK(cfg.inst[1].initial == VRRP_STATE_BACKUP, "inst1 default state BACKUP");
    CHECK(strcmp(cfg.inst[1].vips[0].dev, "igb0") == 0, "inst1 vip dev defaults to adv_if");

    /* legacy [iface] must be rejected with a migration error */
    write_cfg(path, "[iface em0]\n  vip = 10.0.0.1/24\n");
    CHECK(config_load(path, &cfg) == -1, "legacy [iface] rejected");

    /* missing vrid must fail validation */
    write_cfg(path,
        "[vrrp_instance x]\n  interface = igb0\n"
        "  unicast_src_ip = 1.1.1.1\n  unicast_peer = 1.1.1.2\n");
    CHECK(config_load(path, &cfg) == -1, "missing virtual_router_id rejected");

    remove(path);
    printf("\n%s (%d failures)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails);
    return fails ? 1 : 0;
}
