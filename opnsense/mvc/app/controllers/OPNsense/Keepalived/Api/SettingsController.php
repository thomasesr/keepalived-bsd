<?php

namespace OPNsense\Keepalived\Api;

use OPNsense\Base\ApiMutableModelControllerBase;
use OPNsense\Core\Config;

class SettingsController extends ApiMutableModelControllerBase
{
    protected static $internalModelName  = 'keepalived';
    protected static $internalModelClass = '\OPNsense\Keepalived\Keepalived';

    /* GET /api/keepalived/settings/get */
    public function getAction()
    {
        return $this->getBase('keepalived');
    }

    /* POST /api/keepalived/settings/set */
    public function setAction()
    {
        return $this->setBase('keepalived');
    }

    /* POST /api/keepalived/settings/addInterface */
    public function addInterfaceAction()
    {
        return $this->addBase('interface', $this->getModel()->interfaces->interface);
    }

    /* POST /api/keepalived/settings/delInterface/<uuid> */
    public function delInterfaceAction($uuid)
    {
        return $this->delBase('interfaces', $this->getModel()->interfaces->interface, $uuid);
    }
}
