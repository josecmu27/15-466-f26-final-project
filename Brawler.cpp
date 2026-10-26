#include "Brawler.hpp"

void Brawler::update(float elapsed)
{

    auto target = target_troop.lock();
    attack_timer -= elapsed;

    if (target && glm::distance(position, target->get_position()) < attack_range){
        if (attack_timer < 0.0f){
            attack();
            attack_timer = attack_rate;
        }
    } else if (target){
        move_to_target(elapsed);
    } else {
        determine_target();
    }

}   
void Brawler::attack()
{
    std::cout << "Attacking!" << std::endl;
    auto target = target_troop.lock();
    if (!target) return;

    int new_health = target->get_health() - attack_damage;
    target->set_health(new_health);
    
    if (target->is_dead())
    {
        std::cout << target->get_name() << " has been eliminated" << std::endl;
    }
}

void Brawler::move_to_target(float elapsed)
{
    auto target = target_troop.lock();
    if (!target || target->is_dead()){
        target_troop.reset();
        return;
    }

    if (target->get_position().x > position.x){
        position.x += movement_speed * elapsed;
    } else {
        position.x -= movement_speed * elapsed;
    }
}

void Brawler::determine_target() // todo after prototype
{
    
}
