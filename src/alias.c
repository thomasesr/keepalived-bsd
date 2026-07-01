#include <sys/socket.h>
#include <netinet/in.h>
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

static int alias_update(const char *action, const char *alias_name,
                        struct in_addr addr)
{
    char ip[INET_ADDRSTRLEN];

    if (!alias_name || alias_name[0] == '\0')
        return 0;                       /* alias management opted out */

    if (!inet_ntop(AF_INET, &addr, ip, sizeof(ip))) {
        log_err("alias: inet_ntop failed for alias %s", alias_name);
        return -1;
    }

    log_info("alias: %s %s %s alias %s",
             action, ip, strcmp(action, "add") == 0 ? "to" : "from", alias_name);
    return run_alias(action, alias_name, ip);
}

int alias_add_vip(const char *alias_name, struct in_addr addr)
{
    return alias_update("add", alias_name, addr);
}

int alias_del_vip(const char *alias_name, struct in_addr addr)
{
    return alias_update("del", alias_name, addr);
}
