#include "World.hpp"

World* World::instance = nullptr;

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

        if (troop->get_team() != Team::FRUIT) continue;
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
    }

    handle_collisions();
    
    cleanup_defeated_troops();
    cleanup_inactive_projectiles();
}

// optimize later
// only checking for troop projectile collisions
void World::handle_collisions()
{
    for (auto &troop : troops)
    {
        
        for (auto &projectile : projectiles)
        {
            float dist_squared = (troop->get_position().x - projectile->get_position().x) *
                                 (troop->get_position().x - projectile->get_position().x) + 
                                 (troop->get_position().y - projectile->get_position().y) *
                                 (troop->get_position().y - projectile->get_position().y);

            
            float radius_sum_squared = (troop->get_radius() + projectile->get_radius()) *
                                       (troop->get_radius() + projectile->get_radius());

            if (dist_squared > radius_sum_squared){
                troop->set_health(troop->get_health() - projectile->get_damage());
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
