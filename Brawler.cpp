#include "Brawler.hpp"
#include "World.hpp"

void Brawler::update(float elapsed)
{
    if (team == Team::BASKET || team == Team::ENEMY_BASKET) return;

    auto target = target_troop.lock();

    attack_timer -= elapsed;

    determine_target();

    if (target && glm::distance(position, target->get_position()) < attack_range){
        if (attack_timer < 0.0f){
            attack();
            attack_timer = attack_rate;
        }
    } else{
        move_to_target(elapsed);
    }
  

}   

void Brawler::attack()
{
    auto target = target_troop.lock();
    if (!target) return;

    int new_health = target->get_health() - attack_damage;
    target->set_health(new_health);
    
    if (target->is_dead())
    {
        target_troop.reset();

    }

}


void Brawler::move_to_target(float elapsed)
{
    auto target = target_troop.lock();
    
    if (!target || target->is_dead()){
        target_troop.reset();
        return;
    }
    
    std::vector<glm::vec2> others;
    for (auto const &troop : World::get_world()->troops) {
        if (troop.get() == this || troop->is_dead()) continue;
        others.push_back(troop->get_position());
    }

    glm::vec2 wp = World::get_world()->paths.next_waypoint(position, target->get_position(), others);
    float dist = glm::distance(position, wp);
    float step = movement_speed * elapsed;
    if (dist > 0.0f) position = glm::mix(position, wp, std::min(1.0f, step / dist));
    
}

/* @brief find the closest troop from the opposite team

 * @param troops the troop list from PlayMode (might need to get replaced after prototype)
 */
void Brawler::determine_target()
{
    float best_distance = std::numeric_limits<float>::infinity();
    std::weak_ptr<Troop> best_troop;

    for (auto const &troop : World::get_world()->troops){
        if (!troop || troop.get() == this) continue;
        if (troop->get_team() == team || troop->is_dead()) continue;
        if (troop->get_team() == Team::BASKET && team == Team::FRUIT) continue;
        if (troop->get_team() == Team::ENEMY_BASKET && team == Team::VEGETABLE) continue;

        float distance_to_troop = glm::distance(troop->get_position(), position);
        
        if (distance_to_troop < best_distance){
            best_distance = distance_to_troop;
            best_troop = troop;
        }
    }

    target_troop = best_troop;
    auto target = target_troop.lock();
    if (!target) return;

}
