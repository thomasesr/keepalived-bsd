#ifndef STATE_H
#define STATE_H

#include <signal.h>
#include <time.h>
#include <stdint.h>
#include "config.h"
#include "vrrp.h"

/* Cleared by the signal handler (main.c) to request shutdown; read by the
 * event loop. */
extern volatile sig_atomic_t g_running;

/* Per-instance runtime state (one VRID). Feeds the status view. */
typedef struct {
    const vrrp_instance_t *cfg;
    vrrp_state_t state;             /* INIT / BACKUP / MASTER */
    uint64_t     master_down_ms;    /* BACKUP: promote deadline (monotonic ms) */
    uint64_t     next_adv_ms;       /* MASTER: next advert send (monotonic ms) */
    time_t       last_transition;   /* wall-clock epoch of last state change */
    uint64_t     probes_sent;       /* adverts sent for this VRID */
    uint64_t     probes_received;   /* adverts received for this VRID */
} vrrp_rt_t;

typedef struct {
    config_t      *cfg;
    int            sock;            /* shared raw VRRP socket */
    struct in_addr src_ip;          /* bound source address */
    vrrp_rt_t      rt[MAX_INSTANCES];
    int            count;
    uint64_t       next_status_ms;  /* status file heartbeat deadline (mono ms) */
    int            status_dirty;    /* a transition needs an immediate write   */
} state_ctx_t;

/* Action decided by the receive path (RFC 5798 s6.4). */
typedef enum {
    VRRP_ACT_NONE = 0,        /* discard advert, keep current timers    */
    VRRP_ACT_BECOME_MASTER,
    VRRP_ACT_BECOME_BACKUP,
    VRRP_ACT_RESET_TIMER,     /* BACKUP: rearm Master_Down_Interval     */
    VRRP_ACT_RESET_TIMER_SKEW,/* BACKUP: rearm to Skew_Time (peer prio 0)*/
    VRRP_ACT_SEND_NOW         /* MASTER: peer resigned, re-assert now    */
} vrrp_action_t;

/* Pure FSM helpers (unit-tested; no I/O). Timers in centiseconds. */
uint32_t vrrp_skew_cs(uint8_t priority, uint16_t adver_cs);
uint32_t vrrp_master_down_cs(uint8_t priority, uint16_t adver_cs);
vrrp_action_t vrrp_recv_action(vrrp_state_t state, uint8_t my_prio,
                               struct in_addr my_ip, int preempt,
                               const vrrp_advert_t *adv);

int  state_init(state_ctx_t *ctx, config_t *cfg);   /* 0 ok, -1 socket fail */
void state_run(state_ctx_t *ctx);                   /* blocking event loop  */
void state_shutdown(state_ctx_t *ctx);              /* resign + teardown    */

#endif /* STATE_H */
