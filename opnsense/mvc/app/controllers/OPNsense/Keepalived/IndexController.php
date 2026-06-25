<?php

namespace OPNsense\Keepalived;

class IndexController extends \OPNsense\Base\IndexController
{
    public function indexAction()
    {
        $this->view->title = gettext('Keepalived HA');
        $this->view->pick('OPNsense/Keepalived/index');
    }
}
