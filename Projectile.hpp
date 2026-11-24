#pragma once

#include <iostream>
#include <limits>
#include "Troop.hpp"
#include <glm/glm.hpp>

class Projectile{
protected:
    glm::vec2 position;
    glm::vec2 velocity;
    int damage;
    float radius;
    Team team;
    bool active = true;
    glm::vec2 origin;
    float max_range; // a shot that misses goes inactive after flying this far

public:
    Projectile(glm::vec2 position_, glm::vec2 velocity_, int damage_, float radius_, Team team_,
               float max_range_ = std::numeric_limits<float>::infinity()) :
               position(position_), velocity(velocity_), damage(damage_), radius(radius_), team(team_),
               origin(position_), max_range(max_range_) {}

    void update(float elapsed);

    glm::vec2 get_position() { return position; }
    glm::vec2 get_velocity() const { return velocity; }

    bool is_inactive() { return !active; }
    void set_inactive() { active = false; }

    float get_radius() { return radius; }
    int get_damage() { return damage; }

    Team get_team() { return team; }
};
