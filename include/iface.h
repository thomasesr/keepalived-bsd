#ifndef IFACE_H
#define IFACE_H

#include "vrrp.h"   /* vip_t */

/* Add/remove a virtual IP on its host interface (vip->dev, resolved to the
 * instance advert interface by config_load when omitted). FreeBSD ioctls. */
int iface_vip_add(const vip_t *vip);
int iface_vip_del(const vip_t *vip);

#endif /* IFACE_H */
