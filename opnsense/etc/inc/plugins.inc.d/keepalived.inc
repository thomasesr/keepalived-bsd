<?php

function keepalived_services()
{
    return [
        [
            'description' => gettext('Keepalived HA'),
            'pidfile'     => '/var/run/keepalived_bsd.pid',
            'configd'     => [
                'restart' => ['keepalived restart'],
                'start'   => ['keepalived start'],
                'stop'    => ['keepalived stop'],
            ],
            'name' => 'keepalived',
            'id'   => 'keepalived',
        ],
    ];
}

/* Page-access ACLs are defined canonically in the MVC model at
 * mvc/app/models/OPNsense/Keepalived/ACL/ACL.xml. The legacy
 * keepalived_privileges() hook is not read into the page-access map on
 * OPNsense 26.1, so it is intentionally omitted here. */
