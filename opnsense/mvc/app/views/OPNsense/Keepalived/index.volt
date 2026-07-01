<script>
$(function () {

    var pendingChanges = false;
    function markPending() { pendingChanges = true; $('#apply-banner').slideDown(); }
    function clearPending() { pendingChanges = false; $('#apply-banner').slideUp(); }

    /* OPNsense OptionField/InterfaceField return {key:{value,selected}} objects. */
    function selectedKey(obj) {
        if (typeof obj !== 'object' || obj === null) return String(obj);
        var k = '';
        $.each(obj, function (key, v) { if (v && v.selected == 1) { k = key; return false; } });
        return k;
    }
    function selectedVal(obj) {
        if (typeof obj !== 'object' || obj === null) return String(obj);
        var v = '';
        $.each(obj, function (key, val) { if (val && val.selected == 1) { v = val.value || key; return false; } });
        return v;
    }

    /*-- service status --*/
    function updateStatus() {
        $.get('/api/keepalived/service/status', function (data) {
            var badge = data.status === 'running'
                ? '<span class="label label-success">running</span>'
                : '<span class="label label-danger">stopped</span>';
            $('#svc-status').html(badge);
        });
    }
    function svcAction(action) {
        $.post('/api/keepalived/service/' + action, function () { setTimeout(updateStatus, 1500); });
    }
    $('#btn-start').click(function ()   { svcAction('start');   });
    $('#btn-stop').click(function ()    { svcAction('stop');    });
    $('#btn-restart').click(function () { svcAction('restart'); });

    /*-- interface dropdown (populates modal select) --*/
    function loadInterfaceOptions() {
        $.get('/api/keepalived/settings/getInterfaces', function (data) {
            var sel = $('#modal-interface').empty();
            sel.append($('<option>').val('').text('— {{ lang._('select interface') }} —'));
            $.each(data.interfaces || {}, function (key, descr) {
                sel.append($('<option>').val(key).text(descr + ' (' + key + ')'));
            });
        });
    }

    function showGeneralError(msg) {
        if (msg) { $('#general-error').html(msg).show(); }
        else     { $('#general-error').hide().empty(); }
    }

    /*-- settings load (general + instances) --*/
    function loadSettings() {
        $.get('/api/keepalived/settings/get', function (data) {
            if (!data.keepalived) return;
            var g = data.keepalived.general;
            $('#enabled').prop('checked', g.enabled == '1');
            $('#gpriority').val(g.priority);
            $('#gdhcp').val(selectedKey(g.dhcp_backend) || 'none');
            loadInstances((data.keepalived.vrrp_instances || {}).vrrp_instance || {});
        });
    }

    /*-- general save --*/
    function saveSettings(successCb, failCb) {
        var payload = { keepalived: { general: {
            enabled:      $('#enabled').is(':checked') ? '1' : '0',
            priority:     $('#gpriority').val(),
            dhcp_backend: $('#gdhcp').val()
        }}};
        $.post('/api/keepalived/settings/set', payload, function (data) {
            if (data.result === 'saved') {
                if (successCb) { successCb(); }
                else { markPending(); $('#save-msg').fadeIn().delay(2000).fadeOut(); }
            } else {
                var v = data.validations || {};
                var msgs = $.map(v, function (m) { return m; });
                showGeneralError(msgs.length ? msgs.join('<br>') : '{{ lang._('Save failed.') }}');
                if (failCb) failCb();
            }
        }).fail(function () {
            showGeneralError('{{ lang._('Save failed — server error.') }}');
            if (failCb) failCb();
        });
    }
    $('#btn-save').click(function () { saveSettings(); });

    /*-- Apply: save then reconfigure --*/
    $('#btn-apply').click(function () {
        var $btn = $(this);
        function restore() { $btn.prop('disabled', false).text('{{ lang._('Apply') }}'); }
        $btn.prop('disabled', true).text('{{ lang._('Applying…') }}');
        saveSettings(function () {
            $.post('/api/keepalived/service/apply', function (data) {
                clearPending(); restore(); updateStatus();
                var msg = data.response || '{{ lang._('Done.') }}';
                $('#apply-result').text(msg).fadeIn().delay(3000).fadeOut();
            }).fail(function () {
                restore();
                alert('{{ lang._('Apply failed — check system log.') }}');
            });
        }, restore);
    });

    /*-- instances grid --*/
    function vipLines(raw) {
        return $.trim(raw || '').split(/[\r\n,]+/)
            .map(function (s) { return $.trim(s); }).filter(Boolean);
    }

    function loadInstances(rows) {
        var tbody = $('#inst-tbody').empty();
        var n = 0;
        $.each(rows, function (uuid, row) {
            n++;
            var iface = selectedVal(row.interface) || selectedKey(row.interface) || '—';
            var btnEdit = $('<button class="btn btn-xs btn-default">').html('<i class="fa fa-pencil"></i> {{ lang._('Edit') }}')
                .click((function (u, r) { return function () { openModal(u, r); }; })(uuid, row));
            var btnClone = $('<button class="btn btn-xs btn-default" style="margin-left:4px">').html('<i class="fa fa-clone"></i> {{ lang._('Clone') }}')
                .click((function (r) { return function () { openModal(null, r, true); }; })(row));
            var btnDel  = $('<button class="btn btn-xs btn-danger" style="margin-left:4px">').html('<i class="fa fa-trash"></i> {{ lang._('Remove') }}')
                .click((function (u) { return function () { delInstance(u); }; })(uuid));
            tbody.append($('<tr>').append(
                $('<td>').text(row.name),
                $('<td>').text(selectedKey(row.state) || 'BACKUP'),
                $('<td>').text(iface),
                $('<td>').text(row.virtual_router_id),
                $('<td>').text(row.priority),
                $('<td>').text(row.advert_int + 's'),
                $('<td style="white-space:pre-line">').text(vipLines(row.vip).join('\n') || '—'),
                $('<td>').text(selectedVal(row.dhcp_backend) || '{{ lang._('none') }}'),
                $('<td style="white-space:nowrap">').append(btnEdit, btnClone, btnDel)
            ));
        });
        if (n === 0) {
            tbody.append($('<tr>').append(
                $('<td colspan="9" class="text-muted">').text('{{ lang._('No VRRP instances configured.') }}')));
        }
    }

    /*-- instance add/edit modal --*/
    var editUuid = null;
    /* uuid set => edit (delete-then-add on save). isClone => add mode but
       pre-populate from row so a copy can be tweaked before saving. Model has a
       UniqueConstraint on name + virtual_router_id, so the name is suffixed
       "-copy" to avoid an instant duplicate; user still edits the VRID. */
    function openModal(uuid, row, isClone) {
        editUuid = uuid || null;
        var populate = !!uuid || !!isClone;
        row = row || {};
        $('#modal-title').text(uuid ? '{{ lang._('Edit VRRP Instance') }}'
            : (isClone ? '{{ lang._('Clone VRRP Instance') }}' : '{{ lang._('Add VRRP Instance') }}'));
        $('#modal-name').val(populate ? ((row.name || '') + (isClone ? '-copy' : '')) : '');
        $('#modal-state').val(populate ? (selectedKey(row.state) || 'BACKUP') : 'BACKUP');
        $('#modal-interface').val(populate ? selectedKey(row.interface) : '');
        $('#modal-src').val(populate ? row.unicast_src_ip : '');
        $('#modal-peer').val(populate ? row.unicast_peer : '');
        $('#modal-vrid').val(populate ? row.virtual_router_id : '');
        $('#modal-priority').val(populate ? row.priority : '100');
        $('#modal-advint').val(populate ? row.advert_int : '5');
        $('#modal-preempt').prop('checked', populate ? (row.preempt == '1' || selectedKey(row.preempt) == '1') : true);
        $('#modal-vip').val(populate ? vipLines(row.vip).join('\n') : '');
        $('#modal-alias').val(populate ? (row.alias || '') : '');
        $('#modal-dhcp').val(populate ? (selectedKey(row.dhcp_backend) || 'none') : 'none');
        $('#modal-error').hide().text('');
        $('#inst-modal').modal('show');
    }

    function fmtValidations(v) {
        if (typeof v !== 'object' || v === null) return String(v);
        var msgs = $.map(v, function (m) { return m; });
        return msgs.length ? msgs.join('<br>') : '{{ lang._('Save failed.') }}';
    }

    function saveModal() {
        var name = $.trim($('#modal-name').val());
        var iface = $('#modal-interface').val();
        var src = $.trim($('#modal-src').val());
        var peer = $.trim($('#modal-peer').val());
        var vrid = $.trim($('#modal-vrid').val());
        var vip = $.trim($('#modal-vip').val());
        if (!name || !iface || !src || !peer || !vrid || !vip) {
            $('#modal-error').text('{{ lang._('Name, interface, source/peer IP, VRID and at least one VIP are required.') }}').show();
            return;
        }
        var payload = { vrrp_instance: {
            name: name,
            state: $('#modal-state').val(),
            interface: iface,
            unicast_src_ip: src,
            unicast_peer: peer,
            virtual_router_id: vrid,
            priority: $.trim($('#modal-priority').val()),
            advert_int: $.trim($('#modal-advint').val()),
            preempt: $('#modal-preempt').is(':checked') ? '1' : '0',
            vip: vip,
            alias: $.trim($('#modal-alias').val()),
            dhcp_backend: $('#modal-dhcp').val()
        }};
        function doAdd() {
            $.post('/api/keepalived/settings/addInstance', payload, function (r) {
                if (r.result === 'saved' || r.uuid) {
                    $('#inst-modal').modal('hide'); markPending(); loadSettings();
                } else {
                    $('#modal-error').html(fmtValidations(r.validations || r)).show();
                }
            }).fail(function () {
                $('#modal-error').text('{{ lang._('Save failed — server error.') }}').show();
            });
        }
        /* Edit = delete-then-add; the delete must complete first. */
        if (editUuid) { $.post('/api/keepalived/settings/delInstance/' + editUuid, doAdd); }
        else { doAdd(); }
    }

    function delInstance(uuid) {
        if (!confirm('{{ lang._('Remove this VRRP instance?') }}')) return;
        $.post('/api/keepalived/settings/delInstance/' + uuid, function () {
            markPending(); loadSettings();
        });
    }

    $('#btn-add-inst').click(function () { openModal(null, null); });
    $('#modal-btn-save').click(saveModal);

    /*-- VRRP status table (2s poller) --*/
    function fmtEpoch(ts) {
        ts = parseInt(ts, 10);
        if (!ts) return '—';
        return new Date(ts * 1000).toLocaleString();
    }
    function loadVrrpStatus() {
        $.get('/api/keepalived/service/statusDetail', function (data) {
            var tbody = $('#vrrp-status-tbody').empty();
            var insts = (data && data.instances) || [];
            var stale = !(data && data.written) ||
                (Math.floor(Date.now() / 1000) - parseInt(data.written, 10)) > 5;
            $('#vrrp-stale').toggle(stale && insts.length > 0);
            if (!insts.length) {
                tbody.append($('<tr>').append(
                    $('<td colspan="8" class="text-muted">').text('{{ lang._('No status yet — start the service.') }}')));
                return;
            }
            $.each(insts, function (i, it) {
                var isMaster = String(it.state).toUpperCase() === 'MASTER';
                var badge = $('<span class="label">')
                    .addClass(isMaster ? 'label-success' : 'label-default').text(it.state);
                tbody.append($('<tr>').append(
                    $('<td>').text(it.name),
                    $('<td>').text(it.interface),
                    $('<td>').text(it.priority),
                    $('<td>').append(badge),
                    $('<td>').text(it.initial),
                    $('<td>').text(it.probes_sent),
                    $('<td>').text(it.probes_received),
                    $('<td>').text(fmtEpoch(it.last_transition))
                ));
            });
        });
    }
    setInterval(loadVrrpStatus, 2000);

    updateStatus();
    loadSettings();
    loadInterfaceOptions();
    loadVrrpStatus();
});
</script>

<!-- pending-changes banner -->
<div id="apply-banner" class="alert alert-warning" style="display:none; margin:0; border-radius:0; border-left:0; border-right:0">
    <i class="fa fa-exclamation-triangle"></i>
    {{ lang._('Unsaved changes — click Apply to write the config file and restart the service.') }}
</div>

<!-- VRRP instance add/edit modal -->
<div class="modal fade" id="inst-modal" tabindex="-1" role="dialog">
    <div class="modal-dialog" role="document">
        <div class="modal-content">
            <div class="modal-header">
                <button type="button" class="close" data-dismiss="modal">&times;</button>
                <h4 class="modal-title" id="modal-title">{{ lang._('Add VRRP Instance') }}</h4>
            </div>
            <div class="modal-body">
                <div id="modal-error" class="alert alert-danger" style="display:none"></div>
                <form class="form-horizontal">
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Name') }}</label>
                        <div class="col-sm-7"><input type="text" id="modal-name" class="form-control" placeholder="master"></div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Initial State') }}</label>
                        <div class="col-sm-7"><select id="modal-state" class="form-control">
                            <option value="BACKUP">BACKUP</option>
                            <option value="MASTER">MASTER</option>
                        </select></div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Advert Interface') }}</label>
                        <div class="col-sm-7"><select id="modal-interface" class="form-control"></select></div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Unicast Source IP') }}</label>
                        <div class="col-sm-7"><input type="text" id="modal-src" class="form-control" placeholder="192.168.1.1"></div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Unicast Peer IP') }}</label>
                        <div class="col-sm-7"><input type="text" id="modal-peer" class="form-control" placeholder="192.168.1.3"></div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Virtual Router ID') }}</label>
                        <div class="col-sm-3"><input type="number" id="modal-vrid" class="form-control" min="1" max="255"></div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Priority') }}</label>
                        <div class="col-sm-3"><input type="number" id="modal-priority" class="form-control" min="1" max="255"></div>
                        <p class="col-sm-5 help-block">{{ lang._('Higher wins MASTER (e.g. 110 vs peer 100).') }}</p>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Advert Interval (s)') }}</label>
                        <div class="col-sm-3"><input type="number" id="modal-advint" class="form-control" min="1" max="255"></div>
                        <p class="col-sm-5 help-block">{{ lang._('Must match the peer (RFC 5798).') }}</p>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Preempt') }}</label>
                        <div class="col-sm-7"><input type="checkbox" id="modal-preempt">
                            <span class="help-block">{{ lang._('Reclaim MASTER when a higher-priority instance returns.') }}</span>
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Virtual IPs') }}</label>
                        <div class="col-sm-7"><textarea id="modal-vip" class="form-control" rows="3" placeholder="192.165.1.2/24 dev igb0"></textarea>
                            <span class="help-block">{{ lang._('One per line: ADDR/prefix [dev IF] [label L]. dev defaults to the advert interface.') }}</span>
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('FW Alias') }}</label>
                        <div class="col-sm-7"><input type="text" id="modal-alias" class="form-control" placeholder="{{ lang._('optional') }}">
                            <span class="help-block">{{ lang._('Firewall alias to assign the VIP to (letters, digits, underscore).') }}</span>
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('DHCP Backend') }}</label>
                        <div class="col-sm-7"><select id="modal-dhcp" class="form-control">
                            <option value="none">{{ lang._('None') }}</option>
                            <option value="dnsmasq">{{ lang._('dnsmasq (default on 26.1)') }}</option>
                            <option value="kea">{{ lang._('Kea DHCPv4') }}</option>
                            <option value="isc">{{ lang._('ISC DHCP (dhcpd) — legacy, requires os-isc-dhcp') }}</option>
                        </select></div>
                    </div>
                </form>
            </div>
            <div class="modal-footer">
                <button type="button" class="btn btn-default" data-dismiss="modal">{{ lang._('Cancel') }}</button>
                <button type="button" class="btn btn-primary" id="modal-btn-save">{{ lang._('Save') }}</button>
            </div>
        </div>
    </div>
</div>

<div class="content-box" style="padding:16px">
    <div class="row"><div class="col-xs-12">
        <h1>{{ lang._('Keepalived HA') }}
            <small style="margin-left:8px; color:#888">v{{ version }} · VRRPv3</small>
            <small id="svc-status" style="margin-left:12px"></small>
        </h1>
        <button id="btn-start"   class="btn btn-sm btn-success">{{ lang._('Start') }}</button>
        <button id="btn-stop"    class="btn btn-sm btn-danger"  style="margin-left:4px">{{ lang._('Stop') }}</button>
        <button id="btn-restart" class="btn btn-sm btn-warning" style="margin-left:4px">{{ lang._('Restart') }}</button>
    </div></div>
</div>

<div class="content-box" style="padding:16px; margin-top:12px">
    <h3>{{ lang._('General Settings') }}</h3>
    <div id="general-error" class="alert alert-danger" style="display:none"></div>
    <form class="form-horizontal">
        <div class="form-group">
            <label class="col-sm-3 control-label">{{ lang._('Enable') }}</label>
            <div class="col-sm-6"><input type="checkbox" id="enabled"></div>
        </div>
        <div class="form-group"><div class="col-sm-offset-3 col-sm-9">
            <div class="alert alert-warning" style="margin-bottom:0">
                <i class="fa fa-exclamation-triangle"></i>
                {{ lang._('Enabling this service unloads the kernel CARP module (carp.ko) each time the daemon starts. VRRP and CARP both use IP protocol 112, and the kernel CARP handler would otherwise swallow every inbound VRRP advert before it reaches this daemon. This is incompatible with OPNsense CARP virtual IPs — do not run CARP-based HA on this host.') }}
            </div>
        </div></div>
        <div class="form-group">
            <label class="col-sm-3 control-label">{{ lang._('Default Priority') }}</label>
            <div class="col-sm-2"><input type="number" id="gpriority" class="form-control" min="1" max="254"></div>
            <p class="col-sm-6 help-block">{{ lang._('Fallback for instances that omit a priority.') }}</p>
        </div>
        <div class="form-group">
            <label class="col-sm-3 control-label">{{ lang._('Default DHCP Backend') }}</label>
            <div class="col-sm-3"><select id="gdhcp" class="form-control">
                <option value="none">{{ lang._('None') }}</option>
                <option value="dnsmasq">{{ lang._('dnsmasq (default on 26.1)') }}</option>
                <option value="kea">{{ lang._('Kea DHCPv4') }}</option>
                <option value="isc">{{ lang._('ISC DHCP (dhcpd) — legacy') }}</option>
            </select></div>
        </div>
        <div class="form-group"><div class="col-sm-offset-3 col-sm-9">
            <button type="button" id="btn-save" class="btn btn-default">{{ lang._('Save') }}</button>
            <button type="button" id="btn-apply" class="btn btn-primary" style="margin-left:6px">{{ lang._('Apply') }}</button>
            <span id="save-msg"     class="text-muted"   style="display:none; margin-left:8px">{{ lang._('Saved — click Apply to activate.') }}</span>
            <span id="apply-result" class="text-success" style="display:none; margin-left:8px"></span>
        </div></div>
    </form>
</div>

<div class="content-box" style="padding:16px; margin-top:12px">
    <div style="display:flex; align-items:center; justify-content:space-between; margin-bottom:8px">
        <h3 style="margin:0">{{ lang._('VRRP Instances') }}</h3>
        <button id="btn-add-inst" class="btn btn-sm btn-primary">
            <i class="fa fa-plus"></i> {{ lang._('Add Instance') }}
        </button>
    </div>
    <div class="table-responsive"><table class="table table-striped table-condensed">
        <thead><tr>
            <th>{{ lang._('Name') }}</th>
            <th>{{ lang._('State') }}</th>
            <th>{{ lang._('Interface') }}</th>
            <th>{{ lang._('VRID') }}</th>
            <th>{{ lang._('Priority') }}</th>
            <th>{{ lang._('Advert') }}</th>
            <th>{{ lang._('Virtual IPs') }}</th>
            <th>{{ lang._('DHCP') }}</th>
            <th></th>
        </tr></thead>
        <tbody id="inst-tbody"></tbody>
    </table></div>
</div>

<div class="content-box" style="padding:16px; margin-top:12px">
    <h3 style="margin-top:0">{{ lang._('VRRP Status') }}
        <span id="vrrp-stale" class="label label-warning" style="display:none; margin-left:8px; font-weight:normal">{{ lang._('stale') }}</span>
    </h3>
    <p class="text-muted">{{ lang._('This overview shows the current status of the VRRP instances on this device.') }}</p>
    <div class="table-responsive"><table class="table table-striped table-condensed">
        <thead><tr>
            <th>{{ lang._('Name') }}</th>
            <th>{{ lang._('Interface') }}</th>
            <th>{{ lang._('Priority') }}</th>
            <th>{{ lang._('Active State') }}</th>
            <th>{{ lang._('Initial State') }}</th>
            <th>{{ lang._('Probes Sent') }}</th>
            <th>{{ lang._('Probes Received') }}</th>
            <th>{{ lang._('Last Transition') }}</th>
        </tr></thead>
        <tbody id="vrrp-status-tbody"></tbody>
    </table></div>
</div>
