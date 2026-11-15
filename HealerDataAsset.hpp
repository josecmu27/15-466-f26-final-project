#pragma once

#include "TroopDataAsset.hpp"

struct HealerDataAsset : public TroopDataAsset {
    int heal_amount = 15;
    float heal_radius = 2.5f;
};