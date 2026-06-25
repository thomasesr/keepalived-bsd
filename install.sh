#!/bin/sh
# keepalived-bsd installer for OPNsense / FreeBSD
# Always reinstalls all files. Safe to re-run — active .conf is never overwritten.
# Usage: sh install.sh [release-tag]

set -e

RELEASE_TAG="${1:-{{RELEASE_TAG}}}"
BASE_URL="https://github.com/thomasesr/keepalived-bsd/releases/download/${RELEASE_TAG}"

SBIN=/usr/local/sbin
RCD=/usr/local/etc/rc.d
CONF=/usr/local/etc
LIBEXEC=/usr/local/libexec/keepalived-bsd
OPNSBASE=/usr/local/opnsense

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: must run as root" >&2
    exit 1
fi

echo "==> keepalived-bsd ${RELEASE_TAG}"

# ── daemon binary ─────────────────────────────────────────────────────────────
echo "--- daemon binary"
fetch -q -o /tmp/keepalived-bsd "${BASE_URL}/keepalived-bsd"
install -m 0755 /tmp/keepalived-bsd "${SBIN}/keepalived-bsd"

# ── rc.d script ───────────────────────────────────────────────────────────────
echo "--- rc.d script"
fetch -q -o /tmp/keepalived_bsd "${BASE_URL}/keepalived_bsd"
install -d "${RCD}"
install -m 0755 /tmp/keepalived_bsd "${RCD}/keepalived_bsd"

# ── config example (always update); active .conf only created if missing ──────
echo "--- config"
fetch -q -o /tmp/keepalived-bsd.conf.example "${BASE_URL}/keepalived-bsd.conf.example"
install -m 0640 /tmp/keepalived-bsd.conf.example "${CONF}/keepalived-bsd.conf.example"
if [ ! -f "${CONF}/keepalived-bsd.conf" ]; then
    install -m 0640 /tmp/keepalived-bsd.conf.example "${CONF}/keepalived-bsd.conf"
    echo "    installed default config — edit before starting"
else
    echo "    existing config preserved: ${CONF}/keepalived-bsd.conf"
fi

# ── DHCP helper scripts ───────────────────────────────────────────────────────
echo "--- DHCP helper scripts"
fetch -q -o /tmp/keepalived-scripts.tar.gz "${BASE_URL}/scripts.tar.gz"
install -d "${LIBEXEC}"
tar -xzf /tmp/keepalived-scripts.tar.gz -C "${LIBEXEC}"
chmod 0755 "${LIBEXEC}"/*.sh 2>/dev/null || true
chmod 0755 "${LIBEXEC}"/*.php 2>/dev/null || true

# ── OPNsense MVC plugin + configd actions ─────────────────────────────────────
echo "--- OPNsense plugin"
fetch -q -o /tmp/opnsense-plugin.tar.gz "${BASE_URL}/opnsense-plugin.tar.gz"
install -d "${OPNSBASE}"
tar -xzf /tmp/opnsense-plugin.tar.gz -C "${OPNSBASE}"
rm -rf /tmp/opnsense_cache
service configd restart
echo "    configd restarted"

# ── cleanup ───────────────────────────────────────────────────────────────────
rm -f /tmp/keepalived-bsd \
      /tmp/keepalived_bsd \
      /tmp/keepalived-bsd.conf.example \
      /tmp/keepalived-scripts.tar.gz \
      /tmp/opnsense-plugin.tar.gz

echo ""
echo "==> Done."
echo "    1. Edit ${CONF}/keepalived-bsd.conf"
echo "    2. sysrc keepalived_bsd_enable=YES"
echo "    3. service keepalived_bsd start"
echo "    4. https://<opnsense>/ui/keepalived"
