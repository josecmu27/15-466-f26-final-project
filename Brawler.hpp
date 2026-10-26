#pragma once

#include "Troop.hpp"

class Brawler : public Troop
{
public: 
    Brawler(glm::vec2 position_, std::string name_, int health_, int cost_, int damage_,
        float range_, float rate_, float speed_, Team team_) : 
        Troop(position_, name_, health_, cost_, damage_, range_, rate_, speed_, team_) {}

    
    float attack_timer = attack_rate;

    std::weak_ptr<Troop> target_troop;

    void update(float elapsed) override;
    void attack() override;
    void move_to_target(float elapsed) override;
    void determine_target() override;
    void set_target(std::shared_ptr<Troop> target) { target_troop = target; }
};