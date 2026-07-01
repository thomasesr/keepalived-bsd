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

    /* GET /api/keepalived/settings/getVipDevices
     * Real FreeBSD device (igb0, igb0.10) => label, for the VIP "dev" dropdown.
     * VIP dev is a raw BSD ifname (reconfigure.php writes it verbatim), so unlike
     * getInterfaces (OPNsense keys) this maps to the underlying <if>. */
    public function getVipDevicesAction()
    {
        $config = Config::getInstance()->object();
        $devices = [];
        if (isset($config->interfaces)) {
            foreach ($config->interfaces->children() as $ifname => $ifcfg) {
                $bsd = trim((string)($ifcfg->if ?? ''));
                if ($bsd === '') {
                    continue;
                }
                $descr = !empty($ifcfg->descr) ? (string)$ifcfg->descr : strtoupper((string)$ifname);
                $devices[$bsd] = $descr;
            }
        }
        return ['devices' => $devices];
    }

    /* POST /api/keepalived/settings/addInstance */
    public function addInstanceAction()
    {
        return $this->addBase('vrrp_instance', 'vrrp_instances.vrrp_instance');
    }

    /* POST /api/keepalived/settings/delInstance/<uuid> */
    public function delInstanceAction($uuid)
    {
        return $this->delBase('vrrp_instances.vrrp_instance', $uuid);
    }
}
