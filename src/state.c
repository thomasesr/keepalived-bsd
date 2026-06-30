#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "state.h"
#include "heartbeat.h"
#include "iface.h"
#include "dhcp.h"
#include "alias.h"
#include "logger.h"

static time_t now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

int state_init(state_ctx_t *ctx, config_t *cfg)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->cfg     = cfg;
    ctx->current = STATE_BACKUP;

    ctx->sock = hb_socket_open(cfg->port);
    if (ctx->sock < 0) {
        log_err("state_init: failed to open heartbeat socket on port %u", cfg->port);
        return -1;
    }

    /* non-blocking so recv loop doesn't stall the send timer */
    int flags = fcntl(ctx->sock, F_GETFL, 0);
    fcntl(ctx->sock, F_SETFL, flags | O_NONBLOCK);

    /* Discover the local source address used to reach the peer. A connected
     * UDP socket lets getsockname() report the kernel's chosen source IP,
     * which is a stable identity for the equal-priority split-brain tiebreak. */
    ctx->local_addr.s_addr = INADDR_ANY;
    {
        int tmp = socket(AF_INET, SOCK_DGRAM, 0);
        if (tmp >= 0) {
            struct sockaddr_in pa;
            socklen_t ln = sizeof(pa);
            memset(&pa, 0, sizeof(pa));
            pa.sin_family = AF_INET;
            pa.sin_addr   = cfg->peer_addr;
            pa.sin_port   = htons(cfg->port);
            if (connect(tmp, (struct sockaddr *)&pa, sizeof(pa)) == 0 &&
                getsockname(tmp, (struct sockaddr *)&pa, &ln) == 0)
                ctx->local_addr = pa.sin_addr;
            close(tmp);
        }
    }

    ctx->last_hb_recv = now(); /* avoid immediate failover on startup */
    return 0;
}

void state_enter_master(state_ctx_t *ctx)
{
    int i;
    log_info("state: transitioning to MASTER");
    ctx->current = STATE_MASTER;
    for (i = 0; i < ctx->cfg->iface_count; i++) {
        iface_vip_add(&ctx->cfg->ifaces[i]);
        alias_add_vip(&ctx->cfg->ifaces[i]);
        dhcp_enable_iface(&ctx->cfg->ifaces[i], ctx->cfg->ifaces[i].dhcp_backend);
    }
}

void state_enter_backup(state_ctx_t *ctx)
{
    int i;
    log_info("state: transitioning to BACKUP");
    ctx->current = STATE_BACKUP;
    for (i = 0; i < ctx->cfg->iface_count; i++) {
        dhcp_disable_iface(&ctx->cfg->ifaces[i], ctx->cfg->ifaces[i].dhcp_backend);
        alias_del_vip(&ctx->cfg->ifaces[i]);
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

    while (g_running) {
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
                    if (ctx->current == STATE_BACKUP) {
                        log_info("state: peer sent goodbye — taking MASTER");
                        state_enter_master(ctx);
                    }
                    continue;
                }

                /* Only MASTERs transmit heartbeats, so a packet arriving while
                 * we are MASTER means the peer is ALSO MASTER — a split brain
                 * that must be resolved deterministically. Highest priority
                 * wins, regardless of the preempt flag (preempt cannot prevent
                 * split-brain resolution because BACKUP nodes are silent). */
                if (ctx->current == STATE_MASTER) {
                    if (pkt.priority > cfg->priority) {
                        log_info("state: peer priority %u > ours %u, yielding MASTER",
                                 pkt.priority, cfg->priority);
                        state_enter_backup(ctx);
                    } else if (pkt.priority == cfg->priority) {
                        /* Equal priority: break the tie deterministically so
                         * exactly one node yields. The node with the lower
                         * source IP yields; both sides apply the same rule with
                         * addresses swapped, so the result is consistent. */
                        if (ntohl(ctx->local_addr.s_addr) <
                            ntohl(from.sin_addr.s_addr)) {
                            log_warn("state: EQUAL priority %u split brain — "
                                     "yielding (lower IP); set distinct priorities",
                                     pkt.priority);
                            state_enter_backup(ctx);
                        } else {
                            log_warn("state: EQUAL priority %u split brain — "
                                     "holding MASTER (higher IP); set distinct "
                                     "priorities", pkt.priority);
                        }
                    }
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

    /* Loop exited via g_running == 0 (SIGTERM/SIGINT). */
    log_info("state: shutdown requested");
    state_shutdown(ctx);
}

/* Graceful teardown: if MASTER, tell the peer to take over immediately
 * (GOODBYE), then release VIPs / aliases and disable DHCP. Idempotent. */
void state_shutdown(state_ctx_t *ctx)
{
    config_t          *cfg = ctx->cfg;
    struct sockaddr_in peer;
    hb_packet_t        pkt;

    if (ctx->current == STATE_MASTER && ctx->sock >= 0) {
        memset(&peer, 0, sizeof(peer));
        peer.sin_family = AF_INET;
        peer.sin_addr   = cfg->peer_addr;
        peer.sin_port   = htons(cfg->port);

        /* Send twice — UDP is unreliable and a lost GOODBYE forces the peer
         * to wait the full failover timeout before promoting. */
        hb_fill(&pkt, cfg->priority, 1, ++ctx->send_seq, 1);
        hb_send(ctx->sock, &peer, &pkt);
        hb_fill(&pkt, cfg->priority, 1, ++ctx->send_seq, 1);
        hb_send(ctx->sock, &peer, &pkt);
        log_info("state: sent GOODBYE to peer");

        state_enter_backup(ctx); /* remove VIPs, alias entries, disable DHCP */
    }

    if (ctx->sock >= 0) {
        close(ctx->sock);
        ctx->sock = -1;
    }
}
