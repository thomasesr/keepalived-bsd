/* Unit test for src/vrrp.c — round-trip, checksum, bounds. Run: make check */
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "vrrp.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else         { printf("ok:   %s\n", msg); } \
} while (0)

int main(void)
{
    struct in_addr src, dst;
    inet_pton(AF_INET, "192.168.1.1", &src);
    inet_pton(AF_INET, "192.168.1.3", &dst);

    vrrp_advert_t a;
    memset(&a, 0, sizeof(a));
    a.vrid = 10; a.priority = 110; a.adver_cs = 500; /* advert_int 5s */
    a.vip_count = 2;
    inet_pton(AF_INET, "192.165.1.2", &a.vips[0]);
    inet_pton(AF_INET, "10.6.6.2",    &a.vips[1]);

    uint8_t buf[64];
    int n = vrrp_advert_encode(buf, sizeof(buf), &a, src, dst);
    CHECK(n == VRRP_HDR_LEN + 2 * 4, "encode length = 8 + 2*4 = 16");
    CHECK(VRRP_VT_VER(buf[0]) == 3 && VRRP_VT_TYPE(buf[0]) == 1, "ver=3 type=1 (byte0=0x31)");
    CHECK(buf[0] == 0x31, "first byte literally 0x31");

    /* the encoded packet must self-verify (checksum folds to 0) */
    CHECK(vrrp_checksum(buf, n, src, dst) == 0, "checksum verifies over encoded msg");

    vrrp_advert_t b;
    int r = vrrp_advert_decode(buf, (size_t)n, src, dst, &b);
    CHECK(r == 0, "decode succeeds");
    CHECK(b.vrid == 10 && b.priority == 110, "vrid/priority round-trip");
    CHECK(b.adver_cs == 500, "adver_cs round-trip");
    CHECK(b.vip_count == 2, "vip_count round-trip");
    CHECK(b.vips[0].s_addr == a.vips[0].s_addr &&
          b.vips[1].s_addr == a.vips[1].s_addr, "vips round-trip");
    CHECK(b.src.s_addr == src.s_addr, "src set from IP header");

    /* wrong pseudo-header addr must fail the checksum */
    struct in_addr wrong;
    inet_pton(AF_INET, "10.0.0.9", &wrong);
    CHECK(vrrp_advert_decode(buf, (size_t)n, wrong, dst, &b) == -1, "wrong src -> checksum reject");

    /* corrupt a payload byte -> reject */
    uint8_t bad[64]; memcpy(bad, buf, (size_t)n); bad[9] ^= 0xff;
    CHECK(vrrp_advert_decode(bad, (size_t)n, src, dst, &b) == -1, "corrupted byte -> reject");

    /* bad version */
    memcpy(bad, buf, (size_t)n); bad[0] = VRRP_VT(2, 1);
    CHECK(vrrp_advert_decode(bad, (size_t)n, src, dst, &b) == -1, "version 2 -> reject");

    /* truncated */
    CHECK(vrrp_advert_decode(buf, 4, src, dst, &b) == -1, "short buffer -> reject");

    /* count_ip claims more than present */
    memcpy(bad, buf, (size_t)n); bad[3] = 9;
    CHECK(vrrp_advert_decode(bad, (size_t)n, src, dst, &b) == -1, "count_ip > len -> reject");

    /* encode into too-small buffer */
    CHECK(vrrp_advert_encode(buf, 10, &a, src, dst) == -1, "encode buflen too small -> -1");

    printf("\n%s (%d failures)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails);
    return fails ? 1 : 0;
}
