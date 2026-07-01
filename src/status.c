/*
 * Atomic JSON status writer. The daemon publishes one snapshot of every VRRP
 * instance; the OPNsense UI polls the file (via a configd cat action). Written
 * to a temp file, fsync'd, then rename()'d over the target so a reader never
 * sees a half-written file. A `written` epoch lets the UI flag stale rows when
 * the daemon has died.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "status.h"
#include "logger.h"

#define STATUS_DEFAULT "/var/run/keepalived_bsd.status"

static const char *status_path(void)
{
    const char *e = getenv("KEEPALIVED_STATUS_PATH");
    return (e && *e) ? e : STATUS_DEFAULT;
}

static const char *state_name(vrrp_state_t s)
{
    switch (s) {
    case VRRP_STATE_MASTER: return "MASTER";
    case VRRP_STATE_BACKUP: return "BACKUP";
    default:                return "INIT";
    }
}

/* Emit s as a JSON string literal, escaping the mandatory characters. */
static void json_str(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { fputc('\\', f); fputc((int)c, f); }
        else if (c < 0x20)          fprintf(f, "\\u%04x", c);
        else                        fputc((int)c, f);
    }
    fputc('"', f);
}

int status_write(const state_ctx_t *ctx)
{
    const char *path = status_path();
    char  tmp[512];
    FILE *f;
    int   fd, i;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    f = fopen(tmp, "w");
    if (!f) {
        log_warn("status: open %s: %s", tmp, strerror(errno));
        return -1;
    }

    fprintf(f, "{\"written\":%lld,\"instances\":[", (long long)time(NULL));
    for (i = 0; i < ctx->count; i++) {
        const vrrp_rt_t       *rt = &ctx->rt[i];
        const vrrp_instance_t *in = rt->cfg;
        if (i) fputc(',', f);
        fputc('{', f);
        fputs("\"name\":", f);        json_str(f, in->name);
        fputs(",\"interface\":", f);  json_str(f, in->adv_if);
        fprintf(f, ",\"vrid\":%u", in->vrid);
        fprintf(f, ",\"priority\":%u", in->priority);
        fputs(",\"state\":", f);      json_str(f, state_name(rt->state));
        fputs(",\"initial\":", f);    json_str(f, state_name(in->initial));
        fprintf(f, ",\"probes_sent\":%llu",
                (unsigned long long)rt->probes_sent);
        fprintf(f, ",\"probes_received\":%llu",
                (unsigned long long)rt->probes_received);
        fprintf(f, ",\"last_transition\":%lld",
                (long long)rt->last_transition);
        fputc('}', f);
    }
    fputs("]}\n", f);

    fflush(f);
    fd = fileno(f);
    if (fd >= 0) fsync(fd);
    if (fclose(f) != 0) {
        log_warn("status: close %s: %s", tmp, strerror(errno));
        return -1;
    }

    if (rename(tmp, path) != 0) {
        log_warn("status: rename %s -> %s: %s", tmp, path, strerror(errno));
        return -1;
    }
    return 0;
}
