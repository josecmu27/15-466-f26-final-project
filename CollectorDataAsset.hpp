#pragma once

#include "TroopDataAsset.hpp"

struct CollectorDataAsset : public TroopDataAsset {
    float pickup_radius = 1.0f;
    float deposit_radius = 1.2f;
    float splash_damage = 2.5f; // potential on death damage
    float splash_radius = 2.0f; // potential on death damage
};
