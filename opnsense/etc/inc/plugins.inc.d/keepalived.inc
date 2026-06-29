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

function keepalived_privileges()
{
    return [
        'page-services-keepalived' => [
            'descr' => gettext('Keepalived HA'),
            'match' => ['ui/keepalived*', 'api/keepalived/*'],
        ],
    ];
}
