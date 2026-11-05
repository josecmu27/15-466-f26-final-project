#pragma once

#include <iostream>

#include "Troop.hpp"
#include "Projectile.hpp"
#include "Pathfinder.hpp"

// based on https://www.geeksforgeeks.org/cpp/implementation-of-singleton-class-in-cpp/
// likely to be replaced 
class World {
private:
    static World* instance;
    World() {}

public:
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    static World* get_world()
    {
        if (instance == nullptr)
        {
            instance = new World();
        }
        return instance;
    }

    std::vector<std::shared_ptr<Troop>> troops;
    std::vector<std::shared_ptr<Projectile>> projectiles;
    
    Map map;
    Pathfinder paths{map};

    void update(float elapsed);

    void handle_collisions();
    void cleanup_defeated_troops();
};
