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

    /*-- interface dropdown --*/
    function loadInterfaceOptions() {
        $.get('/api/keepalived/settings/getInterfaces', function (data) {
            var sel = $('#new-iface').empty();
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
            $('#dhcp_backend').val(g.dhcp_backend || 'isc');
            loadInterfaces(data.keepalived.interfaces);
        });
    }

    function saveSettings() {
        var payload = { keepalived: {
            general: {
                enabled:      $('#enabled').is(':checked') ? '1' : '0',
                peer:         $('#peer').val(),
                port:         $('#port').val(),
                priority:     $('#priority').val(),
                heartbeat:    $('#heartbeat').val(),
                timeout:      $('#timeout').val(),
                preempt:      $('#preempt').is(':checked') ? '1' : '0',
                dhcp_backend: $('#dhcp_backend').val()
            }
        }};
        $.post('/api/keepalived/settings/set', payload, function (data) {
            if (data.result === 'saved') {
                $('#save-msg').fadeIn().delay(2000).fadeOut();
            }
        });
    }

    /*-- interfaces grid --*/
    function loadInterfaces(ifaces) {
        var tbody = $('#iface-tbody').empty();
        $.each(ifaces.interface || {}, function (uuid, row) {
            tbody.append(
                $('<tr>').append(
                    $('<td>').text(row.iface),
                    $('<td>').text(row.vip),
                    $('<td>').append(
                        $('<button class="btn btn-xs btn-danger">').text('Remove')
                            .click(function () { delInterface(uuid); })
                    )
                )
            );
        });
    }

    function addInterface() {
        var payload = {
            interface: {
                iface: $('#new-iface').val(),
                vip:   $('#new-vip').val()
            }
        };
        $.post('/api/keepalived/settings/addInterface', payload, function () {
            $('#new-iface').val(''); $('#new-vip').val('');
            loadSettings();
        });
    }

    function delInterface(uuid) {
        $.post('/api/keepalived/settings/delInterface/' + uuid, function () {
            loadSettings();
        });
    }

    $('#btn-save').click(saveSettings);
    $('#btn-add-iface').click(addInterface);

    updateStatus();
    loadSettings();
    loadInterfaceOptions();
});
</script>

<div class="content-box" style="padding:16px">
    <div class="row">
        <div class="col-xs-12">
            <h1>{{ lang._('Keepalived HA') }}
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
            <label class="col-sm-3 control-label">{{ lang._('DHCP Backend') }}</label>
            <div class="col-sm-4">
                <select id="dhcp_backend" class="form-control">
                    <option value="isc">{{ lang._('ISC DHCP (dhcpd) — legacy default') }}</option>
                    <option value="kea">{{ lang._('Kea DHCPv4 (os-kea plugin)') }}</option>
                    <option value="dnsmasq">{{ lang._('dnsmasq') }}</option>
                    <option value="none">{{ lang._('None — manual DHCP control') }}</option>
                </select>
            </div>
            <p class="col-sm-3 help-block">{{ lang._('Service started on MASTER, stopped on BACKUP.') }}</p>
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
    <h3>{{ lang._('Interfaces') }}</h3>
    <table class="table table-striped table-condensed">
        <thead><tr>
            <th>{{ lang._('Interface') }}</th>
            <th>{{ lang._('Virtual IP (CIDR)') }}</th>
            <th></th>
        </tr></thead>
        <tbody id="iface-tbody"></tbody>
    </table>
    <div class="row" style="margin-top:8px">
        <div class="col-sm-3">
            <select id="new-iface" class="form-control"></select>
        </div>
        <div class="col-sm-4">
            <input type="text" id="new-vip" class="form-control" placeholder="10.0.0.1/24">
        </div>
        <div class="col-sm-2">
            <button id="btn-add-iface" class="btn btn-sm btn-default">{{ lang._('Add') }}</button>
        </div>
    </div>
</div>
