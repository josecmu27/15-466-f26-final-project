#include "Ranged.hpp"
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

void Ranged::update(float elapsed)
{
    if (team == Team::BASKET || team == Team::ENEMY_BASKET) return;

    auto ordered = order.attack_target.lock();
    if (order.type == OrderType::Attack && (!ordered || ordered->is_dead())) cancel_order();
    if (order.type == OrderType::Move && glm::distance(position, order.move_target) < order.arrive_radius) cancel_order();

    if (order.type == OrderType::Move) {
        target_troop.reset();
        shot_cycle = -1;
        walk_toward(elapsed, order.move_target);
        return;
    }

    if (order.type == OrderType::Attack) target_troop = ordered;
    else determine_target();

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

bool Ranged::is_attacking() const
{
    auto target = target_troop.lock();
    if (!target || target->is_dead() || !data) return false;
    return glm::distance(position, target->get_position()) < data->attack_range;
}

void Ranged::attack()
{
    auto target = target_troop.lock();
    if (!target || !data || !get_data_asset()) return;

    glm::vec2 projectile_dir = glm::normalize(target->get_position() - position);

    // spawn banana projectile in direction to the target troop
    World::get_world()->projectiles.push_back(std::make_shared<Projectile>(
		position,
		projectile_dir * get_data_asset()->projectile_speed,
		data->attack_damage,
		get_data_asset()->projectile_radius,
        team,
        data->attack_range * 1.5f // some slack so a target walking away can still be hit
	));
}

/* todo: add pathfinding code */
void Ranged::move_to_target(float elapsed)
{
    auto target = target_troop.lock();
    
    if (!target || target->is_dead()){
        target_troop.reset();
        return;
    }

    walk_toward(elapsed, approach_point(position, *target));
}

void Ranged::walk_toward(float elapsed, glm::vec2 goal) {
    std::vector<glm::vec2> others;
    for (auto const &troop : World::get_world()->troops) {
        if (troop.get() == this || troop->is_dead()) continue;
        if (is_basket(troop->get_team())) continue;
        others.push_back(troop->get_position());
    }

    glm::vec2 wp = World::get_world()->paths.next_waypoint(position, goal, others);
    float dist = glm::distance(position, wp);
    float step = data->movement_speed * elapsed;
    if (dist > 0.0f) position = glm::mix(position, wp, std::min(1.0f, step / dist));
}


/* @brief find the closest troop from the opposite team

 */
void Ranged::determine_target()
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
