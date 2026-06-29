CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -D_BSD_SOURCE
LDFLAGS =

PREFIX     = /usr/local
SBINDIR    = $(PREFIX)/sbin
RCDIR      = $(PREFIX)/etc/rc.d
CONFDIR    = $(PREFIX)/etc
OPNSBASE   = $(PREFIX)/opnsense
LIBEXECDIR = $(PREFIX)/libexec/keepalived-bsd

TARGET  = keepalived-bsd

SRCS    = src/main.c \
          src/config.c \
          src/heartbeat.c \
          src/state.c \
          src/iface.c \
          src/dhcp.c \
          src/alias.c \
          src/logger.c

OBJS    = $(SRCS:.c=.o)

.PHONY: all clean install install-rc install-opnsense install-all uninstall uninstall-opnsense

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

.c.o:
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

# ── daemon + config ───────────────────────────────────────────────────────────

install: $(TARGET)
	install -d $(DESTDIR)$(SBINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(SBINDIR)/$(TARGET)
	@if [ ! -f $(DESTDIR)$(CONFDIR)/$(TARGET).conf ]; then \
		install -m 0640 $(TARGET).conf.example $(DESTDIR)$(CONFDIR)/$(TARGET).conf; \
		echo "Installed default config: $(DESTDIR)$(CONFDIR)/$(TARGET).conf"; \
	fi

install-rc:
	install -d $(DESTDIR)$(RCDIR)
	install -m 0755 rc.d/keepalived_bsd $(DESTDIR)$(RCDIR)/keepalived_bsd

# ── OPNSense plugin ───────────────────────────────────────────────────────────

OPNS_MVC   = $(DESTDIR)$(OPNSBASE)/mvc/app
OPNS_SVC   = $(DESTDIR)$(OPNSBASE)/service/conf/actions.d
OPNS_INC   = $(DESTDIR)$(PREFIX)/etc/inc/plugins.inc.d

install-opnsense:
	# plugin registration hook
	install -d $(OPNS_INC)
	install -m 0644 opnsense/etc/inc/plugins.inc.d/keepalived.inc \
	    $(OPNS_INC)/keepalived.inc

	# configd action
	install -d $(OPNS_SVC)
	install -m 0644 opnsense/service/conf/actions.d/actions_keepalived.conf \
	    $(OPNS_SVC)/actions_keepalived.conf

	# model + ACL
	install -d $(OPNS_MVC)/models/OPNsense/Keepalived/ACL
	install -m 0644 opnsense/mvc/app/models/OPNsense/Keepalived/Keepalived.php \
	    $(OPNS_MVC)/models/OPNsense/Keepalived/Keepalived.php
	install -m 0644 opnsense/mvc/app/models/OPNsense/Keepalived/Keepalived.xml \
	    $(OPNS_MVC)/models/OPNsense/Keepalived/Keepalived.xml
	install -m 0644 opnsense/mvc/app/models/OPNsense/Keepalived/ACL/ACL.xml \
	    $(OPNS_MVC)/models/OPNsense/Keepalived/ACL/ACL.xml

	# controllers + menu
	install -d $(OPNS_MVC)/controllers/OPNsense/Keepalived/Api
	install -m 0644 opnsense/mvc/app/controllers/OPNsense/Keepalived/menu.xml \
	    $(OPNS_MVC)/controllers/OPNsense/Keepalived/menu.xml
	install -m 0644 opnsense/mvc/app/controllers/OPNsense/Keepalived/IndexController.php \
	    $(OPNS_MVC)/controllers/OPNsense/Keepalived/IndexController.php
	install -m 0644 opnsense/mvc/app/controllers/OPNsense/Keepalived/Api/ServiceController.php \
	    $(OPNS_MVC)/controllers/OPNsense/Keepalived/Api/ServiceController.php
	install -m 0644 opnsense/mvc/app/controllers/OPNsense/Keepalived/Api/SettingsController.php \
	    $(OPNS_MVC)/controllers/OPNsense/Keepalived/Api/SettingsController.php

	# view
	install -d $(OPNS_MVC)/views/OPNsense/Keepalived
	install -m 0644 opnsense/mvc/app/views/OPNsense/Keepalived/index.volt \
	    $(OPNS_MVC)/views/OPNsense/Keepalived/index.volt

	# dhcp helper scripts
	install -d $(DESTDIR)$(LIBEXECDIR)
	install -m 0755 scripts/dhcp-isc.sh \
	    $(DESTDIR)$(LIBEXECDIR)/dhcp-isc.sh
	install -m 0755 scripts/dhcp-kea.sh \
	    $(DESTDIR)$(LIBEXECDIR)/dhcp-kea.sh
	install -m 0755 scripts/dhcp-dnsmasq.sh \
	    $(DESTDIR)$(LIBEXECDIR)/dhcp-dnsmasq.sh
	install -m 0755 scripts/dhcp-opnsense-toggle.php \
	    $(DESTDIR)$(LIBEXECDIR)/dhcp-opnsense-toggle.php
	install -m 0755 scripts/alias-update.sh \
	    $(DESTDIR)$(LIBEXECDIR)/alias-update.sh
	install -m 0755 scripts/alias-update.php \
	    $(DESTDIR)$(LIBEXECDIR)/alias-update.php

	@echo "OPNsense plugin files installed. Register plugin via +PLUGIN.php if not using ports."
	@if [ -z "$(DESTDIR)" ] && command -v service >/dev/null 2>&1; then \
		echo "Reloading configd..."; \
		service configd restart; \
	else \
		echo "Skipping configd restart (DESTDIR set or service unavailable). Run: service configd restart"; \
	fi

# ── convenience ───────────────────────────────────────────────────────────────

install-all: install install-rc install-opnsense

# ── uninstall ─────────────────────────────────────────────────────────────────

uninstall:
	rm -f $(DESTDIR)$(SBINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(RCDIR)/keepalived_bsd

uninstall-opnsense:
	rm -f  $(OPNS_INC)/keepalived.inc
	rm -rf $(OPNS_MVC)/models/OPNsense/Keepalived
	rm -rf $(OPNS_MVC)/controllers/OPNsense/Keepalived
	rm -rf $(OPNS_MVC)/views/OPNsense/Keepalived
	rm -f  $(OPNS_SVC)/actions_keepalived.conf
	rm -rf $(DESTDIR)$(LIBEXECDIR)
