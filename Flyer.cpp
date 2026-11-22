#include "Flyer.hpp"
#include "Projectile.hpp"
#include "World.hpp"

#include <cmath>

namespace {

bool is_basket(Team team) {
	return team == Team::BASKET || team == Team::ENEMY_BASKET;
}

void basket_rect(Team team, glm::vec2 &mn, glm::vec2 &mx) {
	Map const &map = World::get_world()->map;
	if (team == Team::BASKET) {
		mn = map.friendly_base_min();
		mx = map.friendly_base_max();
	} else {
		mn = map.enemy_base_min();
		mx = map.enemy_base_max();
	}
}



// A point just inside the near face, so pathfinding heads for the side the unit is on.
glm::vec2 approach_point(glm::vec2 from, Troop const &troop) {
	if (!is_basket(troop.get_team())) return troop.get_position();
	glm::vec2 mn, mx;
	basket_rect(troop.get_team(), mn, mx);
	glm::vec2 q = glm::clamp(from, mn, mx);
	glm::vec2 inward = 0.5f * (mn + mx) - q;
	float len = glm::length(inward);
	if (len > 1e-4f) q += inward / len * 0.05f;
	return q;
}

}

void Flyer::update(float elapsed)
{
    if (team == Team::BASKET || team == Team::ENEMY_BASKET) return;

    determine_target();

    auto target = target_troop.lock();
    bool in_range = target && !target->is_dead()
        && glm::distance(position, target->get_position()) < data->attack_range;

    if (in_range){
        if (!data) return;

        // One attack clip lasts attack_rate seconds. Frame 12 (index 11) is the throw.
        constexpr int attack_frames = 16;
        constexpr int release_frame = 11;
        float rate = data->attack_rate > 1e-4f ? data->attack_rate : 16.0f / 12.0f;
        float shown = (get_pose() == 2) ? get_attack_time() + elapsed : elapsed;
        int tick = (int)std::floor(shown / rate * float(attack_frames));
        if (tick < 0) tick = 0;
        int cycle = tick / attack_frames;
        int frame = tick % attack_frames;
        if (frame >= release_frame && shot_cycle != cycle){
            attack();
            shot_cycle = cycle;
        }
    } else{
        shot_cycle = -1;
        move_to_target(elapsed);
    }
}

bool Flyer::is_attacking() const
{
    auto target = target_troop.lock();
    if (!target || target->is_dead() || !data) return false;
    return glm::distance(position, target->get_position()) < data->attack_range;
}

void Flyer::attack()
{
    auto target = target_troop.lock();
    if (!target || !get_data_asset()) return;

    target->take_damage(data->attack_damage);

    FlyerDataAsset* flyer_data = get_data_asset();
    
    for (auto const& troop : World::get_world()->troops){
        if (!troop || troop->is_dead()) continue;
        
        // check if friendly troop
        if (troop->get_team() == team || (team == Team::BLUE && troop->get_team() == Team::BASKET) || 
            (team == Team::RED && troop->get_team() == Team::ENEMY_BASKET)) continue;

        if (glm::distance(position, troop->get_position()) <= flyer_data->splash_radius){
            troop->take_damage(flyer_data->attack_damage);
        }
    }
}

void Flyer::move_to_target(float elapsed)
{
    auto target = target_troop.lock();
    if (!target || target->is_dead()){
        target_troop.reset();
        return;
    }

    walk_toward(elapsed, approach_point(position, *target));
}

void Flyer::walk_toward(float elapsed, glm::vec2 goal) {
    float dist = glm::distance(position, goal);
    float step = data->movement_speed * elapsed;
    if (dist > 0.0f) position = glm::mix(position, goal, std::min(1.0f, step / dist));
}


/* @brief find the closest troop from the opposite team

 */
void Flyer::determine_target()
{
    float best_distance = std::numeric_limits<float>::infinity();
    std::weak_ptr<Troop> best_troop;

    for (auto const &troop : World::get_world()->troops){
        if (!troop || troop.get() == this) continue;
        if (troop->get_team() == team || troop->is_dead()) continue;
        if (troop->get_team() == Team::BASKET && team == Team::BLUE) continue;
        if (troop->get_team() == Team::ENEMY_BASKET && team == Team::RED) continue;

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
