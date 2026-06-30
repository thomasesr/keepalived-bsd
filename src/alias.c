#include <arpa/inet.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "alias.h"
#include "logger.h"

#define ALIAS_SCRIPT "/usr/local/libexec/keepalived-bsd/alias-update.sh"

static int run_alias(const char *action, const char *alias_name, const char *ip)
{
    pid_t  pid;
    int    status;
    char  *argv[5];

    argv[0] = (char *)ALIAS_SCRIPT;
    argv[1] = (char *)action;
    argv[2] = (char *)alias_name;
    argv[3] = (char *)ip;
    argv[4] = NULL;

    pid = fork();
    if (pid < 0) {
        log_err("alias: fork: %s", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execv(ALIAS_SCRIPT, argv);
        _exit(127);
    }
    {   /* retry on EINTR: signal handlers run without SA_RESTART */
        pid_t w;
        do { w = waitpid(pid, &status, 0); } while (w < 0 && errno == EINTR);
        if (w < 0) {
            log_err("alias: waitpid: %s", strerror(errno));
            return -1;
        }
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        log_warn("alias: %s %s %s failed (exit %d)",
                 action, alias_name, ip,
                 WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return -1;
    }
    return 0;
}

int alias_add_vip(const iface_cfg_t *iface)
{
    char ip[INET_ADDRSTRLEN];

    if (iface->alias_name[0] == '\0')
        return 0;

    if (!inet_ntop(AF_INET, &iface->vip_addr, ip, sizeof(ip))) {
        log_err("alias: inet_ntop failed on %s", iface->iface);
        return -1;
    }

    log_info("alias: add %s to alias %s", ip, iface->alias_name);
    return run_alias("add", iface->alias_name, ip);
}

int alias_del_vip(const iface_cfg_t *iface)
{
    char ip[INET_ADDRSTRLEN];

    if (iface->alias_name[0] == '\0')
        return 0;

    if (!inet_ntop(AF_INET, &iface->vip_addr, ip, sizeof(ip))) {
        log_err("alias: inet_ntop failed on %s", iface->iface);
        return -1;
    }

    log_info("alias: del %s from alias %s", ip, iface->alias_name);
    return run_alias("del", iface->alias_name, ip);
}
