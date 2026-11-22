#pragma once

#include <iostream>

#include "Troop.hpp"
#include "Projectile.hpp"
#include "Map.hpp"
#include "Pathfinder.hpp"

// based on https://www.geeksforgeeks.org/cpp/implementation-of-singleton-class-in-cpp/
// likely to be replaced 
struct PlayMode;

struct WaterDrop {
    glm::vec2 position;
    int value;
    bool being_carried;
    bool collected;
};

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
    std::vector<std::shared_ptr<WaterDrop>> water_drops;

    int player_resources = 0;
    int director_resources = 0;
    int max_resources = 10;

    float match_time_duration = 180.0f;
    float match_time_remaining = 180.0f;
    float regeneration_interval = 5.0f;
    int base_regeneration_amount = 1;
    float regeneration_timer = 1.0f;


    Map map;
    Pathfinder paths{map};
    PlayMode* play_mode = nullptr;

    void update(float elapsed);
    void clear();

    void handle_collisions();
    void cleanup_defeated_troops();
    void cleanup_inactive_projectiles();
    void cleanup_collected_resources();

    void add_resources(Team team, int amount);

    int get_player_resources() { return player_resources; }
    void add_player_resources(int amount) { 
        player_resources = std::min(max_resources, player_resources + amount); 
    }

    void subtract_player_resources(int amount) { 
        player_resources = std::max(0, player_resources - amount);
    }

    
    int get_director_resources() { return director_resources; }
    void add_director_resources(int amount) { 
        director_resources = std::min(max_resources, director_resources + amount); 
    }

    void subtract_director_resources(int amount) { 
        director_resources = std::max(0, director_resources - amount);
    }

    bool is_endgame() const { return match_time_remaining <= 60.0f; }
    
};
