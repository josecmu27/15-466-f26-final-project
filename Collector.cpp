#include "Collector.hpp"
#include "GameAudio.hpp"
#include "World.hpp"

namespace {

bool is_basket(Team team) {
	return team == Team::BASKET || team == Team::ENEMY_BASKET;
}

glm::vec2 friendly_basket_edge(glm::vec2 from, Team team) {
	Map const &map = World::get_world()->map;
	glm::vec2 mn = (team == Team::BLUE) ? map.friendly_base_min() : map.enemy_base_min();
	glm::vec2 mx = (team == Team::BLUE) ? map.friendly_base_max() : map.enemy_base_max();
	glm::vec2 target_position = glm::clamp(from, mn, mx);
	glm::vec2 inward = 0.5f * (mn + mx) - target_position;
	float len = glm::length(inward);
	if (len > 1e-4f) target_position += (inward / len) * 0.05f;
	return target_position;
}

}

void Collector::update(float elapsed)
{
    CollectorDataAsset* collector_data = get_data_asset();
    World* world = World::get_world();

    if (!collector_data || !world) return;

    // Drop current resource to base
    if (auto drop = carried_water_drop.lock()) {
        drop->position = position + glm::vec2(0.0f, 0.5f);
        glm::vec2 deposit_position = friendly_basket_edge(position, team);
		
        if (glm::distance(position, deposit_position) <= collector_data->deposit_radius) {
			world->add_resources(team, drop->value);
            drop->being_carried = false;
            drop->collected = true;
			carried_water_drop.reset();
            
		} else {
			walk_toward(elapsed, deposit_position);
		}
		return;
    }

    // No resource on hand, look for the closest active water drop
    std::shared_ptr<WaterDrop> closest_drop;
	float best_distance = std::numeric_limits<float>::infinity();

	for (auto &drop : world->water_drops) {
		if (drop->collected || drop->being_carried) continue;
		float distance = glm::distance(position, drop->position);
		if (distance < best_distance) {
			best_distance = distance;
			closest_drop = drop;
		}
	}

	if (closest_drop) {
		if (best_distance <= collector_data->pickup_radius) {
			closest_drop->being_carried = true;
			carried_water_drop = closest_drop;
			GameAudio::play_at(GameAudio::Sfx::Pickup, position);
            closest_drop.reset();
		} else {
			walk_toward(elapsed, closest_drop->position);
		}
		return;
	}

	// No drops on the map, retreat to base and wait
	glm::vec2 home_position = friendly_basket_edge(position, team);
	if (glm::distance(position, home_position) > collector_data->deposit_radius) {
		walk_toward(elapsed, home_position);
	}


}

bool Collector::is_attacking() const
{
    return false;
}

void Collector::attack()
{
   
}


void Collector::move_to_target(float elapsed)
{

}

void Collector::walk_toward(float elapsed, glm::vec2 goal) {
    std::vector<glm::vec2> others;
    for (auto const &troop : World::get_world()->troops) {
        if (troop->is_dead()) continue;
        if (is_basket(troop->get_team())) continue;
        others.push_back(troop->get_position());
    }

    glm::vec2 wp = World::get_world()->paths.next_waypoint(position, goal, others);
    float dist = glm::distance(position, wp);
    float step = data->movement_speed * elapsed;
    if (dist > 0.0f) position = glm::mix(position, wp, std::min(1.0f, step / dist));
}


void Collector::determine_target()
{
    
}

void Collector::take_damage(int damage)
{
    health -= damage;

	if (is_dead()) {
        if (auto drop = carried_water_drop.lock()) {
            drop->position = position;
            drop->being_carried = false;
            carried_water_drop.reset();
        }
	}
}
