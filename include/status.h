#ifndef STATUS_H
#define STATUS_H

#include "state.h"

/*
 * Atomically publish the per-instance VRRP status as JSON so the OPNsense UI
 * can poll it (temp file + fsync + rename). Default path
 * /var/run/keepalived_bsd.status, overridable via $KEEPALIVED_STATUS_PATH
 * (used by the unit harness). Returns 0 on success, -1 on error.
 */
int status_write(const state_ctx_t *ctx);

#endif /* STATUS_H */
