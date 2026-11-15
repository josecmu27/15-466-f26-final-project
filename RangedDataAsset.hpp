#pragma once

#include <string>
#include "TroopDataAsset.hpp"

struct RangedDataAsset : public TroopDataAsset {
    float projectile_speed = 8.0f;
    float projectile_radius = 0.25f;

};