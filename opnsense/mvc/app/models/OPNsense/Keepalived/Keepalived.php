<?php

namespace OPNsense\Keepalived;

use OPNsense\Base\BaseModel;
use OPNsense\Base\Messages\Message;

class Keepalived extends BaseModel
{
    /**
     * Per-field ranges are enforced by Keepalived.xml. This adds the
     * cross-field rule the daemon requires: the failover timeout must be
     * strictly greater than the heartbeat interval, otherwise a single
     * missed heartbeat can trigger spurious failover.
     */
    public function performValidation($validateFullModel = false)
    {
        $messages = parent::performValidation($validateFullModel);

        $hb = (int)(string)$this->general->heartbeat;
        $to = (int)(string)$this->general->timeout;
        if ($hb > 0 && $to > 0 && $to <= $hb) {
            $messages->appendMessage(new Message(
                gettext('Failover timeout must be greater than the heartbeat interval ' .
                        '(recommended at least 3× the heartbeat).'),
                $this->general->timeout->__reference
            ));
        }

        return $messages;
    }
}
