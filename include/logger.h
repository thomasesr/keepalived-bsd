#ifndef LOGGER_H
#define LOGGER_H

void log_init(const char *ident, int foreground);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_err(const char *fmt, ...);
void log_debug(const char *fmt, ...);

#endif /* LOGGER_H */
