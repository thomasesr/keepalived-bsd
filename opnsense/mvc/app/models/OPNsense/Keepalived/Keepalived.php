<?php

namespace OPNsense\Keepalived;

use OPNsense\Base\BaseModel;

/**
 * VRRPv3 HA model. Per-field ranges and the name/VRID uniqueness
 * constraints are declared in Keepalived.xml; no cross-field PHP rule is
 * required for the per-instance model.
 */
class Keepalived extends BaseModel
{
}
