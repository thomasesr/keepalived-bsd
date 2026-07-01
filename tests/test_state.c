/* Unit test for the pure FSM helpers in src/state.c. Run: make check */
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "state.h"
#include "sidefx.h"

/* state.c references this (normally defined in main.c). */
volatile sig_atomic_t g_running = 1;

/* state.c calls into sidefx.c (FreeBSD-only I/O); stub them for the pure
 * FSM unit test so it links without the transition side-effect code. */
void sidefx_enter_master(const vrrp_instance_t *in) { (void)in; }
void sidefx_enter_backup(const vrrp_instance_t *in) { (void)in; }
void sidefx_carp_guard(void) { }

static int fails = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); fails++; } \
                         else      { printf("ok:   %s\n", m); } } while (0)

static vrrp_advert_t mkadv(uint8_t prio, const char *src)
{
    vrrp_advert_t a;
    memset(&a, 0, sizeof(a));
    a.priority = prio;
    inet_pton(AF_INET, src, &a.src);
    return a;
}

int main(void)
{
    struct in_addr me;
    vrrp_advert_t hi, lo, eq_hi, eq_lo, stop;

    inet_pton(AF_INET, "192.168.1.1", &me);

    /* timers (RFC 5798 s6.1) */
    CHECK(vrrp_skew_cs(100, 100) == 60,   "skew(prio100, 100cs) = 60");
    CHECK(vrrp_skew_cs(110, 500) == 285,  "skew(prio110, 500cs) = 285");
    CHECK(vrrp_skew_cs(255, 100) == 0,    "skew(owner) = 0");
    CHECK(vrrp_master_down_cs(110, 500) == 1785, "master_down(110,500) = 1785");
    CHECK(vrrp_master_down_cs(255, 100) == 300,  "master_down(owner,100) = 300");

    hi    = mkadv(120, "192.168.1.3");
    lo    = mkadv(100, "192.168.1.3");
    eq_hi = mkadv(110, "192.168.1.3");   /* equal prio, higher IP than me */
    eq_lo = mkadv(110, "192.168.1.0");   /* equal prio, lower IP than me  */
    stop  = mkadv(0,   "192.168.1.3");   /* peer resigning                */

    /* BACKUP */
    CHECK(vrrp_recv_action(VRRP_STATE_BACKUP, 110, me, 1, &lo) == VRRP_ACT_NONE,
          "backup+preempt, lower prio -> ignore (we take over)");
    CHECK(vrrp_recv_action(VRRP_STATE_BACKUP, 110, me, 1, &hi) == VRRP_ACT_RESET_TIMER,
          "backup, higher prio -> reset master-down");
    CHECK(vrrp_recv_action(VRRP_STATE_BACKUP, 110, me, 1, &stop) == VRRP_ACT_RESET_TIMER_SKEW,
          "backup, prio 0 -> arm skew");
    CHECK(vrrp_recv_action(VRRP_STATE_BACKUP, 110, me, 0, &lo) == VRRP_ACT_RESET_TIMER,
          "backup+no-preempt, lower prio -> honor master");

    /* MASTER */
    CHECK(vrrp_recv_action(VRRP_STATE_MASTER, 110, me, 1, &hi) == VRRP_ACT_BECOME_BACKUP,
          "master, higher prio -> yield");
    CHECK(vrrp_recv_action(VRRP_STATE_MASTER, 110, me, 1, &lo) == VRRP_ACT_NONE,
          "master, lower prio -> stay");
    CHECK(vrrp_recv_action(VRRP_STATE_MASTER, 110, me, 1, &eq_hi) == VRRP_ACT_BECOME_BACKUP,
          "master, equal prio + higher IP -> yield");
    CHECK(vrrp_recv_action(VRRP_STATE_MASTER, 110, me, 1, &eq_lo) == VRRP_ACT_NONE,
          "master, equal prio + lower IP -> stay");
    CHECK(vrrp_recv_action(VRRP_STATE_MASTER, 110, me, 1, &stop) == VRRP_ACT_SEND_NOW,
          "master, peer prio 0 -> re-assert now");

    printf("\n%s (%d failures)\n", fails ? "TESTS FAILED" : "ALL TESTS PASSED", fails);
    return fails ? 1 : 0;
}
