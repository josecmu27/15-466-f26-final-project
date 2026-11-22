#pragma once

#include "Troop.hpp"
#include "World.hpp"
#include "CollectorDataAsset.hpp"

class Collector : public Troop
{
private:
    std::weak_ptr<WaterDrop> carried_water_drop;
public: 
    Collector(glm::vec2 position_, CollectorDataAsset* data_,  Team team_) : 
        Troop(position_, data_, team_) {}


    CollectorDataAsset* get_data_asset() const override { return static_cast<CollectorDataAsset*>(data); }
    bool is_carrying_resource() const { return !carried_water_drop.expired(); }

    
    void update(float elapsed) override;
    void attack() override;
    bool is_attacking() const override;
    void move_to_target(float elapsed) override;
    void determine_target() override;
    void walk_toward(float elapsed, glm::vec2 goal);
    void take_damage(int damage) override;
    
};

