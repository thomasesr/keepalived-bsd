/*
 * Per-VRID VRRPv3 finite state machine (RFC 5798 s6). One shared raw socket
 * carries all instances; received adverts are demultiplexed by VRID.
 *
 * The pure decision helpers (skew/master-down timers and the receive action)
 * carry no I/O and are unit-tested. Transition side-effects (VIP add/del, DHCP
 * toggle, gratuitous ARP) are log-only stubs here and are implemented in
 * Phase 5.
 */

#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "state.h"
#include "net.h"
#include "vrrp.h"
#include "sidefx.h"
#include "logger.h"

/* ── pure FSM helpers (RFC 5798 s6.1, s6.4) ─────────────────────────────── */

uint32_t vrrp_skew_cs(uint8_t priority, uint16_t adver_cs)
{
    /* Skew_Time = ((256 - priority) * Master_Adver_Interval) / 256 */
    return (uint32_t)(256u - priority) * (uint32_t)adver_cs / 256u;
}

uint32_t vrrp_master_down_cs(uint8_t priority, uint16_t adver_cs)
{
    /* Master_Down_Interval = 3 * Master_Adver_Interval + Skew_Time */
    return 3u * (uint32_t)adver_cs + vrrp_skew_cs(priority, adver_cs);
}

vrrp_action_t vrrp_recv_action(vrrp_state_t state, uint8_t my_prio,
                               struct in_addr my_ip, int preempt,
                               const vrrp_advert_t *adv)
{
    if (state == VRRP_STATE_BACKUP) {
        if (adv->priority == 0)
            return VRRP_ACT_RESET_TIMER_SKEW;            /* peer resigning */
        if (!preempt || adv->priority >= my_prio)
            return VRRP_ACT_RESET_TIMER;                 /* valid master */
        return VRRP_ACT_NONE;                            /* lower: we take over */
    }
    if (state == VRRP_STATE_MASTER) {
        if (adv->priority == 0)
            return VRRP_ACT_SEND_NOW;                    /* peer resigned */
        if (adv->priority > my_prio ||
            (adv->priority == my_prio &&
             ntohl(adv->src.s_addr) > ntohl(my_ip.s_addr)))
            return VRRP_ACT_BECOME_BACKUP;               /* higher wins */
        return VRRP_ACT_NONE;                            /* we stay master */
    }
    return VRRP_ACT_NONE;
}

/* Transition side-effects (VIP/DHCP/gARP/alias) live in sidefx.c; the FSM stays
 * portable and the unit tests link their own no-op stubs. */

/* ── internals ──────────────────────────────────────────────────────────── */

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static int send_advert(state_ctx_t *ctx, vrrp_rt_t *rt, uint8_t priority)
{
    uint8_t buf[VRRP_HDR_LEN + VRRP_MAX_VIPS * 4];
    vrrp_advert_t a;
    int i, n;

    memset(&a, 0, sizeof(a));
    a.vrid      = rt->cfg->vrid;
    a.priority  = priority;
    a.adver_cs  = rt->cfg->adver_cs;
    a.vip_count = rt->cfg->vip_count;
    for (i = 0; i < rt->cfg->vip_count; i++)
        a.vips[i] = rt->cfg->vips[i].addr;

    n = vrrp_advert_encode(buf, sizeof(buf), &a, ctx->src_ip, rt->cfg->peer_ip);
    if (n < 0)
        return -1;
    if (net_vrrp_send(ctx->sock, rt->cfg->peer_ip, buf, (size_t)n) != 0)
        return -1;
    rt->probes_sent++;
    return 0;
}

static void enter_master(state_ctx_t *ctx, vrrp_rt_t *rt)
{
    rt->state = VRRP_STATE_MASTER;
    rt->last_transition = time(NULL);
    send_advert(ctx, rt, rt->cfg->priority);
    rt->next_adv_ms = now_ms() + (uint64_t)rt->cfg->adver_cs * 10u;
    sidefx_enter_master(rt->cfg);
}

static void enter_backup(state_ctx_t *ctx, vrrp_rt_t *rt)
{
    (void)ctx;
    rt->state = VRRP_STATE_BACKUP;
    rt->last_transition = time(NULL);
    rt->master_down_ms = now_ms() +
        (uint64_t)vrrp_master_down_cs(rt->cfg->priority, rt->cfg->adver_cs) * 10u;
    sidefx_enter_backup(rt->cfg);
}

static void dispatch(state_ctx_t *ctx, const vrrp_advert_t *adv)
{
    uint64_t now;
    int i;

    for (i = 0; i < ctx->count; i++) {
        vrrp_rt_t *rt = &ctx->rt[i];
        if (rt->cfg->vrid != adv->vrid)
            continue;
        if (adv->src.s_addr != rt->cfg->peer_ip.s_addr)
            continue;                        /* only adverts from our peer */
        rt->probes_received++;
        now = now_ms();
        switch (vrrp_recv_action(rt->state, rt->cfg->priority,
                                 ctx->src_ip, rt->cfg->preempt, adv)) {
        case VRRP_ACT_RESET_TIMER:
            rt->master_down_ms = now +
                (uint64_t)vrrp_master_down_cs(rt->cfg->priority,
                                              rt->cfg->adver_cs) * 10u;
            break;
        case VRRP_ACT_RESET_TIMER_SKEW:
            rt->master_down_ms = now +
                (uint64_t)vrrp_skew_cs(rt->cfg->priority, rt->cfg->adver_cs) * 10u;
            break;
        case VRRP_ACT_BECOME_BACKUP:
            enter_backup(ctx, rt);
            break;
        case VRRP_ACT_SEND_NOW:
            send_advert(ctx, rt, rt->cfg->priority);
            rt->next_adv_ms = now + (uint64_t)rt->cfg->adver_cs * 10u;
            break;
        default:
            break;
        }
        return;
    }
}

/* ── public API ─────────────────────────────────────────────────────────── */

int state_init(state_ctx_t *ctx, config_t *cfg)
{
    int i;

    memset(ctx, 0, sizeof(*ctx));
    ctx->cfg    = cfg;
    ctx->count  = cfg->inst_count;
    ctx->src_ip = cfg->inst[0].src_ip;

    for (i = 1; i < cfg->inst_count; i++)
        if (cfg->inst[i].src_ip.s_addr != ctx->src_ip.s_addr)
            log_warn("state: [%s] unicast_src_ip differs; all instances share "
                     "one socket source for now", cfg->inst[i].name);

    ctx->sock = net_vrrp_open(ctx->src_ip);
    if (ctx->sock < 0) {
        log_err("state_init: cannot open VRRP socket");
        return -1;
    }

    for (i = 0; i < cfg->inst_count; i++) {
        vrrp_rt_t *rt = &ctx->rt[i];
        rt->cfg   = &cfg->inst[i];
        rt->state = VRRP_STATE_INIT;
        rt->last_transition = time(NULL);
    }

    /* Startup election (RFC 5798 s6.4.1): the address owner (priority 255) goes
     * MASTER at once; everyone else starts BACKUP and waits Master_Down. */
    for (i = 0; i < cfg->inst_count; i++) {
        vrrp_rt_t *rt = &ctx->rt[i];
        if (rt->cfg->priority == VRRP_PRIO_OWNER)
            enter_master(ctx, rt);
        else
            enter_backup(ctx, rt);
    }
    return 0;
}

void state_run(state_ctx_t *ctx)
{
    uint8_t buf[256];
    struct in_addr src;
    uint64_t now;
    int ttl, r, i;

    log_info("state: event loop start, %d instance(s)", ctx->count);

    while (g_running) {
        /* drain the shared socket; dispatch each advert to its VRID */
        while ((r = net_vrrp_recv(ctx->sock, buf, sizeof(buf), &src, &ttl)) != -1) {
            vrrp_advert_t adv;
            if (r > 0 &&
                vrrp_advert_decode(buf, (size_t)r, src, ctx->src_ip, &adv) == 0)
                dispatch(ctx, &adv);
        }

        now = now_ms();
        for (i = 0; i < ctx->count; i++) {
            vrrp_rt_t *rt = &ctx->rt[i];
            if (rt->state == VRRP_STATE_MASTER) {
                if (now >= rt->next_adv_ms) {
                    send_advert(ctx, rt, rt->cfg->priority);
                    rt->next_adv_ms = now + (uint64_t)rt->cfg->adver_cs * 10u;
                }
            } else if (rt->state == VRRP_STATE_BACKUP) {
                if (now >= rt->master_down_ms)
                    enter_master(ctx, rt);
            }
        }
        usleep(50000); /* 50 ms poll */
    }

    log_info("state: shutdown requested");
    state_shutdown(ctx);
}

void state_shutdown(state_ctx_t *ctx)
{
    int i;

    for (i = 0; i < ctx->count; i++) {
        vrrp_rt_t *rt = &ctx->rt[i];
        if (rt->state == VRRP_STATE_MASTER) {
            /* RFC 5798: a master shutting down sends a priority-0 advert so the
             * backup takes over immediately rather than waiting Master_Down. */
            send_advert(ctx, rt, VRRP_PRIO_STOP);
            enter_backup(ctx, rt);   /* release VIPs / DHCP (Phase 5) */
        }
    }
    if (ctx->sock >= 0) {
        close(ctx->sock);
        ctx->sock = -1;
    }
    log_info("state: shutdown complete");
}

