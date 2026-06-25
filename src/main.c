#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"
#include "logger.h"
#include "state.h"

#define DEFAULT_CONF "/usr/local/etc/keepalived-bsd.conf"

static volatile int g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid > 0) exit(0);          /* parent exits */
    setsid();
    /* redirect stdio to /dev/null */
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [-c config] [-f] [-h]\n"
            "  -c config  config file (default: %s)\n"
            "  -f         foreground, log to stderr\n"
            "  -h         this help\n",
            argv0, DEFAULT_CONF);
}

int main(int argc, char *argv[])
{
    const char *confpath  = DEFAULT_CONF;
    int         foreground = 0;
    int         opt;
    config_t    cfg;
    state_ctx_t state;

    while ((opt = getopt(argc, argv, "c:fh")) != -1) {
        switch (opt) {
        case 'c': confpath   = optarg; break;
        case 'f': foreground = 1;      break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    log_init("keepalived-bsd", foreground);

    if (config_load(confpath, &cfg) != 0) {
        log_err("failed to load config: %s", confpath);
        return 1;
    }
    config_dump(&cfg);

    signal(SIGTERM, sig_handler);
    signal(SIGINT,  sig_handler);
    signal(SIGHUP,  SIG_IGN);

    if (!foreground)
        daemonize();

    state_init(&state, &cfg);
    state_run(&state);  /* blocks until signal */

    return 0;
}
