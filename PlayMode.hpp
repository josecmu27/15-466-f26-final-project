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

	glm::uvec2 window_size = glm::uvec2(1280, 720);
	glm::uvec2 drawable_size = glm::uvec2(1280, 720);
	glm::vec2 mouse = glm::vec2(0.0f);
	int hover_col = -1;
	int hover_row = -1;

	glm::mat4 clip_from_world(float aspect) const;
	bool mouse_to_world(glm::vec3 &hit) const;
	void refresh_hover();
};
