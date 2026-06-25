<?php

namespace OPNsense\Keepalived\Api;

use OPNsense\Base\ApiControllerBase;
use OPNsense\Core\Backend;

class ServiceController extends ApiControllerBase
{
    /* POST /api/keepalived/service/start */
    public function startAction()
    {
        $this->sessionClose();
        $backend = new Backend();
        $response = $backend->configdRun('keepalived start');
        return ['response' => trim($response)];
    }

    /* POST /api/keepalived/service/stop */
    public function stopAction()
    {
        $this->sessionClose();
        $backend = new Backend();
        $response = $backend->configdRun('keepalived stop');
        return ['response' => trim($response)];
    }

    /* POST /api/keepalived/service/restart */
    public function restartAction()
    {
        $this->sessionClose();
        $backend = new Backend();
        $response = $backend->configdRun('keepalived restart');
        return ['response' => trim($response)];
    }

    /* GET /api/keepalived/service/status */
    public function statusAction()
    {
        $backend  = new Backend();
        $response = trim($backend->configdRun('keepalived status'));
        $running  = (stripos($response, 'is running') !== false);
        return [
            'status'   => $running ? 'running' : 'stopped',
            'response' => $response,
        ];
    }
}
