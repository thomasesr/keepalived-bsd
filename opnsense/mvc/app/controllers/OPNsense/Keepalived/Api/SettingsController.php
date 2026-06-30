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
            /* performValidation() returns an iterable of Message objects, each
             * exposing getField()/getMessage(). The previous isValid()/
             * getMessages() calls do not exist on Message and fatally errored
             * the request whenever validation actually failed (e.g. an
             * out-of-range value), so the UI got a 500 with no message. */
            $valMsgs = $mdl->performValidation();
            $valErrs = [];
            foreach ($valMsgs as $msg) {
                $valErrs[$msg->getField()] = $msg->getMessage();
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

    /* GET /api/keepalived/settings/getInterfaces */
    public function getInterfacesAction()
    {
        $config = Config::getInstance()->object();
        $ifaces = [];
        if (isset($config->interfaces)) {
            foreach ($config->interfaces->children() as $ifname => $ifcfg) {
                $descr = !empty($ifcfg->descr) ? (string)$ifcfg->descr : strtoupper((string)$ifname);
                $ifaces[(string)$ifname] = $descr;
            }
        }
        return ['interfaces' => $ifaces];
    }

    /* POST /api/keepalived/settings/addInterface */
    public function addInterfaceAction()
    {
        return $this->addBase('interface', 'interfaces.interface');
    }

    /* POST /api/keepalived/settings/delInterface/<uuid> */
    public function delInterfaceAction($uuid)
    {
        /* delBase($path, $uuids) — only two args. The previous three-arg form
         * ('interface', 'interfaces.interface', $uuid) was a fatal call that
         * silently broke interface deletion (and edit, which deletes first). */
        return $this->delBase('interfaces.interface', $uuid);
    }
}
