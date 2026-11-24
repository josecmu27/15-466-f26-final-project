#include "Brawler.hpp"
#include "GameAudio.hpp"
#include "World.hpp"

#include <cmath>

namespace {

bool is_tree(Team team) {
	return team == Team::TREE || team == Team::ENEMY_TREE;
}

void tree_rect(Team team, glm::vec2 &mn, glm::vec2 &mx) {
	Map const &map = World::get_world()->map;
	if (team == Team::TREE) {
		mn = map.friendly_base_min();
		mx = map.friendly_base_max();
	} else {
		mn = map.enemy_base_min();
		mx = map.enemy_base_max();
	}
}

// Distance to a tree is distance to its footprint, so a unit on the near edge is in range.
float distance_to(glm::vec2 from, Troop const &troop) {
	if (!is_tree(troop.get_team())) return glm::distance(from, troop.get_position());
	glm::vec2 mn, mx;
	tree_rect(troop.get_team(), mn, mx);
	return glm::distance(from, glm::clamp(from, mn, mx));
}

// A point just inside the near face, so pathfinding heads for the side the unit is on.
glm::vec2 approach_point(glm::vec2 from, Troop const &troop) {
	if (!is_tree(troop.get_team())) return troop.get_position();
	glm::vec2 mn, mx;
	tree_rect(troop.get_team(), mn, mx);
	glm::vec2 q = glm::clamp(from, mn, mx);
	glm::vec2 inward = 0.5f * (mn + mx) - q;
	float len = glm::length(inward);
	if (len > 1e-4f) q += inward / len * 0.05f;
	return q;
}

}

void Brawler::update(float elapsed)
{
    if (team == Team::TREE || team == Team::ENEMY_TREE) return;

    auto ordered = order.attack_target.lock();
    if (order.type == OrderType::Attack && (!ordered || ordered->is_dead())) cancel_order();
    if (order.type == OrderType::Move && glm::distance(position, order.move_target) < order.arrive_radius) cancel_order();

    if (order.type == OrderType::Move) {
        target_troop.reset();
        hit_cycle = -1;
        walk_toward(elapsed, order.move_target);
        return;
    }

    if (order.type == OrderType::Attack) target_troop = ordered;
    else determine_target();

    auto target = target_troop.lock();

    if (target && distance_to(position, *target) < data->attack_range){
        float aim = target->get_position().x - position.x;
        if (aim > 0.01f) set_face_left(false);
        else if (aim < -0.01f) set_face_left(true);

        // One swing lasts attack_rate. Frame 7 (index 6) is the fist fully extended.
        constexpr int attack_frames = 11;
        constexpr int release_frame = 6;
        float rate = data->attack_rate > 1e-4f ? data->attack_rate : float(attack_frames) / 12.0f;
        float shown = (get_pose() == 2) ? get_attack_time() + elapsed : elapsed;
        int tick = (int)std::floor(shown / rate * float(attack_frames));
        if (tick < 0) tick = 0;
        int cycle = tick / attack_frames;
        int frame = tick % attack_frames;
        if (frame >= release_frame && hit_cycle != cycle){
            attack();
            hit_cycle = cycle;
        }
    } else{
        hit_cycle = -1;
        if (order.type != OrderType::Hold) move_to_target(elapsed);
    }
  

}   

bool Brawler::is_attacking() const
{
    auto target = target_troop.lock();
    if (!target || target->is_dead() || !data) return false;
    return distance_to(position, *target) < data->attack_range;
}

void Brawler::attack()
{
    auto target = target_troop.lock();
    if (!target || !data) return;

    target->take_damage(data->attack_damage);
    GameAudio::play_at(is_tree(target->get_team()) ? GameAudio::Sfx::TreeHit : GameAudio::Sfx::BrawlerAttack, position);

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

    walk_toward(elapsed, approach_point(position, *target));
}

void Brawler::walk_toward(float elapsed, glm::vec2 goal) {
    std::vector<glm::vec2> others;
    for (auto const &troop : World::get_world()->troops) {
        if (troop.get() == this || troop->is_dead()) continue;
        if (is_tree(troop->get_team())) continue;
        others.push_back(troop->get_position());
    }

    glm::vec2 wp = World::get_world()->paths.next_waypoint(position, goal, others);
    float dist = glm::distance(position, wp);
    float step = data->movement_speed * elapsed;
    if (dist > 0.0f) position = glm::mix(position, wp, std::min(1.0f, step / dist));
}

/**
 * @brief find the closest troop from the opposite team
 */
void Brawler::determine_target()
{
    float best_distance = std::numeric_limits<float>::infinity();
    std::weak_ptr<Troop> best_troop;

    for (auto const &troop : World::get_world()->troops){
        if (!troop || troop.get() == this) continue;
        if (troop->get_team() == team || troop->is_dead()) continue;
        if (troop->get_team() == Team::TREE && team == Team::BLUE) continue;
        if (troop->get_team() == Team::ENEMY_TREE && team == Team::RED) continue;
        if (troop->get_data_asset()->is_flying) continue;

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
