#pragma once

#include "Mode.hpp"
#include "Map.hpp"

#include <glm/glm.hpp>

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	Map map;
	float time_ = 0.0f;

	glm::mat4 clip_from_world(float aspect) const;
};
