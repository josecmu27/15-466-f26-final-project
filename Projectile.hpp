#pragma once

#include <iostream>
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

public:
    Projectile(glm::vec2 position_, glm::vec2 velocity_, int damage_, float radius_, Team team_) :
               position(position_), velocity(velocity_), damage(damage_), radius(radius_), team(team_) {}

    void update(float elapsed);

    glm::vec2 get_position() { return position; }
    glm::vec2 get_velocity() const { return velocity; }

    bool is_inactive() { return !active; }
    void set_inactive() { active = false; }

    float get_radius() { return radius; }
    float get_damage() { return damage; }

    Team get_team() { return team; }
};
