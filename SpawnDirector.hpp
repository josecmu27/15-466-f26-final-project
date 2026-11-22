#pragma once

#include <random>
#include <glm/glm.hpp>
#include "DataAssets.hpp"
#include "Troop.hpp"

/**
 * @brief Inspired and referenced from Valve's GDC Talk: https://www.youtube.com/watch?v=PJNQl3K58CQ
 */
enum class DirectorState {
    BUILD_UP,
    SUSTAIN_PEAK,
    PEAK_FADE,
    RELAX
};

struct DirectorConfig {
	// Pacing and state thresholds
	float initial_intensity = 10.0f;
	float base_peak_threshold = 70.0f;
	float endgame_peak_threshold = 90.0f;
	float relax_threshold = 25.0f;
	float intensity_decay = 0.08f;

	// State durations
	float max_buildup_time = 15.0f;
	float sustain_time = 4.0f;
	float max_fade_time = 8.0f;
	float base_max_relax_time = 6.0f;
	float endgame_max_relax_time = 3.0f;
	float endgame_start = 60.0f;

	// Spawn cooldowns
	float initial_spawn_cooldown = 1.5f;
	float base_spawn_cooldown = 1.5f;
	float endgame_spawn_cooldown = 1.25f;
	float peak_spawn_cooldown = 1.0f;
	float relax_spawn_cooldown = 2.5f;
    float fade_defense_cooldown = 2.0f;
	float failed_spawn_retry = 0.5f;
	int max_peak_spawns = 2;

	// Intensity sensor weights
	float player_base_damage_weight = 0.75f;
	float director_base_damage_weight = 0.75f;
	float troop_elimination_effect = 2.5f;
	float threat_intensity_weight = 0.05f;

	// troop amount thresholds
	int max_collectors = 2;
	int swarm_threshold = 3;
	float base_spawn_offset = 1.0f;
};

struct WorldSnapshot {
    int director_general_troops = 0;
    int director_healers = 0;
    int director_collectors = 0;

    int player_flying_troops = 0;
    int player_ground_troops = 0;
    std::shared_ptr<Troop> closest_invader;
    std::shared_ptr<Troop> injured_defender;
    bool uncollected_drops = false;
    glm::vec2 closest_drop_position = glm::vec2(0.0f);
};

class SpawnDirector {
public:
    SpawnDirector(DataAssets& data, uint32_t seed);
    void reset(uint32_t seed);
    void update(float elapsed);

private:
    void scan_world();
    void update_intensity(float elapsed);
    void update_state(float elapsed);
    void enter_state(DirectorState next);

    bool spawn_offense();
    bool spawn_defense();
    bool spawn_collector();
    bool spawn_troop(TroopType type, glm::vec2 spawn_pos);
    glm::vec2 pick_spawn_position();

    DirectorConfig director_config;
    WorldSnapshot world_snapshot;
    DataAssets* director_data;

    DirectorState state = DirectorState::BUILD_UP;
    float intensity = 10.0f;
    float state_timer = 0.0f;
    float match_time_left = 0.0f;
    float base_match_time = 200.0f;
    float spawn_cooldown = 1.5f;

    int peak_spawns_left = 0;
    bool relax_defense_used = false;

    int previous_player_base_health = 0;
    int previous_director_base_health = 0;

    std::vector<std::weak_ptr<Troop>> tracked_player_troops;
	std::mt19937 rng;
};
