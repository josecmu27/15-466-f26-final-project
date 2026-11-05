#include "World.hpp"

World* World::instance = nullptr;

void World::update(float elapsed)
{
    glm::vec2 left(map.bounds_min());
	glm::vec2 right(map.bounds_max());

    for (auto &troop : troops){
        glm::vec2 before = troop->get_position();

        troop->update(elapsed);

        glm::vec2 p = glm::clamp(troop->get_position(), left, right);
		troop->set_position(p);

        if (troop->get_team() == Team::FRUIT && glm::distance(before, p) > 0.002f) {
			troop->add_walk_time(elapsed);
		}

    }

    for (auto &projectile : projectiles){
        projectile->update(elapsed);
    }

    handle_collisions();
    cleanup_defeated_troops();
}

void World::handle_collisions()
{

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
