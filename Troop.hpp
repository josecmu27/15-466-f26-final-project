#pragma once

#include <iostream>
#include <memory>
#include <glm/glm.hpp>
#include <vector>


enum Team {
    FRUIT, // Player
    VEGETABLE, // Enemy
    BASKET, // Player's Base
    ENEMY_BASKET // Enemy's Base
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
    float radius;
    bool selected = false;
    Team team;

    float walk_time = 0.0f;
    float attack_time = 0.0f;
    unsigned char pose = 1; // 1 = walk, 2 = attack
    
public:
    Troop(glm::vec2 position_, std::string name_, int health_, int cost_, int damage_,
        float range_, float rate_, float speed_, float radius_, Team team_) : 
        position(position_), name(name_), health(health_), max_health(health_), resource_cost(cost_), attack_damage(damage_),
        attack_range(range_), attack_rate(rate_), movement_speed(speed_), radius(radius_), team(team_) {}

    virtual ~Troop() = default;

    virtual void update(float elapsed) = 0;
    virtual void attack() = 0;
    virtual void move_to_target(float elapsed) = 0;
    virtual void determine_target() = 0;
    virtual bool is_attacking() const { return false; }

    std::string get_name() const { return name; }
    Team get_team() const {return team; }


    int get_health() const { return health; }
    int get_max_health() const { return max_health; }
    void set_health(int new_health) { health = new_health; }
    bool is_dead() const {return health <= 0; }
    bool is_selected() const { return selected; }
    void set_selected(bool s) { selected = s; }

    float get_walk_time() const { return walk_time; }
    void add_walk_time(float elapsed ) { walk_time += elapsed; }
    float get_attack_time() const { return attack_time; }
    void add_attack_time(float elapsed) { attack_time += elapsed; }
    void reset_attack_time() { attack_time = 0.0f; }
    unsigned char get_pose() const { return pose; }
    void set_pose(unsigned char next) { pose = next; }

    glm::vec2 get_position() const { return position; }
    void set_position(glm::vec2& new_position) {position = new_position;}

};