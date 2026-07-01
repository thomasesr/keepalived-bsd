<?php

namespace OPNsense\Keepalived\Api;

use OPNsense\Base\ApiControllerBase;
use OPNsense\Core\Backend;

class ServiceController extends ApiControllerBase
{
    /* POST /api/keepalived/service/start */
    public function startAction()
    {
        $backend = new Backend();
        $response = $backend->configdRun('keepalived start');
        return ['response' => trim($response)];
    }

    /* POST /api/keepalived/service/stop */
    public function stopAction()
    {
        $backend = new Backend();
        $response = $backend->configdRun('keepalived stop');
        return ['response' => trim($response)];
    }

    /* POST /api/keepalived/service/restart */
    public function restartAction()
    {
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

    /* GET /api/keepalived/service/statusDetail
     * Returns the daemon's atomic JSON status file (per-VRRP-instance state),
     * decoded to an array. Empty/absent file yields an empty instance list so
     * the UI can flag the data stale rather than error. */
    public function statusDetailAction()
    {
        $backend = new Backend();
        $raw     = trim($backend->configdRun('keepalived status_detail'));
        $data    = json_decode($raw, true);
        if (!is_array($data)) {
            $data = ['written' => 0, 'instances' => []];
        }
        return $data;
    }

    /* POST /api/keepalived/service/apply
     * Generates /usr/local/etc/keepalived-bsd.conf from the stored model
     * and restarts the daemon if enabled. */
    public function applyAction()
    {
        $backend  = new Backend();
        $response = trim($backend->configdRun('keepalived reconfigure'));
        return ['result' => 'ok', 'response' => $response];
    }
}
