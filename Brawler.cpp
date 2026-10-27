#include "Brawler.hpp"
#include "Pathfinder.hpp"

void Brawler::update(float elapsed, std::vector<std::shared_ptr<Troop>> &troops, Pathfinder const &paths)
{

    auto target = target_troop.lock();
    attack_timer -= elapsed;

    determine_target(troops);

    if (target && glm::distance(position, target->get_position()) < attack_range){
        if (attack_timer < 0.0f){
            attack();
            attack_timer = attack_rate;
        }
    } else{
        move_to_target(elapsed, troops, paths);
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
        std::cout << target->get_name() << " has been eliminated" << std::endl;
        target_troop.reset();
    }

}

/* todo: add pathfinding code */
void Brawler::move_to_target(float elapsed, std::vector<std::shared_ptr<Troop>> &troops, Pathfinder const &paths)
{
    auto target = target_troop.lock();
    
    if (!target || target->is_dead()){
        target_troop.reset();
        return;
    }

    std::vector<glm::vec2> others;
    for (auto const &troop : troops) {
        if (troop.get() == this || troop->is_dead()) continue;
        others.push_back(troop->get_position());
    }

    glm::vec2 wp = paths.next_waypoint(position, target->get_position(), others);
    float dist = glm::distance(position, wp);
    float step = movement_speed * elapsed;
    if (dist > 0.0f) position = glm::mix(position, wp, std::min(1.0f, step / dist));
}

/* @brief find the closest troop from the opposite team

 * @param troops the troop list from PlayMode (might need to get replaced after prototype)
 */
void Brawler::determine_target(std::vector<std::shared_ptr<Troop>> &troops)
{
    float best_distance = std::numeric_limits<float>::infinity();
    std::weak_ptr<Troop> best_troop;

    for (auto const &troop : troops){
        if (!troop || troop.get() == this) continue;
        if (troop->get_team() == team || troop->is_dead()) continue;

        float distance_to_troop = glm::distance(troop->get_position(), position);
        
        if (distance_to_troop < best_distance){
            best_distance = distance_to_troop;
            best_troop = troop;
        }
    }

    target_troop = best_troop;
    auto target = target_troop.lock();
    if (!target) return;

    std::cout << "New Target Aquired! Target is " << target->get_name() << std::endl;

}
