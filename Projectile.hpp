#pragma once

#include <iostream>
#include <glm/glm.hpp>
#include <vector>

class Projectile{
protected:
    glm::vec2 position;
    glm::vec2 velocity;
    float damage;

public:
    void update(float elapsed);
};