#pragma once
#include <string>

struct TroopDataAsset {
    std::string name;
    std::string label;
    std::string description;
    int max_health = 100;
    int resource_cost = 10;
    int attack_damage = 10;
    float attack_range = 1.0f;
    float attack_rate = 1.0f;
    float movement_speed = 1.0f;
    float troop_radius = 1.0f;
    float attack_radius = 1.0f;
    bool is_flying = false;
    bool can_hit_flying = false;

    virtual ~TroopDataAsset() = default;
};
