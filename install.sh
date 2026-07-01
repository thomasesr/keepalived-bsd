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
# The VRRPv3 rewrite is a HARD BREAK from the old KALV [global]+[iface] format:
# the daemon rejects legacy configs and there is no auto-migration (the whole-node
# model became per-VRID [vrrp_instance] blocks — no safe mechanical rewrite).
# We never mutate the operator's .conf; we detect the old shape and tell them to
# port it. A current VRRPv3 config needs no changes ([global] keys are optional).
config_upgrade() {
    local conf="$1"

    if grep -qE '^[[:space:]]*\[iface[[:space:]]+[^]]+\]' "${conf}"; then
        echo "    WARNING: legacy [iface ...] config detected — INCOMPATIBLE with VRRPv3." >&2
        echo "             The daemon will refuse to start until this file is rewritten as" >&2
        echo "             [vrrp_instance NAME] blocks. There is no automatic migration." >&2
        echo "             Template: ${CONF}/keepalived-bsd.conf.example" >&2
        return 0
    fi

    # Old KALV global keys but no instances = stale / half-ported file.
    if ! grep -qE '^[[:space:]]*\[vrrp_instance[[:space:]]+[^]]+\]' "${conf}" \
       && grep -qE '^[[:space:]]*(heartbeat|timeout|peer|port)[[:space:]]*=' "${conf}"; then
        echo "    WARNING: old KALV-style keys but no [vrrp_instance] blocks — rewrite required." >&2
        echo "             Template: ${CONF}/keepalived-bsd.conf.example" >&2
        return 0
    fi

    echo "    Config format is current (VRRPv3) — no migration needed."
}

# ── paths / state ───────────────────────────────────────────────────────────
PIDFILE=/var/run/keepalived_bsd.pid
BIN="${SBIN}/keepalived-bsd"
PKG_NAME=os-keepalived

KA_WAS_RUNNING=0
KA_WAS_ENABLED=0

# ── stop any running daemon BEFORE purge/reinstall ──────────────────────────────
# Replacing the binary under a live process, or purging the rc.d script while the
# daemon still holds VIPs/DHCP, leaves the box split-brained. Stop first.
# Prefer a clean SIGTERM so the daemon runs its MASTER->BACKUP cleanup (drops VIPs,
# disables DHCP, sends goodbye packet) before exit; escalate to SIGKILL only if it
# refuses. Record prior run/enable state so we can restore intent at the end.
stop_running_daemon() {
    if command -v sysrc >/dev/null 2>&1; then
        if [ "$(sysrc -n keepalived_bsd_enable 2>/dev/null | tr 'A-Z' 'a-z')" = "yes" ]; then
            KA_WAS_ENABLED=1
        fi
    fi

    _pid=""
    if [ -f "${PIDFILE}" ]; then
        _pid="$(tr -dc '0-9' < "${PIDFILE}" 2>/dev/null)"
    fi
    if [ -z "${_pid}" ] || ! kill -0 "${_pid}" 2>/dev/null; then
        _pid="$(pgrep -f "${BIN}" 2>/dev/null | head -n1)"
    fi

    if [ -z "${_pid}" ]; then
        echo "    no running keepalived-bsd detected"
        return 0
    fi

    KA_WAS_RUNNING=1
    echo "    stopping running keepalived-bsd (pid ${_pid})"

    # rc.d path first (runs poststop cleanup); rc.d may already be absent.
    if [ -x "${RCD}/keepalived_bsd" ]; then
        service keepalived_bsd onestop >/dev/null 2>&1 || true
    fi

    # Escalate: SIGTERM once, wait up to ~10s, then SIGKILL.
    _n=0
    while kill -0 "${_pid}" 2>/dev/null; do
        [ "${_n}" -eq 0 ] && kill -TERM "${_pid}" 2>/dev/null || true
        _n=$((_n + 1))
        if [ "${_n}" -ge 10 ]; then
            echo "    daemon ignored SIGTERM — sending SIGKILL" >&2
            kill -KILL "${_pid}" 2>/dev/null || true
            break
        fi
        sleep 1
    done

    if kill -0 "${_pid}" 2>/dev/null; then
        echo "    WARNING: could not stop pid ${_pid}" >&2
    else
        echo "    stopped"
    fi
    rm -f "${PIDFILE}"
    unset _pid _n
}

# ── validation ──────────────────────────────────────────────────────────────
# Config issues = warnings (user edits the conf later). Shipped PHP/XML syntax
# errors = fatal: a broken model/controller takes down the whole OPNsense UI.
validate_conf() {
    local conf="$1"
    [ -f "${conf}" ] || return 0
    local _k _v

    # Legacy KALV format — the VRRPv3 daemon rejects [iface] and will not start.
    if grep -qE "^[[:space:]]*\[iface[[:space:]]+[^]]+\]" "${conf}"; then
        echo "    WARNING: legacy [iface ...] block present — VRRPv3 daemon will reject this config" >&2
    fi

    # At least one instance, or the daemon has nothing to run.
    if ! grep -qE "^[[:space:]]*\[vrrp_instance[[:space:]]+[^]]+\]" "${conf}"; then
        echo "    WARNING: no [vrrp_instance NAME] block — daemon would manage nothing" >&2
    fi

    # Coarse required-key presence (daemon requires each per [vrrp_instance]).
    # File-wide check only — the daemon does the authoritative per-section validation.
    for _k in interface unicast_src_ip unicast_peer virtual_router_id; do
        grep -qE "^[[:space:]]*${_k}[[:space:]]*=" "${conf}" \
            || echo "    WARNING: no '${_k} =' found — required in each [vrrp_instance]" >&2
    done

    grep -hoE "^[[:space:]]*dhcp_backend[[:space:]]*=[[:space:]]*[A-Za-z]+" "${conf}" 2>/dev/null \
      | sed -E 's/.*=[[:space:]]*//' \
      | while IFS= read -r _v; do
            case "${_v}" in
                none|dnsmasq|kea|isc) ;;
                *) echo "    WARNING: unknown dhcp_backend '${_v}' (want none|dnsmasq|kea|isc)" >&2 ;;
            esac
        done
    return 0
}

validate_plugin_files() {
    local rc=0 _f _x
    if command -v php >/dev/null 2>&1; then
        for _f in $(find "${OPNSBASE}/mvc" "${OPNSBASE}/scripts" "${LIBEXEC}" \
                         -name '*.php' 2>/dev/null) \
                  /usr/local/etc/inc/plugins.inc.d/keepalived.inc; do
            [ -f "${_f}" ] || continue
            if ! php -l "${_f}" >/dev/null 2>&1; then
                echo "ERROR: PHP syntax error in ${_f}" >&2
                php -l "${_f}" >&2 || true
                rc=1
            fi
        done
    fi
    # Malformed model/menu/acl XML is silently ignored by OPNsense — the menu and
    # settings just vanish with no error. Catch it here instead.
    for _x in $(find "${OPNSBASE}/mvc" -name '*.xml' 2>/dev/null); do
        if command -v xmllint >/dev/null 2>&1; then
            xmllint --noout "${_x}" 2>/dev/null || { echo "ERROR: malformed XML ${_x}" >&2; rc=1; }
        elif command -v php >/dev/null 2>&1; then
            php -r 'exit(@simplexml_load_file($argv[1])===false?1:0);' "${_x}" 2>/dev/null \
                || { echo "ERROR: malformed XML ${_x}" >&2; rc=1; }
        fi
    done
    return ${rc}
}

# ── pkg registration: make plugin appear on System > Firmware > Plugins ───────
# That page enumerates installed pkg packages (os-*). A tarball install is
# invisible there until registered. Build a manifest from +PLUGIN.php, package
# the live files, and add them. NON-FATAL: registration failure must not abort
# the install — the plugin still works via its MVC files; it just won't list.
register_pkg() {
    command -v pkg >/dev/null 2>&1 || { echo "    pkg not found — skipping Firmware-page registration"; return 0; }
    command -v php >/dev/null 2>&1 || { echo "    php not found — skipping Firmware-page registration"; return 0; }
    local plugin_php="${OPNSBASE}/+PLUGIN.php"
    [ -f "${plugin_php}" ] || { echo "    +PLUGIN.php missing — skipping Firmware-page registration" >&2; return 0; }

    local ver comment workdir manifest plist created
    ver="$(php -r '$m=include $argv[1]; echo $m["version"]??"0";' "${plugin_php}" 2>/dev/null)" || ver=""
    comment="$(php -r '$m=include $argv[1]; echo $m["comment"]??"OPNsense plugin";' "${plugin_php}" 2>/dev/null)" || comment=""
    [ -n "${ver}" ] || ver="0"
    [ -n "${comment}" ] || comment="Keepalived-BSD HA daemon for OPNsense"

    workdir="$(mktemp -d /tmp/ka-pkg.XXXXXX)" || return 0
    manifest="${workdir}/+MANIFEST"
    plist="${workdir}/plist"

    # Every installed file, absolute paths (pkg create -r /).
    {
        echo "${BIN}"
        echo "${RCD}/keepalived_bsd"
        echo "/usr/local/etc/inc/plugins.inc.d/keepalived.inc"
        echo "${OPNSBASE}/service/conf/actions.d/actions_keepalived.conf"
        find "${LIBEXEC}" -type f 2>/dev/null
        find "${OPNSBASE}/mvc/app/models/OPNsense/Keepalived" -type f 2>/dev/null
        find "${OPNSBASE}/mvc/app/controllers/OPNsense/Keepalived" -type f 2>/dev/null
        find "${OPNSBASE}/mvc/app/views/OPNsense/Keepalived" -type f 2>/dev/null
        find "${OPNSBASE}/scripts/OPNsense/Keepalived" -type f 2>/dev/null
    } | sort -u > "${plist}"

    # UCL manifest; "-" = let pkg compute each file's checksum.
    {
        printf 'name: %s\n'        "${PKG_NAME}"
        printf 'version: "%s"\n'   "${ver}"
        printf 'origin: opnsense/%s\n' "${PKG_NAME}"
        printf 'comment: "%s"\n'   "${comment}"
        printf 'desc: "%s"\n'      "${comment}"
        printf 'maintainer: thomas@richterconsulting.com.br\n'
        printf 'www: https://github.com/thomasesr/keepalived-bsd\n'
        printf 'prefix: /usr/local\n'
        printf 'categories: [net]\n'
        printf 'files: {\n'
        while IFS= read -r _f; do
            [ -f "${_f}" ] && printf '  "%s": "-",\n' "${_f}"
        done < "${plist}"
        printf '}\n'
    } > "${manifest}"

    set +e
    pkg create -M "${manifest}" -r / -o "${workdir}" >/dev/null 2>&1
    created="$(ls "${workdir}/${PKG_NAME}"-*.pkg "${workdir}/${PKG_NAME}"-*.txz 2>/dev/null | head -n1)"
    if [ -n "${created}" ] && pkg add -f "${created}" >/dev/null 2>&1; then
        echo "    registered ${PKG_NAME}-${ver} (now listed on Firmware > Plugins)"
    else
        echo "    WARNING: pkg registration failed — plugin still works, but won't list on Firmware > Plugins" >&2
    fi
    set -e
    rm -rf "${workdir}"
    unset _f
}

# ── clear MVC caches so a new menu/ACL/model is picked up ──────────────────────
# The real cache is mvc/app/cache (compiled volt + ACL json), NOT /tmp.
clear_mvc_caches() {
    rm -f "${OPNSBASE}"/mvc/app/cache/*.json 2>/dev/null || true
    rm -f "${OPNSBASE}"/mvc/app/cache/*.php  2>/dev/null || true
    rm -rf /tmp/opnsense_cache 2>/dev/null || true
}

echo "==> keepalived-bsd ${RELEASE_TAG}"

# ── stop running daemon ─────────────────────────────────────────────────────
# Must happen BEFORE purge: we replace the binary and remove the rc.d script.
echo "--- stop running daemon"
stop_running_daemon

# ── purge old installation ────────────────────────────────────────────────────
# Remove all managed files before reinstalling so stale files cannot linger.
# Active keepalived-bsd.conf is intentionally excluded.
echo "--- purge old files"
rm -rf "${OPNSBASE}/mvc/app/models/OPNsense/Keepalived"
rm -rf "${OPNSBASE}/mvc/app/controllers/OPNsense/Keepalived"
rm -rf "${OPNSBASE}/mvc/app/views/OPNsense/Keepalived"
rm -f  "${OPNSBASE}/service/conf/actions.d/actions_keepalived.conf"
rm -f  /usr/local/etc/inc/plugins.inc.d/keepalived.inc
rm -rf "${LIBEXEC}"

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

# plugins.inc.d lands outside OPNSBASE — move it to the correct path
install -d /usr/local/etc/inc/plugins.inc.d
if [ -f "${OPNSBASE}/etc/inc/plugins.inc.d/keepalived.inc" ]; then
    install -m 0644 "${OPNSBASE}/etc/inc/plugins.inc.d/keepalived.inc" \
        /usr/local/etc/inc/plugins.inc.d/keepalived.inc
    rm -rf "${OPNSBASE}/etc"
fi

if [ ! -f "${OPNSBASE}/service/conf/actions.d/actions_keepalived.conf" ]; then
    echo "ERROR: actions_keepalived.conf not found after extraction — configd actions will not work" >&2
    echo "       Check that the release tar is intact and re-run install.sh" >&2
    exit 1
fi

# ── validate before activating ──────────────────────────────────────────────
# Hard-fail on broken shipped PHP/XML (would take down the whole UI); only warn
# on config-content issues (the user edits the conf afterwards).
echo "--- validate"
if ! validate_plugin_files; then
    echo "ERROR: shipped plugin files failed validation — aborting before activation" >&2
    exit 1
fi
validate_conf "${CONF}/keepalived-bsd.conf"
echo "    ok"

# ── register with pkg so it appears on System > Firmware > Plugins ──────────
echo "--- pkg registration"
register_pkg

# ── activate: clear MVC caches, reload configd ──────────────────────────────
clear_mvc_caches
service configd restart
echo "    configd restarted"
# Reload php-fpm workers to clear opcache/realpath cache so new menu.xml is found.
# USR2 = graceful reload of all workers; works regardless of php version suffix in rc name.
_fpm_reloaded=0
for _pid in /var/run/php-fpm.pid /var/run/php*.pid; do
    [ -f "$_pid" ] || continue
    kill -USR2 "$(cat "$_pid")" 2>/dev/null && _fpm_reloaded=1 && break
done
if [ "$_fpm_reloaded" -eq 0 ]; then
    # fallback: try versioned rc.d service names (e.g. php83_fpm, php82_fpm)
    for _svc in $(ls /usr/local/etc/rc.d/ 2>/dev/null | grep -E '^php[0-9]*[-_]fpm$'); do
        service "$_svc" onerestart 2>/dev/null && _fpm_reloaded=1 && break
    done
fi
[ "$_fpm_reloaded" -eq 1 ] && echo "    php-fpm reloaded" || echo "    php-fpm not found — reload manually if menu does not appear"
unset _fpm_reloaded _pid _svc

# ── restore service state ───────────────────────────────────────────────────
# Re-enable + restart ONLY if the daemon was running before we started, so a
# reinstall does not silently change the user's intended state.
if [ "${KA_WAS_ENABLED}" -eq 1 ] && command -v sysrc >/dev/null 2>&1; then
    sysrc keepalived_bsd_enable=YES >/dev/null 2>&1 || true
fi
if [ "${KA_WAS_RUNNING}" -eq 1 ]; then
    echo "--- restart (was running before install)"
    if service keepalived_bsd onestart >/dev/null 2>&1; then
        echo "    started"
    else
        echo "    WARNING: restart failed — start manually: service keepalived_bsd start" >&2
    fi
fi

# ── cleanup ───────────────────────────────────────────────────────────────────
rm -f /tmp/keepalived-bsd \
      /tmp/keepalived_bsd \
      /tmp/keepalived-bsd.conf.example \
      /tmp/keepalived-scripts.tar.gz \
      /tmp/opnsense-plugin.tar.gz

echo ""
echo "==> Done."
if [ "${KA_WAS_RUNNING}" -eq 1 ]; then
    echo "    Daemon was reinstalled and restarted."
    echo "    - Settings: https://<opnsense>/ui/keepalived"
    echo "    - Plugins:  System > Firmware > Plugins (${PKG_NAME})"
else
    echo "    1. Edit ${CONF}/keepalived-bsd.conf"
    echo "    2. sysrc keepalived_bsd_enable=YES"
    echo "    3. service keepalived_bsd start"
    echo "    4. https://<opnsense>/ui/keepalived  (menu: Services > Keepalived)"
fi
