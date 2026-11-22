#pragma once

#include <iostream>
#include <memory>
#include <glm/glm.hpp>
#include <vector>
#include "TroopDataAsset.hpp"


class Troop;

enum Team {
    FRUIT, // Player
    VEGETABLE, // Enemy
    BASKET, // Player's Base
    ENEMY_BASKET // Enemy's Base
};

/* troop selection (note that the baskets are not included)*/
enum TroopType {
	CHERRY_BRAWLER, 
	BANANA_RANGED,
	PINEAPPLE_HEALER,
	ORANGE_FLYER,
	BLUEBERRY_COLLECTOR
};

enum class OrderType {
    None, Move, Attack
};

struct Order {
    OrderType type = OrderType::None;
    glm::vec2 move_target = glm::vec2(0.0f);
    std::weak_ptr<Troop> attack_target;
    float arrive_radius = 0.0f;
};

class Troop {
protected:
    glm::vec2 position = glm::vec2(0.0f);
    TroopDataAsset* data = nullptr;
    int health = 0;
    Team team = Team::FRUIT;

    // Animation
    float walk_time = 0.0f;
    float attack_time = 0.0f;
    unsigned char pose = 1; // 1 = walk, 2 = attack
    bool face_left = false;

    // Manual Movement
    bool selected = false;
    Order order;
    
public:
    Troop(glm::vec2 position_, TroopDataAsset* data_, Team team_) : 
        position(position_), data(data_), health(data_ ? data->max_health : 0), team(team_), face_left(team_ != Team::FRUIT) {}

    virtual ~Troop() = default;

    /************************ AI FUNCTIONS ******************* */
    virtual void update(float elapsed) = 0;
    virtual void attack() = 0;
    virtual void move_to_target(float elapsed) = 0;
    virtual void determine_target() = 0;
    virtual bool is_attacking() const { return false; }
    virtual TroopDataAsset* get_data_asset() const { return data; }


    /************************ DATA ASSET STATS ******************* */
    std::string const get_name() const { return data->name; }
    Team get_team() const { return team; }
    int get_max_health() const { return data->max_health; }
    float get_attack_rate() const { return data->attack_rate; }
    float get_troop_radius() const { return data->troop_radius; }
    float get_attack_radius() const { return data->attack_radius; }

    
    /************************ Position & Health ******************* */
    glm::vec2 get_position() const { return position; }
    void set_position(glm::vec2 new_position) { position = new_position; }
    int get_health() const { return health; }
    bool is_dead() const {return health <= 0; }
    void set_health(int new_health) { health = new_health; }
    void heal(int amount) { health = std::min(health + amount, get_max_health()); }
    virtual void take_damage(int damage) {
        health -= damage;
        if (order.type == OrderType::Move) cancel_order();
    }
    

    /************************ Animation ******************* */
    float get_walk_time() const { return walk_time; }
    void add_walk_time(float elapsed ) { walk_time += elapsed; }
    
    float get_attack_time() const { return attack_time; }
    void add_attack_time(float elapsed) { attack_time += elapsed; }
    void reset_attack_time() { attack_time = 0.0f; }
   
    unsigned char get_pose() const { return pose; }
    void set_pose(unsigned char next) { pose = next; }
    bool faces_left() const { return face_left; }
    void set_face_left(bool left) { face_left = left; }


    /************************ Orders & Selection ******************* */
    void give_order(Order &o) { order = o; }
    void cancel_order() { order = Order{}; }
    Order get_order() { return order; }

    bool is_selected() const { return selected; }
    void set_selected(bool s) { selected = s; }
};
