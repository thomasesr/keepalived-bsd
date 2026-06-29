<script>
$(function () {

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
        $.post('/api/keepalived/service/' + action, function () {
            setTimeout(updateStatus, 1500);
        });
    }

    $('#btn-start').click(function ()   { svcAction('start');   });
    $('#btn-stop').click(function ()    { svcAction('stop');    });
    $('#btn-restart').click(function () { svcAction('restart'); });

    /*-- interface dropdown (populates modal select) --*/
    function loadInterfaceOptions() {
        $.get('/api/keepalived/settings/getInterfaces', function (data) {
            var sel = $('#modal-iface').empty();
            sel.append($('<option>').val('').text('— select interface —'));
            $.each(data.interfaces || {}, function (key, descr) {
                sel.append($('<option>').val(key).text(descr + ' (' + key + ')'));
            });
        });
    }

    /*-- settings load/save --*/
    function loadSettings() {
        $.get('/api/keepalived/settings/get', function (data) {
            if (!data.keepalived) return;
            var g = data.keepalived.general;
            $('#enabled').prop('checked', g.enabled == '1');
            $('#peer').val(g.peer);
            $('#port').val(g.port);
            $('#priority').val(g.priority);
            $('#heartbeat').val(g.heartbeat);
            $('#timeout').val(g.timeout);
            $('#preempt').prop('checked', g.preempt == '1');
            loadInterfaces(data.keepalived.interfaces);
        });
    }

    function saveSettings() {
        var payload = { keepalived: {
            general: {
                enabled:   $('#enabled').is(':checked') ? '1' : '0',
                peer:      $('#peer').val(),
                port:      $('#port').val(),
                priority:  $('#priority').val(),
                heartbeat: $('#heartbeat').val(),
                timeout:   $('#timeout').val(),
                preempt:   $('#preempt').is(':checked') ? '1' : '0'
            }
        }};
        $.post('/api/keepalived/settings/set', payload, function (data) {
            if (data.result === 'saved') {
                $('#save-msg').fadeIn().delay(2000).fadeOut();
            }
        });
    }

    /* OPNsense OptionField/InterfaceField return {key:{value,selected}} objects */
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

    /*-- interface modal --*/
    var editUuid = null;

    function openModal(uuid, row) {
        editUuid = uuid || null;
        $('#modal-title').text(uuid ? '{{ lang._('Edit Interface') }}' : '{{ lang._('Add Interface') }}');
        $('#modal-iface').val(uuid ? selectedKey(row.iface) : '');
        $('#modal-vip').val(uuid ? row.vip : '');
        $('#modal-alias').val(uuid ? (row.alias || '') : '');
        $('#modal-dhcp-backend').val(uuid ? (selectedKey(row.dhcp_backend) || 'global') : 'global');
        $('#modal-error').hide().text('');
        $('#iface-modal').modal('show');
    }

    function saveModal() {
        var iface = $('#modal-iface').val();
        var vip   = $('#modal-vip').val();
        if (!iface || !vip) {
            $('#modal-error').text('{{ lang._('Interface and Virtual IP are required.') }}').show();
            return;
        }
        var payload = {
            interface: {
                iface:        iface,
                vip:          vip,
                alias:        $('#modal-alias').val(),
                dhcp_backend: $('#modal-dhcp-backend').val()
            }
        };

        function doAdd() {
            $.post('/api/keepalived/settings/addInterface', payload, function (r) {
                if (r.result === 'saved' || r.uuid) {
                    $('#iface-modal').modal('hide');
                    loadSettings();
                } else {
                    $('#modal-error').text(JSON.stringify(r.validations || r)).show();
                }
            });
        }

        if (editUuid) {
            $.post('/api/keepalived/settings/delInterface/' + editUuid, doAdd);
        } else {
            doAdd();
        }
    }

    function delInterface(uuid) {
        if (!confirm('{{ lang._('Remove this interface entry?') }}')) return;
        $.post('/api/keepalived/settings/delInterface/' + uuid, function () {
            loadSettings();
        });
    }

    /*-- interfaces grid --*/
    function loadInterfaces(ifaces) {
        var tbody = $('#iface-tbody').empty();
        $.each(ifaces.interface || {}, function (uuid, row) {
            var backendKey = selectedKey(row.dhcp_backend);
            var backend = backendKey && backendKey !== 'global'
                ? (selectedVal(row.dhcp_backend) || backendKey) : '{{ lang._('global') }}';
            var btnEdit = $('<button class="btn btn-xs btn-default">').html('<i class="fa fa-pencil"></i> {{ lang._('Edit') }}')
                .click((function (u, r) { return function () { openModal(u, r); }; })(uuid, row));
            var btnDel  = $('<button class="btn btn-xs btn-danger" style="margin-left:4px">').html('<i class="fa fa-trash"></i> {{ lang._('Remove') }}')
                .click((function (u) { return function () { delInterface(u); }; })(uuid));
            tbody.append(
                $('<tr>').append(
                    $('<td>').text(selectedVal(row.iface) || selectedKey(row.iface)),
                    $('<td>').text(row.vip),
                    $('<td>').text(row.alias || '—'),
                    $('<td>').text(backend),
                    $('<td style="white-space:nowrap">').append(btnEdit, btnDel)
                )
            );
        });
    }

    $('#btn-save').click(saveSettings);
    $('#btn-add-iface').click(function () { openModal(null, null); });
    $('#modal-btn-save').click(saveModal);

    updateStatus();
    loadSettings();
    loadInterfaceOptions();
});
</script>

<!-- Interface add/edit modal -->
<div class="modal fade" id="iface-modal" tabindex="-1" role="dialog">
    <div class="modal-dialog" role="document">
        <div class="modal-content">
            <div class="modal-header">
                <button type="button" class="close" data-dismiss="modal">&times;</button>
                <h4 class="modal-title" id="modal-title">{{ lang._('Add Interface') }}</h4>
            </div>
            <div class="modal-body">
                <div id="modal-error" class="alert alert-danger" style="display:none"></div>
                <form class="form-horizontal">
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Interface') }}</label>
                        <div class="col-sm-7">
                            <select id="modal-iface" class="form-control"></select>
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('Virtual IP (CIDR)') }}</label>
                        <div class="col-sm-7">
                            <input type="text" id="modal-vip" class="form-control" placeholder="10.0.0.1/24">
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('FW Alias') }}</label>
                        <div class="col-sm-7">
                            <input type="text" id="modal-alias" class="form-control" placeholder="{{ lang._('optional') }}">
                            <span class="help-block">{{ lang._('Firewall alias name to assign the VIP to (letters, digits, underscore).') }}</span>
                        </div>
                    </div>
                    <div class="form-group">
                        <label class="col-sm-4 control-label">{{ lang._('DHCP Backend') }}</label>
                        <div class="col-sm-7">
                            <select id="modal-dhcp-backend" class="form-control">
                                <option value="global">{{ lang._('Global setting') }}</option>
                                <option value="isc">{{ lang._('ISC DHCP (dhcpd)') }}</option>
                                <option value="kea">{{ lang._('Kea DHCPv4') }}</option>
                                <option value="dnsmasq">{{ lang._('dnsmasq') }}</option>
                                <option value="none">{{ lang._('None') }}</option>
                            </select>
                        </div>
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
    <div class="row">
        <div class="col-xs-12">
            <h1>{{ lang._('Keepalived HA') }}
                <small style="margin-left:8px; color:#888">v{{ version }}</small>
                <small id="svc-status" style="margin-left:12px"></small>
            </h1>
            <button id="btn-start"   class="btn btn-sm btn-success">{{ lang._('Start') }}</button>
            <button id="btn-stop"    class="btn btn-sm btn-danger"  style="margin-left:4px">{{ lang._('Stop') }}</button>
            <button id="btn-restart" class="btn btn-sm btn-warning" style="margin-left:4px">{{ lang._('Restart') }}</button>
        </div>
    </div>
</div>

<div class="content-box" style="padding:16px; margin-top:12px">
    <h3>{{ lang._('General Settings') }}</h3>
    <form class="form-horizontal">
        <div class="form-group">
            <label class="col-sm-3 control-label">{{ lang._('Enable') }}</label>
            <div class="col-sm-6"><input type="checkbox" id="enabled"></div>
        </div>
        <div class="form-group">
            <label class="col-sm-3 control-label">{{ lang._('Peer IP') }}</label>
            <div class="col-sm-4"><input type="text" id="peer" class="form-control" placeholder="192.168.1.2"></div>
        </div>
        <div class="form-group">
            <label class="col-sm-3 control-label">{{ lang._('UDP Port') }}</label>
            <div class="col-sm-2"><input type="number" id="port" class="form-control" min="1" max="65535"></div>
        </div>
        <div class="form-group">
            <label class="col-sm-3 control-label">{{ lang._('Priority') }}</label>
            <div class="col-sm-2"><input type="number" id="priority" class="form-control" min="1" max="255"></div>
            <p class="col-sm-4 help-block">{{ lang._('Higher wins MASTER. Set PRIMARY > BACKUP (e.g. 110 vs 100).') }}</p>
        </div>
        <div class="form-group">
            <label class="col-sm-3 control-label">{{ lang._('Heartbeat (s)') }}</label>
            <div class="col-sm-2"><input type="number" id="heartbeat" class="form-control" min="1" max="60"></div>
        </div>
        <div class="form-group">
            <label class="col-sm-3 control-label">{{ lang._('Failover Timeout (s)') }}</label>
            <div class="col-sm-2"><input type="number" id="timeout" class="form-control" min="1" max="60"></div>
        </div>
        <div class="form-group">
            <label class="col-sm-3 control-label">{{ lang._('Preempt') }}</label>
            <div class="col-sm-6"><input type="checkbox" id="preempt">
                <span class="help-block">{{ lang._('Yield MASTER when higher-priority peer returns.') }}</span>
            </div>
        </div>
        <div class="form-group">
            <div class="col-sm-offset-3 col-sm-6">
                <button type="button" id="btn-save" class="btn btn-primary">{{ lang._('Save') }}</button>
                <span id="save-msg" class="text-success" style="display:none;margin-left:8px">{{ lang._('Saved.') }}</span>
            </div>
        </div>
    </form>
</div>

<div class="content-box" style="padding:16px; margin-top:12px">
    <div style="display:flex; align-items:center; justify-content:space-between; margin-bottom:8px">
        <h3 style="margin:0">{{ lang._('Interfaces') }}</h3>
        <button id="btn-add-iface" class="btn btn-sm btn-primary">
            <i class="fa fa-plus"></i> {{ lang._('Add Interface') }}
        </button>
    </div>
    <table class="table table-striped table-condensed">
        <thead><tr>
            <th>{{ lang._('Interface') }}</th>
            <th>{{ lang._('Virtual IP (CIDR)') }}</th>
            <th>{{ lang._('FW Alias') }}</th>
            <th>{{ lang._('DHCP Backend') }}</th>
            <th></th>
        </tr></thead>
        <tbody id="iface-tbody"></tbody>
    </table>
</div>
