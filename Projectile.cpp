#include "Projectile.hpp"

void Projectile::update(float elapsed)
{
    position += velocity * elapsed;
    if (glm::distance(origin, position) > max_range) active = false;
}
