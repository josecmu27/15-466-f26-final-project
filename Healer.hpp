#pragma once

#include "Troop.hpp"
#include "HealerDataAsset.hpp"

class Healer : public Troop
{
public: 
    Healer(glm::vec2 position_, HealerDataAsset* data_,  Team team_) : 
        Troop(position_, data_, team_) {}

    
    float attack_timer = (data) ? data->attack_rate : 0.0f;
    int shot_cycle = -1;

    std::weak_ptr<Troop> target_troop;

    void update(float elapsed) override;
    void attack() override;
    bool is_attacking() const override;
    HealerDataAsset* get_data_asset() const override { return static_cast<HealerDataAsset*>(data); }
    void move_to_target(float elapsed) override;
    void determine_target() override;
    void walk_toward(float elapsed, glm::vec2 goal);
    void set_target(std::shared_ptr<Troop> target) { target_troop = target; }
};
