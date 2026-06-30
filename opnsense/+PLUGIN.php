<?php

return [
    'name'    => 'keepalived-bsd',
    'version' => '0.1.21',
    'comment' => 'Keepalived-BSD VRRP-like HA daemon for OPNsense',
    'depends' => [],
    'flat'    => [
        'usr/local/sbin/keepalived-bsd',
        'usr/local/etc/rc.d/keepalived_bsd',
        'usr/local/etc/inc/plugins.inc.d/keepalived.inc',
        'usr/local/libexec/keepalived-bsd',
        'usr/local/opnsense/mvc/app/controllers/OPNsense/Keepalived',
        'usr/local/opnsense/mvc/app/models/OPNsense/Keepalived',
        'usr/local/opnsense/mvc/app/views/OPNsense/Keepalived',
        'usr/local/opnsense/service/conf/actions.d/actions_keepalived.conf',
        'usr/local/opnsense/scripts/OPNsense/Keepalived',
    ],
    'menu' => [
        'Services' => [
            'Keepalived' => [
                'url'         => '/ui/keepalived',
                'visiblename' => 'Keepalived HA',
                'cssclass'    => 'fa fa-exchange-alt',
                'order'       => 450,
            ],
        ],
    ],
    'acl' => [
        'page-services-keepalived' => [
            'description' => 'Access Keepalived HA settings page',
            'pattern'     => 'ui/keepalived*',
        ],
        'page-services-keepalived-api' => [
            'description' => 'Access Keepalived HA API',
            'pattern'     => 'api/keepalived/*',
        ],
    ],
];
