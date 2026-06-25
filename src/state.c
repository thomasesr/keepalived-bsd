#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "state.h"
#include "heartbeat.h"
#include "iface.h"
#include "dhcp.h"
#include "logger.h"

static time_t now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

void state_init(state_ctx_t *ctx, config_t *cfg)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->cfg     = cfg;
    ctx->current = STATE_BACKUP;

    ctx->sock = hb_socket_open(cfg->port);
    if (ctx->sock < 0) {
        log_err("state_init: failed to open heartbeat socket");
        return;
    }

    /* non-blocking so recv loop doesn't stall the send timer */
    int flags = fcntl(ctx->sock, F_GETFL, 0);
    fcntl(ctx->sock, F_SETFL, flags | O_NONBLOCK);

    ctx->last_hb_recv = now(); /* avoid immediate failover on startup */
}

void state_enter_master(state_ctx_t *ctx)
{
    int i;
    log_info("state: transitioning to MASTER");
    ctx->current = STATE_MASTER;
    for (i = 0; i < ctx->cfg->iface_count; i++) {
        iface_vip_add(&ctx->cfg->ifaces[i]);
        dhcp_enable_iface(&ctx->cfg->ifaces[i], ctx->cfg->dhcp_backend);
    }
}

void state_enter_backup(state_ctx_t *ctx)
{
    int i;
    log_info("state: transitioning to BACKUP");
    ctx->current = STATE_BACKUP;
    for (i = 0; i < ctx->cfg->iface_count; i++) {
        dhcp_disable_iface(&ctx->cfg->ifaces[i], ctx->cfg->dhcp_backend);
        iface_vip_del(&ctx->cfg->ifaces[i]);
    }
}

void state_run(state_ctx_t *ctx)
{
    config_t         *cfg  = ctx->cfg;
    struct sockaddr_in peer;
    hb_packet_t       pkt;
    time_t            last_send = 0;

    memset(&peer, 0, sizeof(peer));
    peer.sin_family = AF_INET;
    peer.sin_addr   = cfg->peer_addr;
    peer.sin_port   = htons(cfg->port);

    log_info("state: starting event loop (initial state: BACKUP)");

    for (;;) {
        time_t t = now();

        /* send heartbeat if MASTER */
        if (ctx->current == STATE_MASTER && t - last_send >= cfg->heartbeat_sec) {
            hb_fill(&pkt, cfg->priority, 1, ++ctx->send_seq, 0);
            hb_send(ctx->sock, &peer, &pkt);
            last_send = t;
        }

        /* receive any incoming heartbeats */
        struct sockaddr_in from;
        while (hb_recv(ctx->sock, &pkt, &from) == 0) {
            /* ignore our own packets (shouldn't arrive but guard anyway) */
            if (from.sin_addr.s_addr == cfg->peer_addr.s_addr) {
                ctx->last_hb_recv = t;

                if (pkt.flags & HB_FLAG_GOODBYE) {
                    log_info("state: peer sent goodbye — taking MASTER");
                    if (ctx->current == STATE_BACKUP)
                        state_enter_master(ctx);
                    continue;
                }

                /* preemption: yield if peer has higher priority */
                if (ctx->current == STATE_MASTER &&
                    cfg->preempt &&
                    pkt.priority > cfg->priority) {
                    log_info("state: peer priority %u > ours %u, yielding",
                             pkt.priority, cfg->priority);
                    state_enter_backup(ctx);
                }
            }
        }

        /* failover: promote if peer silent too long */
        if (ctx->current == STATE_BACKUP &&
            t - ctx->last_hb_recv >= cfg->timeout_sec) {
            state_enter_master(ctx);
        }

        usleep(100000); /* 100 ms poll interval */
    }
}
