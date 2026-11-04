#include "Projectile.hpp"

void Projectile::update(float elapsed)
{
    position += velocity * elapsed;

}
