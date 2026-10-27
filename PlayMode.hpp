#pragma once

#include "Mode.hpp"
#include "Map.hpp"
#include "Pathfinder.hpp"

#include <glm/glm.hpp>
#include "Brawler.hpp"

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	Map map;
	Pathfinder paths{map};
	float time_ = 0.0f;

	glm::uvec2 window_size = glm::uvec2(1280, 720);
	glm::uvec2 drawable_size = glm::uvec2(1280, 720);
	glm::vec2 mouse = glm::vec2(0.0f);
	int hover_col = -1;
	int hover_row = -1;

	glm::mat4 clip_from_world(float aspect) const;
	bool mouse_to_world(glm::vec3 &hit) const;
	void refresh_hover();
	bool pointer_on_slot() const;
	float map_aspect() const;

	// Drops a cherry brawler at the center of the grass cell under `world`.
	void place_troop(glm::vec2 world);
	void load_troop_sprite();
	void draw_troops(glm::mat4 const &clip_from_world);
	void draw_sprite(glm::mat4 const &clip, glm::vec2 center, glm::vec2 half, glm::u8vec4 color);
	void draw_bar();

	// GAME LOGIC
	std::vector<std::shared_ptr<Troop>> troops;

	GLuint troop_tex_ = 0;
	GLuint troop_vbo_ = 0;
	GLuint troop_vao_ = 0;
	glm::vec2 troop_uv_min_ = glm::vec2(0.0f);
	glm::vec2 troop_uv_max_ = glm::vec2(1.0f);
	glm::vec2 troop_half_ = glm::vec2(0.7f);

	static constexpr float bar_frac_ = 0.16f;
	static constexpr int cherry_cost_ = 2;
	int resources_ = 10;
	bool dragging_ = false;
	GLuint bar_vbo_ = 0;
	GLuint bar_vao_ = 0;

	// TODO: Remove this after prototype!
	float troop_spawn_rate = 7.5f;
	float troop_spawn_timer = 0.0f;

	void spawn_vegetable_troop();

};
