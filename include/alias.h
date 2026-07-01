#ifndef ALIAS_H
#define ALIAS_H

#include <netinet/in.h>

/* Add/remove a VIP address to/from an OPNsense firewall alias. A NULL or empty
 * alias_name is a no-op (the instance opted out of alias management). */
int alias_add_vip(const char *alias_name, struct in_addr addr);
int alias_del_vip(const char *alias_name, struct in_addr addr);

#endif /* ALIAS_H */
