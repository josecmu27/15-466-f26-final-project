#include "World.hpp"
#include "GameAudio.hpp"

World* World::instance = nullptr;

void World::clear()
{
    troops.clear();
    projectiles.clear();
    water_drops.clear();
    player_resources = 0;
    director_resources = 0;
    max_resources = 10;
    match_time_remaining = match_time_duration;
    regeneration_timer = 1.0f;
}

void World::update(float elapsed)
{
    glm::vec2 left(map.bounds_min());
	glm::vec2 right(map.bounds_max());

    for (auto &troop : troops){
        if (!troop || troop->is_dead()) continue;
        glm::vec2 before = troop->get_position();

        troop->update(elapsed);

        glm::vec2 p = glm::clamp(troop->get_position(), left, right);
		troop->set_position(p);

        float dx = p.x - before.x;
        if (dx > 0.002f) troop->set_face_left(false);
        else if (dx < -0.002f) troop->set_face_left(true);

        Team team = troop->get_team();
        if (team != Team::BLUE && team != Team::RED) continue;
        if (troop->is_attacking()) {
            if (troop->get_pose() != 2) troop->reset_attack_time();
            troop->set_pose(2);
            troop->add_attack_time(elapsed);
        } else {
            troop->set_pose(1);
            if (glm::distance(before, p) > 0.002f) troop->add_walk_time(elapsed);
        }

    }

    for (auto &projectile : projectiles){
        projectile->update(elapsed);
        glm::vec2 at = projectile->get_position();
        if (at != glm::clamp(at, left, right)) projectile->set_inactive(); // left the map
    }

    handle_collisions();

    if (match_time_remaining > 0.0f) {
        match_time_remaining = std::max(0.0f, match_time_remaining - elapsed);
    }

    // water drop resource regeneration
    if (regeneration_timer <= 0.0f){
        add_player_resources(base_regeneration_amount);
        add_director_resources(base_regeneration_amount);
        regeneration_timer = regeneration_interval;
    } else {
        regeneration_timer -= elapsed;
    }
    
    cleanup_defeated_troops();
    cleanup_inactive_projectiles();
    cleanup_collected_resources();
}

// optimize later
// only checking for troop projectile collisions
void World::handle_collisions()
{
    for (auto &troop : troops)
    {
        if (troop->is_dead()) continue; // already down this frame; don't soak up another shot

        for (auto &projectile : projectiles)
        {
            if (projectile->is_inactive()) continue; // each shot hits at most one troop
            if (projectile->get_team() == troop->get_team()) continue;
            
            float dist_squared = (troop->get_position().x - projectile->get_position().x) *
                                 (troop->get_position().x - projectile->get_position().x) + 
                                 (troop->get_position().y - projectile->get_position().y) *
                                 (troop->get_position().y - projectile->get_position().y);

            
            float radius_sum_squared = (troop->get_troop_radius() + projectile->get_radius()) *
                                       (troop->get_troop_radius() + projectile->get_radius());

            if (dist_squared <= radius_sum_squared){
                troop->set_health(troop->get_health() - projectile->get_damage());
                GameAudio::play_at(GameAudio::Sfx::RangedAttack, troop->get_position());
                projectile->set_inactive();
            }
        }

    }
}

void World::cleanup_defeated_troops()
{
    for (auto it = troops.begin(); it != troops.end();){
            if ((*it)->is_dead())
        {
            Team team = (*it)->get_team();
            if (team == Team::TREE || team == Team::ENEMY_TREE) GameAudio::play_at(GameAudio::Sfx::TreeBreak, (*it)->get_position());
            auto water_drop = std::make_shared<WaterDrop>((*it)->get_position(), 5, false, false);
            water_drops.push_back(water_drop);
            it = troops.erase(it);
        } 
        else 
        {
            ++it;
        }
    }
}

void World::cleanup_inactive_projectiles()
{
    for (auto it = projectiles.begin(); it != projectiles.end();){
        if ((*it)->is_inactive())
        {
            it = projectiles.erase(it);
        } 
        else 
        {
            ++it;
        }
    }
}

void World::cleanup_collected_resources()
{
    for (auto it = water_drops.begin(); it != water_drops.end();){
        if ((*it)->collected)
        {
            it = water_drops.erase(it);
        } 
        else 
        {
            ++it;
        }
    }
}

void World::add_resources(Team team, int amount) {
    if (team == Team::BLUE) add_player_resources(amount);
    if (team == Team::RED) add_director_resources(amount);
}
