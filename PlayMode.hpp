#pragma once

#include "Mode.hpp"
#include "Map.hpp"
#include "Pathfinder.hpp"
#include "World.hpp"

#include "CharacterAnim.hpp"

#include <glm/glm.hpp>
#include "Brawler.hpp"
#include "Ranged.hpp"

#include <string>
#include <vector>

/* troop selection (note that the baskets are not included)*/
enum TroopType {
	CHERRY_BRAWLER, 
	BANANA_RANGED
};

struct PlayMode : Mode {
	PlayMode();
	virtual ~PlayMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	float time_ = 0.0f;

	glm::uvec2 window_size = glm::uvec2(1280, 720);
	glm::uvec2 drawable_size = glm::uvec2(1280, 720);
	glm::vec2 mouse = glm::vec2(0.0f);
	int hover_col = -1;
	int hover_row = -1;

	glm::mat4 clip_from_world(float aspect) const;
	bool mouse_to_world(glm::vec3 &hit) const;
	void refresh_hover();
	bool pointer_on_slot();
	float map_aspect() const;

	// Drops a cherry brawler at the center of the grass cell under `world`.
	void place_troop(glm::vec2 spawn_pos);
	void load_troop_sprite();
	void load_character(CharacterAnimDesc const &desc);
	// px_scale <= 0 picks a scale that makes this strip 1.7 units tall, and returns that scale.
	float load_frames(std::string const &prefix, int count, GLuint *tex, glm::vec2 &uv_min, glm::vec2 &uv_max, glm::vec2 &half, glm::vec2 &offset, float px_scale);
	CharacterAnim const *find_anim(std::string const &troop_name) const;
	void draw_troops(glm::mat4 const &clip_from_world);
	void draw_sprite(glm::mat4 const &clip, glm::vec2 center, glm::vec2 half, glm::u8vec4 color, GLuint tex, glm::vec2 uv_min, glm::vec2 uv_max);
	void draw_bar();
	void draw_pause_overlay();
	void draw_unit_stats();
	void draw_victory_overlay();
	void draw_defeat_overlay();
	void draw_end_overlay(std::string const &label, glm::u8vec4 color);

	// Freezes the battle; troops can still be placed while paused.
	bool paused_ = false;

	// World removes a basket when its health runs out.
	std::weak_ptr< Troop > basket_;
	std::weak_ptr< Troop > enemy_basket_;
	bool won_ = false;
	bool lost_ = false;

	GLuint troop_vbo_ = 0;
	GLuint troop_vao_ = 0;
	std::vector< CharacterAnim > characters_;

	static constexpr float bar_frac_ = 0.16f;
	static constexpr int cherry_cost_ = 2;
	static constexpr int banana_cost_ = 3;
	int resources_ = 1000;
	bool dragging_ = false;
	bool box_dragging_ = false;
	glm::vec3 box_start_ = glm::vec3(0.0f);
	glm::vec3 box_end_ = glm::vec3(0.0f);
	GLuint bar_vbo_ = 0;
	GLuint bar_vao_ = 0;

	// TODO: Remove this after prototype!
	float troop_spawn_rate = 7.5f;
	float troop_spawn_timer = 0.0f;

	void spawn_vegetable_troop();

	TroopType selected_troop = TroopType::CHERRY_BRAWLER;
};
