/*
 * Config parser: keepalived-style INI with [global] and one or more
 * [vrrp_instance NAME] blocks. Fail-closed on malformed VIPs.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "config.h"
#include "dhcp.h"
#include "logger.h"

#define DEFAULT_PRIORITY 100
#define DEFAULT_ADVER_CS 100   /* 1s, in centiseconds */

static int parse_vip(const char *str, struct in_addr *addr, uint8_t *prefix)
{
    char buf[64];
    char *slash;

    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    slash = strchr(buf, '/');
    if (slash) {
        int v = atoi(slash + 1);
        *slash = '\0';
        if (v < 0 || v > 32)
            return -1;
        *prefix = (uint8_t)v;
    } else {
        *prefix = 32;
    }
    return inet_pton(AF_INET, buf, addr) == 1 ? 0 : -1;
}

static int parse_state(const char *s, vrrp_state_t *out)
{
    if (strcasecmp(s, "MASTER") == 0) { *out = VRRP_STATE_MASTER; return 0; }
    if (strcasecmp(s, "BACKUP") == 0) { *out = VRRP_STATE_BACKUP; return 0; }
    return -1;
}

/* Parse a keepalived-style "ADDR/prefix [dev IF] [label L] [scope S]" line. */
static int parse_vip_line(const char *line, vip_t *v)
{
    char buf[192], *tok, *save;

    memset(v, 0, sizeof(*v));
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    tok = strtok_r(buf, " \t", &save);
    if (!tok || parse_vip(tok, &v->addr, &v->prefix_len) != 0)
        return -1;

    while ((tok = strtok_r(NULL, " \t", &save)) != NULL) {
        if (strcmp(tok, "dev") == 0) {
            tok = strtok_r(NULL, " \t", &save);
            if (tok) strncpy(v->dev, tok, IFNAMSIZ - 1);
        } else if (strcmp(tok, "label") == 0) {
            tok = strtok_r(NULL, " \t", &save);
            if (tok) strncpy(v->label, tok, sizeof(v->label) - 1);
        } else if (strcmp(tok, "scope") == 0) {
            (void)strtok_r(NULL, " \t", &save);   /* skip value */
        }
        /* unknown trailing tokens ignored */
    }
    return 0;
}

int config_load(const char *path, config_t *cfg)
{
    FILE *f;
    char  line[256];
    char  section[96] = "";
    int   idx = -1;   /* current instance, -1 = global/other */
    int   i, j;

    memset(cfg, 0, sizeof(*cfg));
    cfg->def_priority     = 0;                 /* -> DEFAULT_PRIORITY */
    cfg->def_dhcp_backend = DHCP_BACKEND_NONE;  /* fail closed */

    f = fopen(path, "r");
    if (!f) {
        log_err("config_load: cannot open %s: %s", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        char  key[64], val[192];

        line[strcspn(line, "\r\n")] = '\0';
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '\0') continue;

        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) continue;
            *end = '\0';
            strncpy(section, p + 1, sizeof(section) - 1);
            section[sizeof(section) - 1] = '\0';

            if (strncmp(section, "vrrp_instance ", 14) == 0) {
                if (cfg->inst_count >= MAX_INSTANCES) {
                    log_warn("config: max %d instances, ignoring [%s]",
                             MAX_INSTANCES, section);
                    idx = -1;
                } else {
                    vrrp_instance_t *in = &cfg->inst[cfg->inst_count];
                    idx = cfg->inst_count++;
                    strncpy(in->name, section + 14, sizeof(in->name) - 1);
                    in->initial      = VRRP_STATE_BACKUP;
                    in->preempt      = 1;
                    in->dhcp_backend = DHCP_BACKEND_INHERIT;
                }
            } else if (strncmp(section, "iface ", 6) == 0) {
                log_err("config: '[iface]' is the old KALV format; rewrite as "
                        "'[vrrp_instance NAME]' (see PLAN.md/README)");
                fclose(f);
                return -1;
            } else {
                idx = -1;   /* [global] or unknown */
            }
            continue;
        }

        if (sscanf(p, "%63s = %191[^\n]", key, val) != 2) continue;

        if (idx >= 0) {
            vrrp_instance_t *in = &cfg->inst[idx];
            if (strcmp(key, "state") == 0) {
                if (parse_state(val, &in->initial) != 0)
                    log_warn("config: bad state '%s' in [%s]", val, in->name);
            } else if (strcmp(key, "interface") == 0) {
                strncpy(in->adv_if, val, IFNAMSIZ - 1);
            } else if (strcmp(key, "unicast_src_ip") == 0) {
                strncpy(in->src_str, val, sizeof(in->src_str) - 1);
                if (inet_pton(AF_INET, val, &in->src_ip) != 1)
                    log_warn("config: bad unicast_src_ip '%s' in [%s]", val, in->name);
            } else if (strcmp(key, "unicast_peer") == 0) {
                strncpy(in->peer_str, val, sizeof(in->peer_str) - 1);
                if (inet_pton(AF_INET, val, &in->peer_ip) != 1)
                    log_warn("config: bad unicast_peer '%s' in [%s]", val, in->name);
            } else if (strcmp(key, "virtual_router_id") == 0) {
                in->vrid = (uint8_t)atoi(val);
            } else if (strcmp(key, "priority") == 0) {
                in->priority = (uint8_t)atoi(val);
            } else if (strcmp(key, "advert_int") == 0) {
                int sec = atoi(val);
                if (sec < 1) sec = 1;
                in->adver_cs = (uint16_t)(sec * 100);
            } else if (strcmp(key, "preempt") == 0) {
                in->preempt = (strcmp(val, "yes") == 0 || strcmp(val, "1") == 0);
            } else if (strcmp(key, "dhcp_backend") == 0) {
                in->dhcp_backend = dhcp_backend_parse(val);
            } else if (strcmp(key, "alias") == 0) {
                strncpy(in->alias_name, val, sizeof(in->alias_name) - 1);
            } else if (strcmp(key, "vip") == 0) {
                if (in->vip_count >= VRRP_MAX_VIPS) {
                    log_warn("config: max %d vips in [%s]", VRRP_MAX_VIPS, in->name);
                } else if (parse_vip_line(val, &in->vips[in->vip_count]) != 0) {
                    log_err("config: invalid vip '%s' in [%s]", val, in->name);
                    fclose(f);
                    return -1;
                } else {
                    in->vip_count++;
                }
            } else if (strcmp(key, "version") == 0) {
                if (atoi(val) != 3)
                    log_warn("config: [%s] version %s; only VRRPv3 supported",
                             in->name, val);
            }
            /* keepalived-only keys (notify_*, garp_*, debug, ...) ignored */
        } else {
            if (strcmp(key, "priority") == 0)
                cfg->def_priority = (uint8_t)atoi(val);
            else if (strcmp(key, "dhcp_backend") == 0)
                cfg->def_dhcp_backend = dhcp_backend_parse(val);
        }
    }
    fclose(f);

    /* resolve fallbacks */
    for (i = 0; i < cfg->inst_count; i++) {
        vrrp_instance_t *in = &cfg->inst[i];
        if (in->priority == 0)
            in->priority = cfg->def_priority ? cfg->def_priority : DEFAULT_PRIORITY;
        if (in->dhcp_backend == DHCP_BACKEND_INHERIT)
            in->dhcp_backend = cfg->def_dhcp_backend;
        if (in->adver_cs == 0)
            in->adver_cs = DEFAULT_ADVER_CS;
        for (j = 0; j < in->vip_count; j++)
            if (in->vips[j].dev[0] == '\0')
                strncpy(in->vips[j].dev, in->adv_if, IFNAMSIZ - 1);
    }

    /* validate */
    if (cfg->inst_count == 0) {
        log_err("config: at least one [vrrp_instance NAME] section required");
        return -1;
    }
    for (i = 0; i < cfg->inst_count; i++) {
        vrrp_instance_t *in = &cfg->inst[i];
        if (in->vrid < 1) {
            log_err("config: [%s] missing/invalid virtual_router_id", in->name);
            return -1;
        }
        if (in->adv_if[0] == '\0') {
            log_err("config: [%s] missing interface", in->name);
            return -1;
        }
        if (in->src_ip.s_addr == 0 || in->peer_ip.s_addr == 0) {
            log_err("config: [%s] requires unicast_src_ip and unicast_peer", in->name);
            return -1;
        }
        for (j = i + 1; j < cfg->inst_count; j++)
            if (cfg->inst[j].vrid == in->vrid)
                log_warn("config: duplicate virtual_router_id %u ([%s] and [%s])",
                         in->vrid, in->name, cfg->inst[j].name);
    }
    return 0;
}

void config_dump(const config_t *cfg)
{
    int i, j;

    log_info("config: %d instance(s), def_priority=%u def_dhcp=%s",
             cfg->inst_count, cfg->def_priority,
             dhcp_backend_name(cfg->def_dhcp_backend));

    for (i = 0; i < cfg->inst_count; i++) {
        const vrrp_instance_t *in = &cfg->inst[i];
        log_info("config: [%s] vrid=%u prio=%u state=%s if=%s src=%s peer=%s "
                 "adv=%ucs preempt=%d dhcp=%s vips=%d",
                 in->name, in->vrid, in->priority,
                 in->initial == VRRP_STATE_MASTER ? "MASTER" : "BACKUP",
                 in->adv_if, in->src_str, in->peer_str, in->adver_cs,
                 in->preempt, dhcp_backend_name(in->dhcp_backend), in->vip_count);
        for (j = 0; j < in->vip_count; j++) {
            char a[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &in->vips[j].addr, a, sizeof(a));
            log_info("config:   vip %s/%u dev %s%s%s",
                     a, in->vips[j].prefix_len,
                     in->vips[j].dev[0] ? in->vips[j].dev : "(adv_if)",
                     in->vips[j].label[0] ? " label " : "",
                     in->vips[j].label);
        }
    }
}

