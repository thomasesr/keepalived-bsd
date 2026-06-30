#ifndef STATE_H
#define STATE_H

#include <signal.h>
#include <time.h>
#include <stdint.h>
#include "config.h"

/* Set to 0 by the signal handler (main.c) to request a clean shutdown.
 * Read by the state_run() event loop. */
extern volatile sig_atomic_t g_running;

typedef enum {
    STATE_BACKUP = 0,
    STATE_MASTER = 1
} node_state_t;

typedef struct {
    node_state_t   current;
    config_t      *cfg;
    time_t         last_hb_recv; /* monotonic: last heartbeat from peer */
    uint32_t       send_seq;
    int            sock;
    struct in_addr local_addr;   /* local source IP toward peer; equal-priority tiebreak */
} state_ctx_t;

int  state_init(state_ctx_t *ctx, config_t *cfg);  /* 0 ok, -1 on socket failure */
void state_run(state_ctx_t *ctx);   /* blocking event loop, returns on shutdown */
void state_enter_master(state_ctx_t *ctx);
void state_enter_backup(state_ctx_t *ctx);
void state_shutdown(state_ctx_t *ctx);  /* goodbye + teardown on exit */

#endif /* STATE_H */
