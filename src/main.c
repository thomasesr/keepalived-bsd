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
#define PIDFILE      "/var/run/keepalived_bsd.pid"

/* Defined here, declared extern in state.h so the event loop can poll it. */
volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* sigaction without SA_RESTART so a signal interrupts usleep() and the
 * event loop re-checks g_running promptly. */
static void install_signal(int sig, void (*handler)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(sig, &sa, NULL);
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

static void write_pidfile(void)
{
    FILE *f = fopen(PIDFILE, "w");
    if (!f) return;
    fprintf(f, "%d\n", (int)getpid());
    fclose(f);
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

    install_signal(SIGTERM, sig_handler);
    install_signal(SIGINT,  sig_handler);
    install_signal(SIGHUP,  SIG_IGN);

    /* Open the heartbeat socket before daemonizing so a bind failure
     * (port in use, not root) aborts with a non-zero exit the parent
     * process / rc.d can see, instead of a silently broken daemon. */
    if (state_init(&state, &cfg) != 0) {
        log_err("failed to initialize state (heartbeat socket)");
        return 1;
    }

    if (!foreground) {
        daemonize();
        write_pidfile();
    }

    state_run(&state);  /* blocks until SIGTERM/SIGINT, then tears down */

    return 0;
}
