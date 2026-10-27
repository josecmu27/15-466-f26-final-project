#pragma once

#include "Map.hpp"

#include <glm/glm.hpp>

#include <vector>

struct Pathfinder {
	explicit Pathfinder(Map const &map_) : map(map_) {}

	glm::vec2 next_waypoint(glm::vec2 self, glm::vec2 goal, std::vector< glm::vec2 > const &others) const;

private:
	Map const &map;

	glm::vec2 grid_step(glm::vec2 from, glm::vec2 to) const;

	bool walkable(glm::vec2 p) const;
};