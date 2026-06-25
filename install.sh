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

# ── config migration ──────────────────────────────────────────────────────────
# Called after the active .conf is confirmed to exist.
# Adds missing keys with safe defaults. Never removes or overwrites existing values.
config_upgrade() {
    local conf="$1"
    local changed=0

    # Helper: append a key=value line under [global] if key is absent
    add_global_key() {
        local key="$1" val="$2" comment="$3"
        if ! grep -qE "^[[:space:]]*${key}[[:space:]]*=" "${conf}"; then
            printf '\n# Added by install.sh upgrade\n# %s\n%s = %s\n' \
                "${comment}" "${key}" "${val}" >> "${conf}"
            echo "    + added: ${key} = ${val}"
            changed=1
        fi
    }

    # v0.1.0+: required global keys
    add_global_key dhcp_backend isc  "DHCP backend: isc | kea | dnsmasq | none"
    add_global_key heartbeat    1    "Seconds between heartbeat sends"
    add_global_key timeout      3    "Seconds of peer silence before MASTER promotion"
    add_global_key preempt      yes  "Yield MASTER to higher-priority peer: yes | no"

    # per-iface dhcp_backend (v0.1.7+) is optional — daemon inherits global if absent
    # No migration needed; omitting it is valid.

    if [ "${changed}" -eq 1 ]; then
        echo "    Config updated — review ${conf} before restarting."
    else
        echo "    Config is current — no migration needed."
    fi
}

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
    echo "--- config migration"
    config_upgrade "${CONF}/keepalived-bsd.conf"
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
