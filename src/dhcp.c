#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "dhcp.h"
#include "logger.h"

#define SCRIPTS_DIR "/usr/local/libexec/keepalived-bsd"

/* One helper script per backend — each handles config toggle + daemon reload */
static const char *SCRIPT[] = {
    /* DHCP_BACKEND_ISC     */ SCRIPTS_DIR "/dhcp-isc.sh",
    /* DHCP_BACKEND_KEA     */ SCRIPTS_DIR "/dhcp-kea.sh",
    /* DHCP_BACKEND_DNSMASQ */ SCRIPTS_DIR "/dhcp-dnsmasq.sh",
    /* DHCP_BACKEND_NONE    */ NULL,
};

dhcp_backend_t dhcp_backend_parse(const char *str)
{
    if (strcmp(str, "kea")     == 0) return DHCP_BACKEND_KEA;
    if (strcmp(str, "dnsmasq") == 0) return DHCP_BACKEND_DNSMASQ;
    if (strcmp(str, "none")    == 0) return DHCP_BACKEND_NONE;
    return DHCP_BACKEND_ISC;
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

/* FreeBSD iface names are alphanumeric only (em0, igb1, vtnet0 ...) */
static int iface_safe(const char *iface)
{
    const char *p = iface;
    if (!*p || strlen(p) >= IFNAMSIZ) return 0;
    for (; *p; p++)
        if (!isalnum((unsigned char)*p))
            return 0;
    return 1;
}

static int run_script(const char *script, const char *action, const char *iface)
{
    pid_t  pid;
    int    status;
    char  *argv[4];

    argv[0] = (char *)script;
    argv[1] = (char *)action;
    argv[2] = (char *)iface;
    argv[3] = NULL;

    pid = fork();
    if (pid < 0) {
        log_err("dhcp: fork: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execv(script, argv);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        log_err("dhcp: waitpid: %s", strerror(errno));
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        log_warn("dhcp: %s %s %s failed (exit %d)",
                 script, action, iface,
                 WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return -1;
    }
    return 0;
}

int dhcp_enable_iface(const iface_cfg_t *iface, dhcp_backend_t backend)
{
    if (backend == DHCP_BACKEND_NONE) {
        log_info("dhcp: backend=none, skip enable on %s", iface->iface);
        return 0;
    }
    if (!iface_safe(iface->iface)) {
        log_err("dhcp: unsafe iface name '%s'", iface->iface);
        return -1;
    }
    log_info("dhcp: enable %s on %s", dhcp_backend_name(backend), iface->iface);
    return run_script(SCRIPT[backend], "enable", iface->iface);
}

int dhcp_disable_iface(const iface_cfg_t *iface, dhcp_backend_t backend)
{
    if (backend == DHCP_BACKEND_NONE) {
        log_info("dhcp: backend=none, skip disable on %s", iface->iface);
        return 0;
    }
    if (!iface_safe(iface->iface)) {
        log_err("dhcp: unsafe iface name '%s'", iface->iface);
        return -1;
    }
    log_info("dhcp: disable %s on %s", dhcp_backend_name(backend), iface->iface);
    return run_script(SCRIPT[backend], "disable", iface->iface);
}
