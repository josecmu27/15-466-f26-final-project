#pragma once

#include <iostream>
#include <glm/glm.hpp>

class Projectile{
protected:
    glm::vec2 position;
    glm::vec2 velocity;
    int damage;
    float radius;
    bool active = true;

public:
    Projectile(glm::vec2 position_, glm::vec2 velocity_, int damage_, float radius_) :
               position(position_), velocity(velocity_), damage(damage_), radius(radius_) {}

    virtual void update(float elapsed);

    glm::vec2 get_position() { return position; }

    bool is_inactive() { return !active; }
    void set_inactive() { active = false; }

    float get_radius() { return radius; }
    float get_damage() { return damage; }
};
