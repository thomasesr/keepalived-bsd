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
        return ['keepalived' => $this->getModel()->getNodes()];
    }

    /* POST /api/keepalived/settings/set */
    public function setAction()
    {
        $result = ['result' => 'failed'];
        if ($this->request->isPost()) {
            $mdl  = $this->getModel();
            $post = $this->request->getPost('keepalived');
            if (!empty($post['general'])) {
                $mdl->general->setNodes($post['general']);
            }
            $valMsgs = $mdl->performValidation();
            $valErrs = [];
            foreach ($valMsgs as $msg) {
                if (!$msg->isValid()) {
                    foreach ($msg->getMessages() as $m) {
                        $valErrs[$msg->getField()] = $m->getMessage();
                    }
                }
            }
            if (empty($valErrs)) {
                $mdl->serializeToConfig();
                Config::getInstance()->save();
                $result = ['result' => 'saved'];
            } else {
                $result = ['result' => 'failed', 'validations' => $valErrs];
            }
        }
        return $result;
    }

    /* POST /api/keepalived/settings/addInterface */
    public function addInterfaceAction()
    {
        return $this->addBase('interface', 'interfaces.interface');
    }

    /* POST /api/keepalived/settings/delInterface/<uuid> */
    public function delInterfaceAction($uuid)
    {
        return $this->delBase('interface', 'interfaces.interface', $uuid);
    }
}
