#include "SpawnDirector.hpp"
#include "World.hpp"
#include "Troop.hpp"
#include "Brawler.hpp"
#include "Ranged.hpp"
#include "Healer.hpp"
#include "Flyer.hpp"
#include "Collector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>

SpawnDirector::SpawnDirector(DataAssets& data, uint32_t seed) {
    director_data = &data;
    reset(seed);
}

void SpawnDirector::reset(uint32_t seed) {
    rng.seed(seed);
    world_snapshot = {};
    intensity = director_config.initial_intensity;
    match_time_left = 200.0f;
    previous_player_base_health = 1000;
    previous_director_base_health = 1000;
    tracked_player_troops.clear();

    enter_state(DirectorState::BUILD_UP);
    spawn_cooldown = director_config.initial_spawn_cooldown;
    match_time_left = base_match_time;
}

void SpawnDirector::enter_state(DirectorState new_state) {
    // DEBUG
    if (new_state == DirectorState::BUILD_UP) {
        std::cout << "New State: BUILD_UP" << std::endl;
    } else if (new_state == DirectorState::SUSTAIN_PEAK) {
        std::cout << "New State: SUSTAIN_PEAK" << std::endl;
    } else if (new_state == DirectorState::PEAK_FADE) {
        std::cout << "New State: PEAK_FADE" << std::endl;
    } else {
        std::cout << "New State: RELAX" << std::endl;
    }

    state = new_state;
    state_timer = 0.0f;

    if (new_state == DirectorState::SUSTAIN_PEAK) {
        peak_spawns_left = director_config.max_peak_spawns;
        spawn_cooldown = 0.0f;
    } else if (new_state == DirectorState::RELAX) {
        relax_defense_used = false;
        spawn_cooldown = 0.0f;
    }
}

void SpawnDirector::update(float elapsed) {
    match_time_left -= elapsed;
    if (match_time_left <= 0.0f) return; 
    scan_world();
    update_intensity(elapsed);
    update_state(elapsed);
}

void SpawnDirector::scan_world() {
    world_snapshot = {};
    World* world = World::get_world();
    glm::vec2 player_base_position = world->map.base_center(false);
	glm::vec2 director_base_position = world->map.base_center(true);
    float midfield_x = 0.5f * (player_base_position.x + director_base_position.x);

    float closest_invader_distance = std::numeric_limits<float>::infinity();

    for (auto const&troop : world->troops) {
        if (!troop || troop->is_dead()) continue;

        // Scan director troops
        if (troop->get_team() == Team::RED) {
            if (troop->get_name() == "PineappleHealer") {
                world_snapshot.director_healers++;
            } else if (troop->get_name() == "BlueberryCollector") {
                world_snapshot.director_collectors++;
            } else {
                world_snapshot.director_general_troops++;
                
                if (troop->get_data_asset() && troop->get_health() < troop->get_data_asset()->max_health) {
                    world_snapshot.injured_defender = troop;
                }
            }
        }

        // Scan player troops
        if (troop->get_team() == Team::BLUE) {
            if (troop->get_data_asset() && troop->get_data_asset()->is_flying) {
                world_snapshot.player_flying_troops++;
            } else {
                world_snapshot.player_ground_troops++;
            }

    
            bool past_midfield = (director_base_position.x > player_base_position.x) ? 
                                (troop->get_position().x > midfield_x) : (troop->get_position().x < midfield_x);

			if (past_midfield) {
				float distance = glm::distance(director_base_position, troop->get_position());
				if (distance < closest_invader_distance) {
					closest_invader_distance = distance;
					world_snapshot.closest_invader = troop;
				}

			}
        }
    }

    float closest_water_drop_distance = std::numeric_limits<float>::infinity();
    for(auto const &water_drop : world->water_drops) {
        if (!water_drop || water_drop->collected || water_drop->being_carried) continue;

        world_snapshot.uncollected_drops = true;
        float distance = glm::distance(director_base_position, water_drop->position);

        if (distance < closest_water_drop_distance) {
            closest_water_drop_distance = distance;
            world_snapshot.closest_drop_position = water_drop->position;
        }
    }
}

void SpawnDirector::update_intensity(float elapsed) {
    World* world = World::get_world();
    if (!world) return;

    glm::vec2 player_base_position = world->map.base_center(false); // player's basket position (still might change the 'basket' to another kinda of base)
    glm::vec2 director_base_position = world->map.base_center(true);
    float arena_length = glm::distance(player_base_position, director_base_position);

    intensity *= std::exp(-director_config.intensity_decay * elapsed);

    // Check if any scanned player troops were eliminated
    for (auto const& player_troop : tracked_player_troops) {
        auto troop = player_troop.lock();
        if (!troop || troop->is_dead()) {
            intensity += director_config.troop_elimination_effect;
        }
    }

    tracked_player_troops.clear();

    for (auto const& troop : world->troops) {
        if (!troop) continue;

        if (troop->get_team() == Team::BASKET) {
            int health = troop->get_health();
            if (health < previous_player_base_health) {
                intensity += float(previous_player_base_health - health) * director_config.player_base_damage_weight;
                previous_player_base_health = health;
            }
        } else if (troop->get_team() == Team::ENEMY_BASKET) {
            int health = troop->get_health();
            if (health < previous_director_base_health) {
                intensity += float(previous_director_base_health - health) * director_config.director_base_damage_weight;
                previous_director_base_health = health;
            }
        } else if (troop->get_team() == Team::BLUE) {
            tracked_player_troops.push_back(troop);
        } else if (troop->get_team() == Team::RED) {
            float distance_to_player_base = glm::distance(troop->get_position(), player_base_position);
            float progress = 1.0f - std::clamp(distance_to_player_base / arena_length, 0.0f, 1.0f);
            intensity += progress * director_config.threat_intensity_weight * elapsed;
        }

    }

    intensity = std::clamp(intensity, 0.0f, 100.0f);
    // std::cout << intensity << std::endl;
}

void SpawnDirector::update_state(float elapsed) {
    state_timer += elapsed;

    if (spawn_cooldown > 0.0f) {
        spawn_cooldown -= elapsed;
    }

    bool is_endgame = (match_time_left <= director_config.endgame_start);
    float current_peak = is_endgame ? director_config.endgame_peak_threshold : director_config.base_peak_threshold ;
    float current_relax_time = is_endgame ? director_config.endgame_max_relax_time : director_config.base_max_relax_time;
    float current_spawn_cooldown = is_endgame ? director_config.endgame_spawn_cooldown : director_config.base_spawn_cooldown;

    switch (state) {
        case DirectorState::BUILD_UP: {
            if (intensity >= current_peak || state_timer >= director_config.max_buildup_time) {
                enter_state(DirectorState::SUSTAIN_PEAK);
                return;
            }

            if (spawn_cooldown <= 0.0f) {
                bool invaded = (world_snapshot.closest_invader != nullptr);
                bool spawned = invaded ? spawn_defense() : spawn_offense();
                spawn_cooldown = spawned ? current_spawn_cooldown : director_config.failed_spawn_retry;
            }
            
            break;
        }

        case DirectorState::SUSTAIN_PEAK: {
            if (state_timer >= director_config.sustain_time) {
                enter_state(DirectorState::PEAK_FADE);
            }

            if (spawn_cooldown <= 0.0f && peak_spawns_left > 0) {
                bool invaded = (world_snapshot.closest_invader != nullptr);
                bool spawned = invaded ? spawn_defense() : spawn_offense();

                if (spawned) {
                    peak_spawns_left--;
                    spawn_cooldown = director_config.peak_spawn_cooldown;
                } else {
                    spawn_cooldown = spawned ? current_spawn_cooldown : director_config.failed_spawn_retry;
                }
                
            }
            break;
        }

        case DirectorState::PEAK_FADE: {
            if (intensity <= director_config.relax_threshold || state_timer >= director_config.max_fade_time) {
                enter_state(DirectorState::RELAX);
                return;
            }

            if (spawn_cooldown <= 0.0f && world_snapshot.closest_invader != nullptr) {
                bool spawned = spawn_defense();
                spawn_cooldown = spawned ? director_config.fade_defense_cooldown : director_config.failed_spawn_retry;
            }
            break;
        }

        case DirectorState::RELAX: {
            if (!world_snapshot.uncollected_drops || state_timer >= current_relax_time) {
                enter_state(DirectorState::BUILD_UP);
                return;
            }

            if (spawn_cooldown <= 0.0f) {
                if (world_snapshot.director_collectors < director_config.max_collectors && world_snapshot.uncollected_drops) {
                    bool spawned = spawn_collector();
                    spawn_cooldown = spawned ? director_config.relax_spawn_cooldown : director_config.failed_spawn_retry;
                } else if (!relax_defense_used && world_snapshot.director_collectors > 0 && world_snapshot.closest_invader != nullptr) {
                    bool spawned = spawn_defense();
                    if (spawned) {
                        relax_defense_used = true;
                        spawn_cooldown = director_config.relax_spawn_cooldown;
                    } else {
                        spawn_cooldown = director_config.failed_spawn_retry;
                    }
                }
            }
            break;
        }
    }
}

glm::vec2 SpawnDirector::pick_spawn_position() {
	World *world = World::get_world();
	glm::vec2 player_base_center = 0.5f * (world->map.friendly_base_min() + world->map.friendly_base_max());
	glm::vec2 min = world->map.enemy_base_min();
	glm::vec2 max = world->map.enemy_base_max();

	float spawn_x = (player_base_center.x < min.x) ? (min.x - director_config.base_spawn_offset) : (max.x + director_config.base_spawn_offset);

	float spawn_y = 0.0f;

	if (state == DirectorState::RELAX && world_snapshot.uncollected_drops &&
		world_snapshot.director_collectors < director_config.max_collectors) {
		spawn_y = std::clamp(world_snapshot.closest_drop_position.y, min.y, max.y);
	} else if (world_snapshot.injured_defender != nullptr && world_snapshot.director_healers == 0) {
		spawn_y = std::clamp(world_snapshot.injured_defender->get_position().y, min.y, max.y);
	} else if (world_snapshot.closest_invader != nullptr) {
		spawn_y = std::clamp(world_snapshot.closest_invader->get_position().y, min.y, max.y);
	} else {
		std::uniform_real_distribution<float> y_dist(min.y, max.y);
		spawn_y = y_dist(rng);
	}

	return glm::vec2(spawn_x, spawn_y);
}

bool SpawnDirector::spawn_troop(TroopType type, glm::vec2 spawn_pos) {
	World *world = World::get_world();
	if (!world || !director_data) return false;

	int col = 0;
	int row = 0;
	if (!world->map.world_to_cell(glm::vec3(spawn_pos, 0.0f), col, row)) return false;

	glm::vec2 pos(world->map.cell_center(col, row));
	int available = world->get_director_resources();

	if (type == TroopType::CHERRY_BRAWLER) {
		if (available < director_data->cherry.resource_cost) return false;
		world->subtract_director_resources(director_data->cherry.resource_cost);
		world->troops.push_back(std::make_shared<Brawler>(pos, &director_data->cherry, Team::RED));
		return true;
	} else if (type == TroopType::BANANA_RANGED) {
		if (available < director_data->banana.resource_cost) return false;
		world->subtract_director_resources(director_data->banana.resource_cost);
		world->troops.push_back(std::make_shared<Ranged>(pos, &director_data->banana, Team::RED));
		return true;
	} else if (type == TroopType::PINEAPPLE_HEALER) {
		if (available < director_data->pineapple.resource_cost) {
            return false;
        }
		world->subtract_director_resources(director_data->pineapple.resource_cost);
		world->troops.push_back(std::make_shared<Healer>(pos, &director_data->pineapple, Team::RED));
		return true;
	} else if (type == TroopType::ORANGE_FLYER) {
		if (available < director_data->orange.resource_cost) return false;
		world->subtract_director_resources(director_data->orange.resource_cost);
		world->troops.push_back(std::make_shared<Flyer>(pos, &director_data->orange, Team::RED));
		return true;
	} else if (type == TroopType::BLUEBERRY_COLLECTOR) {
		if (available < director_data->blueberry.resource_cost) return false;
		world->subtract_director_resources(director_data->blueberry.resource_cost);
		world->troops.push_back(std::make_shared<Collector>(pos, &director_data->blueberry, Team::RED));
		return true;
	}

	return false;
}

bool SpawnDirector::spawn_collector() {
	if (!world_snapshot.uncollected_drops || world_snapshot.director_collectors >= director_config.max_collectors) {
		return false;
	}
	return spawn_troop(TroopType::BLUEBERRY_COLLECTOR, pick_spawn_position());
}

bool SpawnDirector::spawn_defense() {
	glm::vec2 pos = pick_spawn_position();

	// Heal injured defenders or support an active defense line
	if (world_snapshot.director_healers == 0 && world_snapshot.injured_defender != nullptr) {
		if (spawn_troop(TroopType::PINEAPPLE_HEALER, pos)) {
			return true;
		}
	}

	// Intercept invader in its lane
	if (world_snapshot.closest_invader != nullptr) {
		bool invader_flying = world_snapshot.closest_invader->get_data_asset() &&
			world_snapshot.closest_invader->get_data_asset()->is_flying;

		if (invader_flying && spawn_troop(TroopType::BANANA_RANGED, pos)) {
			return true;
		}
		if (world_snapshot.director_general_troops == 0 && spawn_troop(TroopType::CHERRY_BRAWLER, pos)) {
			return true;
		}
		if (spawn_troop(TroopType::BANANA_RANGED, pos)) {
			return true;
		}
	}

	return false;
}

bool SpawnDirector::spawn_offense() {
	glm::vec2 pos = pick_spawn_position();

	// Flying troop counter
	if (world_snapshot.player_flying_troops > 0 && spawn_troop(TroopType::BANANA_RANGED, pos)) {
		return true;
	}

	// Swarm counter
	if (world_snapshot.player_ground_troops >= director_config.swarm_threshold && spawn_troop(TroopType::ORANGE_FLYER, pos)) {
		return true;
	}

	// Lead a wave with a mini tank
	if (world_snapshot.director_general_troops == 0 && spawn_troop(TroopType::CHERRY_BRAWLER, pos)) {
		return true;
	}

	// Wave variety
	std::array<TroopType, 3> followups = {
		TroopType::BANANA_RANGED,
		TroopType::ORANGE_FLYER,
		TroopType::CHERRY_BRAWLER
	};

	std::uniform_int_distribution<size_t> roll(0, followups.size() - 1);
	if (spawn_troop(followups[roll(rng)], pos)) {
		return true;
	}
	return false;
}
