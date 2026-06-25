#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "dhcp.h"
#include "logger.h"

/* Defaults */
#define DEFAULT_PORT      5405
#define DEFAULT_PRIORITY  100
#define DEFAULT_HEARTBEAT 1
#define DEFAULT_TIMEOUT   3
#define DEFAULT_PREEMPT   1

static int parse_vip(const char *str, struct in_addr *addr, uint8_t *prefix)
{
    char buf[64];
    char *slash;

    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    slash = strchr(buf, '/');
    if (slash) {
        *slash = '\0';
        *prefix = (uint8_t)atoi(slash + 1);
    } else {
        *prefix = 32;
    }

    return inet_pton(AF_INET, buf, addr) == 1 ? 0 : -1;
}

int config_load(const char *path, config_t *cfg)
{
    FILE *f;
    char  line[256];
    char  section[64] = "";
    int   iface_idx   = -1;

    memset(cfg, 0, sizeof(*cfg));
    cfg->port          = DEFAULT_PORT;
    cfg->priority      = DEFAULT_PRIORITY;
    cfg->heartbeat_sec = DEFAULT_HEARTBEAT;
    cfg->timeout_sec   = DEFAULT_TIMEOUT;
    cfg->preempt       = DEFAULT_PREEMPT;
    cfg->dhcp_backend  = DHCP_BACKEND_ISC;

    f = fopen(path, "r");
    if (!f) {
        log_err("config_load: cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        char  key[64], val[192];

        /* strip newline */
        line[strcspn(line, "\r\n")] = '\0';

        /* skip whitespace */
        while (*p == ' ' || *p == '\t') p++;

        /* skip comments and blank lines */
        if (*p == '#' || *p == ';' || *p == '\0') continue;

        /* section header: [iface em0] or [global] */
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) continue;
            *end = '\0';
            strncpy(section, p + 1, sizeof(section) - 1);
            section[sizeof(section) - 1] = '\0';

            if (strncmp(section, "iface ", 6) == 0) {
                if (cfg->iface_count >= MAX_IFACES) {
                    log_warn("config: max %d ifaces, ignoring %s", MAX_IFACES, section);
                    iface_idx = -1;
                } else {
                    iface_idx = cfg->iface_count++;
                    strncpy(cfg->ifaces[iface_idx].iface,
                            section + 6, IFNAMSIZ - 1);
                    cfg->ifaces[iface_idx].dhcp_backend = DHCP_BACKEND_INHERIT;
                }
            } else {
                iface_idx = -1;
            }
            continue;
        }

        /* key = value */
        if (sscanf(p, "%63s = %191[^\n]", key, val) != 2) continue;

        if (iface_idx >= 0) {
            iface_cfg_t *ic = &cfg->ifaces[iface_idx];
            if (strcmp(key, "vip") == 0) {
                strncpy(ic->vip_str, val, sizeof(ic->vip_str) - 1);
                if (parse_vip(val, &ic->vip_addr, &ic->prefix_len) != 0)
                    log_warn("config: invalid vip '%s'", val);
            } else if (strcmp(key, "dhcp_backend") == 0) {
                ic->dhcp_backend = dhcp_backend_parse(val);
            }
        } else {
            if      (strcmp(key, "peer")      == 0) {
                strncpy(cfg->peer_str, val, sizeof(cfg->peer_str) - 1);
                if (inet_pton(AF_INET, val, &cfg->peer_addr) != 1)
                    log_warn("config: invalid peer '%s'", val);
            }
            else if (strcmp(key, "port")      == 0) cfg->port          = (uint16_t)atoi(val);
            else if (strcmp(key, "priority")  == 0) cfg->priority      = (uint8_t)atoi(val);
            else if (strcmp(key, "heartbeat") == 0) cfg->heartbeat_sec = atoi(val);
            else if (strcmp(key, "timeout")   == 0) cfg->timeout_sec   = atoi(val);
            else if (strcmp(key, "preempt")      == 0) cfg->preempt      = (strcmp(val, "yes") == 0);
            else if (strcmp(key, "dhcp_backend") == 0) cfg->dhcp_backend = dhcp_backend_parse(val);
        }
    }

    fclose(f);

    /* resolve per-iface INHERIT → global backend */
    {
        int i;
        for (i = 0; i < cfg->iface_count; i++) {
            if (cfg->ifaces[i].dhcp_backend == DHCP_BACKEND_INHERIT)
                cfg->ifaces[i].dhcp_backend = cfg->dhcp_backend;
        }
    }

    if (cfg->peer_str[0] == '\0') {
        log_err("config: 'peer' is required");
        return -1;
    }
    if (cfg->iface_count == 0) {
        log_err("config: at least one [iface] section required");
        return -1;
    }

    return 0;
}

void config_dump(const config_t *cfg)
{
    int i;
    log_info("config: peer=%s port=%u priority=%u hb=%ds timeout=%ds preempt=%d dhcp=%s",
             cfg->peer_str, cfg->port, cfg->priority,
             cfg->heartbeat_sec, cfg->timeout_sec, cfg->preempt,
             dhcp_backend_name(cfg->dhcp_backend));
    for (i = 0; i < cfg->iface_count; i++)
        log_info("config: iface[%d] %s vip=%s/%u dhcp=%s",
                 i, cfg->ifaces[i].iface,
                 cfg->ifaces[i].vip_str, cfg->ifaces[i].prefix_len,
                 dhcp_backend_name(cfg->ifaces[i].dhcp_backend));
}
