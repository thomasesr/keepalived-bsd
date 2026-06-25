#ifndef STATE_H
#define STATE_H

#include <time.h>
#include <stdint.h>
#include "config.h"

typedef enum {
    STATE_BACKUP = 0,
    STATE_MASTER = 1
} node_state_t;

typedef struct {
    node_state_t current;
    config_t    *cfg;
    time_t       last_hb_recv; /* monotonic: last heartbeat from peer */
    uint32_t     send_seq;
    int          sock;
} state_ctx_t;

void state_init(state_ctx_t *ctx, config_t *cfg);
void state_run(state_ctx_t *ctx);   /* blocking event loop */
void state_enter_master(state_ctx_t *ctx);
void state_enter_backup(state_ctx_t *ctx);

#endif /* STATE_H */
