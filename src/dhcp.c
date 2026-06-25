#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dhcp.h"
#include "logger.h"

typedef struct {
    const char *start;
    const char *stop;
} backend_cmds_t;

/* configctl action strings per backend */
static const backend_cmds_t BACKENDS[] = {
    /* DHCP_BACKEND_ISC     */ { "dhcpd restart", "dhcpd stop"    },
    /* DHCP_BACKEND_KEA     */ { "kea restart",   "kea stop"      },
    /* DHCP_BACKEND_DNSMASQ */ { "dnsmasq restart","dnsmasq stop" },
    /* DHCP_BACKEND_NONE    */ { NULL,             NULL            },
};

dhcp_backend_t dhcp_backend_parse(const char *str)
{
    if (strcmp(str, "kea")     == 0) return DHCP_BACKEND_KEA;
    if (strcmp(str, "dnsmasq") == 0) return DHCP_BACKEND_DNSMASQ;
    if (strcmp(str, "none")    == 0) return DHCP_BACKEND_NONE;
    return DHCP_BACKEND_ISC; /* default */
}

const char *dhcp_backend_name(dhcp_backend_t b)
{
    switch (b) {
    case DHCP_BACKEND_ISC:     return "isc";
    case DHCP_BACKEND_KEA:     return "kea";
    case DHCP_BACKEND_DNSMASQ: return "dnsmasq";
    case DHCP_BACKEND_NONE:    return "none";
    }
    return "unknown";
}

static int run_configctl(const char *action)
{
    char cmd[128];
    int  ret;

    snprintf(cmd, sizeof(cmd),
             "/usr/local/sbin/configctl %s", action);
    ret = system(cmd);
    if (ret != 0)
        log_warn("dhcp: configctl %s failed (exit %d)", action, ret);
    return ret == 0 ? 0 : -1;
}

int dhcp_enable(const config_t *cfg)
{
    const char *action = BACKENDS[cfg->dhcp_backend].start;
    if (!action) {
        log_info("dhcp: backend=none, skipping enable");
        return 0;
    }
    log_info("dhcp: enabling backend '%s'", dhcp_backend_name(cfg->dhcp_backend));
    return run_configctl(action);
}

int dhcp_disable(const config_t *cfg)
{
    const char *action = BACKENDS[cfg->dhcp_backend].stop;
    if (!action) {
        log_info("dhcp: backend=none, skipping disable");
        return 0;
    }
    log_info("dhcp: disabling backend '%s'", dhcp_backend_name(cfg->dhcp_backend));
    return run_configctl(action);
}
