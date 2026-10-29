#pragma once

#include <iostream>
#include <memory>
#include <glm/glm.hpp>
#include <vector>
#include "Pathfinder.hpp"

enum Team {
    FRUIT, // Player
    VEGETABLE, // Enemy
    BASKET // Player's Base
};

class Troop {
protected:
    glm::vec2 position;
    std::string name;
    int health;
    int max_health;
    int resource_cost;
    int attack_damage;
    float attack_range;
    float attack_rate;
    float movement_speed;
    Team team;
    
public:
    Troop(glm::vec2 position_, std::string name_, int health_, int cost_, int damage_,
        float range_, float rate_, float speed_, Team team_) : 
        position(position_), name(name_), health(health_), max_health(health_), resource_cost(cost_), attack_damage(damage_),
        attack_range(range_), attack_rate(rate_), movement_speed(speed_), team(team_) {}

    virtual ~Troop() = default;

    virtual void update(float elapsed, std::vector<std::shared_ptr<Troop>> &troops, Pathfinder const &paths) = 0;
    virtual void attack() = 0;
    virtual void move_to_target(float elapsed, std::vector<std::shared_ptr<Troop>> &troops, Pathfinder const &paths) = 0;
    virtual void determine_target(std::vector<std::shared_ptr<Troop>> &troops) = 0;

    std::string get_name() const { return name; }
    Team get_team() const {return team; }


    int get_health() const { return health; }
    int get_max_health() const { return max_health; }
    void set_health(int new_health) { health = new_health; }
    bool is_dead() const {return health <= 0; }

    glm::vec2 get_position() const { return position; }
    void set_position(glm::vec2& new_position) {position = new_position;}

};