#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include "logger.h"

static int g_foreground = 0;

void log_init(const char *ident, int foreground)
{
    g_foreground = foreground;
    openlog(ident, LOG_PID | LOG_NDELAY, LOG_DAEMON);
}

static void vlog(int priority, const char *fmt, va_list ap)
{
    if (g_foreground) {
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
    } else {
        vsyslog(priority, fmt, ap);
    }
}

void log_info(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vlog(LOG_INFO, fmt, ap); va_end(ap);
}

void log_warn(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vlog(LOG_WARNING, fmt, ap); va_end(ap);
}

void log_err(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vlog(LOG_ERR, fmt, ap); va_end(ap);
}

void log_debug(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt); vlog(LOG_DEBUG, fmt, ap); va_end(ap);
}
