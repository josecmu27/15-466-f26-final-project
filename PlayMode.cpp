#include "PlayMode.hpp"
#include "Ranged.hpp"
#include "TitleMode.hpp"

#include "ColorProgram.hpp"
#include "ColorTextureProgram.hpp"
#include "GameAudio.hpp"
#include "GameFonts.hpp"
#include "TextRenderer.hpp"
#include "data_path.hpp"
#include "gl_errors.hpp"
#include "load_save_png.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <limits>
#include <set>
#include <utility>

namespace {

// Flying troops hover above the ground. Their layer uses this raised foot line.
constexpr float flight_altitude = 1.0f;

struct ColorVertex {
	glm::vec3 Position;
	glm::u8vec4 Color;
};

// Rounded rectangle in clip space. rx and ry are separate so a corner stays round on a wide window.
void add_round_rect(std::vector< ColorVertex > &dst, glm::vec2 mn, glm::vec2 mx, float rx, float ry, glm::u8vec4 color) {
	rx = std::min(rx, 0.5f * std::max(0.0f, mx.x - mn.x));
	ry = std::min(ry, 0.5f * std::max(0.0f, mx.y - mn.y));
	if (rx < 1e-5f || ry < 1e-5f) return;
	glm::vec2 c = 0.5f * (mn + mx);
	glm::vec2 centers[4] = {
		glm::vec2(mx.x - rx, mn.y + ry),
		glm::vec2(mx.x - rx, mx.y - ry),
		glm::vec2(mn.x + rx, mx.y - ry),
		glm::vec2(mn.x + rx, mn.y + ry),
	};
	constexpr float tau = 6.2831853f;
	constexpr int segs = 4;
	std::vector< glm::vec2 > pts;
	for (int k = 0; k < 4; ++k) {
		float a0 = -tau / 4.0f + float(k) * tau / 4.0f;
		for (int i = 0; i <= segs; ++i) {
			float a = a0 + float(i) / float(segs) * tau / 4.0f;
			pts.push_back(centers[k] + glm::vec2(std::cos(a) * rx, std::sin(a) * ry));
		}
	}
	for (size_t i = 0; i < pts.size(); ++i) {
		glm::vec2 a = pts[i];
		glm::vec2 b = pts[(i + 1) % pts.size()];
		dst.push_back({glm::vec3(c, 0.0f), color});
		dst.push_back({glm::vec3(a, 0.0f), color});
		dst.push_back({glm::vec3(b, 0.0f), color});
	}
}

// Soft oval tucked under the feet. The sprite is centered on `center`, so the feet sit at the bottom edge.
void add_foot_shadow(std::vector< ColorVertex > &verts, glm::vec2 center, glm::vec2 half, glm::u8vec3 color) {
	float rx = std::max(half.x * 0.88f, 0.42f);
	float ry = 0.26f;
	glm::vec2 foot(center.x, center.y - half.y + ry * 0.12f);
	glm::u8vec4 core(color.r, color.g, color.b, 235);
	glm::u8vec4 mid(color.r, color.g, color.b, 180);
	glm::u8vec4 edge(color.r, color.g, color.b, 0);
	constexpr int segs = 18;
	constexpr float inner = 0.62f;
	for (int i = 0; i < segs; ++i) {
		float a0 = float(i) / float(segs) * 6.2831853f;
		float a1 = float(i + 1) / float(segs) * 6.2831853f;
		float c0 = std::cos(a0);
		float s0 = std::sin(a0);
		float c1 = std::cos(a1);
		float s1 = std::sin(a1);
		glm::vec3 p0(foot.x + c0 * rx * inner, foot.y + s0 * ry * inner, 0.0f);
		glm::vec3 p1(foot.x + c1 * rx * inner, foot.y + s1 * ry * inner, 0.0f);
		glm::vec3 q0(foot.x + c0 * rx, foot.y + s0 * ry, 0.0f);
		glm::vec3 q1(foot.x + c1 * rx, foot.y + s1 * ry, 0.0f);
		verts.push_back({glm::vec3(foot.x, foot.y, 0.0f), core});
		verts.push_back({p0, mid});
		verts.push_back({p1, mid});
		verts.push_back({p0, mid});
		verts.push_back({q0, edge});
		verts.push_back({q1, edge});
		verts.push_back({p0, mid});
		verts.push_back({q1, edge});
		verts.push_back({p1, mid});
	}
}

}

PlayMode::PlayMode(bool practice) : practice_(practice) {
	int w = 0, h = 0;
	SDL_GetWindowSize(Mode::window, &w, &h);
	if (w > 0 && h > 0) window_size = glm::uvec2((unsigned)w, (unsigned)h);

	GameAudio::play_music(GameAudio::Music::Battle);

	float mx = 0.0f, my = 0.0f;
	SDL_GetMouseState(&mx, &my);
	mouse = glm::vec2(mx, my);

	World::get_world()->play_mode = this;
	World::get_world()->clear();

	glm::vec2 basket_at = 0.5f * (World::get_world()->map.friendly_base_min() + World::get_world()->map.friendly_base_max());
	auto basket = std::make_shared<Ranged>(basket_at, &player_data.basket, Team::BASKET);

	World::get_world()->troops.emplace_back(basket);
	basket_ = basket;

	glm::vec2 enemy_at = 0.5f * (World::get_world()->map.enemy_base_min() + World::get_world()->map.enemy_base_max());
	auto enemy_basket = std::make_shared<Ranged>(enemy_at, &player_data.basket, Team::ENEMY_BASKET);
	World::get_world()->troops.emplace_back(enemy_basket);
	enemy_basket_ = enemy_basket;

	load_troop_sprite();

	player_data = base_data;
	director_data = base_data;

	// set initial resources amount
	World::get_world()->add_player_resources(10);
	World::get_world()->add_director_resources(10);

	spawn_director = std::make_unique<SpawnDirector>(director_data, 15466);
}

PlayMode::~PlayMode() {
	auto release = [](AnimClip &clip) {
		for (GLuint &tex : clip.frames) {
			if (tex != 0) glDeleteTextures(1, &tex);
			tex = 0;
		}
	};
	for (auto &anim : characters_) {
		release(anim.walk);
		release(anim.attack);
	}
	if (projectile_tex_ != 0) glDeleteTextures(1, &projectile_tex_);
	projectile_tex_ = 0;

	if (water_tex_ != 0) glDeleteTextures(1, &water_tex_);
	water_tex_ = 0;

	if (troop_vao_ != 0) glDeleteVertexArrays(1, &troop_vao_);
	if (troop_vbo_ != 0) glDeleteBuffers(1, &troop_vbo_);
	if (bar_vao_ != 0) glDeleteVertexArrays(1, &bar_vao_);
	if (bar_vbo_ != 0) glDeleteBuffers(1, &bar_vbo_);
	troop_vao_ = troop_vbo_ = 0;
	bar_vao_ = bar_vbo_ = 0;
}

static const float troop_size = 0.7f;

static glm::vec2 restart_center() { return glm::vec2(0.0f, -0.42f); }
static glm::vec2 restart_half() { return glm::vec2(0.26f, 0.065f); }

static bool pointer_on_restart(glm::vec2 mouse, glm::uvec2 window) {
	if (window.x == 0 || window.y == 0) return false;
	glm::vec2 at(
		(restart_center().x * 0.5f + 0.5f) * float(window.x),
		(0.5f - restart_center().y * 0.5f) * float(window.y)
	);
	glm::vec2 half = restart_half();
	glm::vec2 span(half.x * 0.5f * float(window.x), half.y * 0.5f * float(window.y));
	return std::abs(mouse.x - at.x) <= span.x && std::abs(mouse.y - at.y) <= span.y;
}

static int anim_frame(float time, int frames) {
	if (frames <= 0) return 0;
	int frame = (int)std::floor(time * 12.0f) % frames;
	if (frame < 0) frame += frames;
	return frame;
}

static char const *selected_troop_name(TroopType type) {
	if (type == TroopType::BANANA_RANGED) return "BananaRanged";
	if (type == TroopType::PINEAPPLE_HEALER) return "PineappleHealer";
	if (type == TroopType::ORANGE_FLYER) return "OrangeFlyer";
	if (type == TroopType::BLUEBERRY_COLLECTOR) return "BlueberryCollector";
	
	return "CherryBrawler";
}

static TroopType troop_type_from_name(std::string const &name) {
	if (name == "BananaRanged") return TroopType::BANANA_RANGED;
	if (name == "PineappleHealer") return TroopType::PINEAPPLE_HEALER;
	if (name == "OrangeFlyer") return TroopType::ORANGE_FLYER;
	if (name == "BlueberryCollector") return TroopType::BLUEBERRY_COLLECTOR;
	
	return TroopType::CHERRY_BRAWLER;
}

static bool troop_overlaps(glm::vec2 pos, glm::vec2 mn, glm::vec2 mx) {
	return pos.x - troop_size <= mx.x && pos.x + troop_size >= mn.x && pos.y - troop_size <= mx.y && pos.y + troop_size >= mn.y;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size_) {
	window_size = window_size_;
	if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.key == SDLK_ESCAPE && practice_) {
		leave_practice_ = true;
		return true;
	}
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt.button.button == SDL_BUTTON_LEFT && practice_) {
		mouse = glm::vec2(evt.button.x, evt.button.y);
		bool start_game = false;
		if (practice_button(mouse, start_game)) return true;
	}
	if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		mouse = glm::vec2(evt.motion.x, evt.motion.y);
		if (box_dragging_) mouse_to_world(box_end_);
		return true;
	}
	if (won_ || lost_) {
		if (evt.type == SDL_EVENT_KEY_DOWN && (evt.key.key == SDLK_RETURN || evt.key.key == SDLK_R)) {
			restart_ = true;
			return true;
		}
		if ((evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN || evt.type == SDL_EVENT_MOUSE_BUTTON_UP)
			&& evt.button.button == SDL_BUTTON_LEFT) {
			mouse = glm::vec2(evt.button.x, evt.button.y);
			if (evt.type == SDL_EVENT_MOUSE_BUTTON_UP && pointer_on_restart(mouse, window_size)) restart_ = true;
			return true;
		}
		return false;
	}
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt.button.button == SDL_BUTTON_LEFT) {
		mouse = glm::vec2(evt.button.x, evt.button.y);
		dragging_ = pointer_on_slot();
		box_dragging_ = !dragging_ && mouse_to_world(box_start_);
		box_end_ = box_start_;
		return true;
	}
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_UP && evt.button.button == SDL_BUTTON_LEFT) {
		mouse = glm::vec2(evt.button.x, evt.button.y);
		if (practice_) {
			bool start_game = false;
			if (practice_button(mouse, start_game)) {
				if (start_game) start_real_ = true;
				else leave_practice_ = true;
				dragging_ = false;
				box_dragging_ = false;
				return true;
			}
		}
		if (dragging_) {
			// place_troop only spends resources when it actually places, so that tells us which sound to play.
			int resources_before = World::get_world()->get_player_resources();
			glm::vec3 hit;
			if (mouse_to_world(hit)) place_troop(glm::vec2(hit));
			GameAudio::play(World::get_world()->get_player_resources() < resources_before ? GameAudio::Sfx::Place : GameAudio::Sfx::PlaceFail);
		}
		if (box_dragging_) {
			glm::vec2 mn = (glm::min(glm::vec2(box_start_), glm::vec2(box_end_)));
			glm::vec2 mx = (glm::max(glm::vec2(box_start_), glm::vec2(box_end_)));

			bool any_selected = false;
			for (auto const &troop : World::get_world()->troops) {
				troop->set_selected(false);
				if (troop->get_team() != Team::BLUE) continue;
				glm::vec2 pos = troop->get_position();
				bool selected = troop_overlaps(pos, mn, mx);
				troop->set_selected(selected);
				if (selected) any_selected = true;
			}
			if (any_selected) GameAudio::play(GameAudio::Sfx::SelectUnits);
		}
		dragging_ = false;
		box_dragging_ = false;
		return true;
	}
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt.button.button == SDL_BUTTON_RIGHT) {
		mouse = glm::vec2(evt.button.x, evt.button.y);
		World *world = World::get_world();
		glm::vec3 hit;
		if (mouse_to_world(hit)) {
			glm::vec2 click = glm::vec2(hit);
			int command_count = 0;
			for (auto &troop : world->troops) {
				if (troop->is_selected()) command_count++;
			}
			if (command_count == 0) return true;
			std::shared_ptr<Troop> target;
			float best = std::numeric_limits<float>::infinity();
			for (auto &troop : world->troops) {
				if (troop->is_dead()) continue;
				glm::vec2 pos = troop->get_position();
				if (troop->get_team() == Team::RED) {
					float dist = glm::distance(pos, click);
					if (troop_overlaps(pos, click, click) && dist < best) {
						best = dist;
						target = troop;
					}
				}
			}

			Order order;
			if (target) {
				order.type = OrderType::Attack;
				order.attack_target = target;
			} else {
				int row = 0;
				int col = 0;
				if (!world->map.world_to_cell(hit, col, row)) return true;
				if (!world->map.is_walkable(col, row)) return true;
				order.type = OrderType::Move;
				order.move_target = click;
				order.arrive_radius = 0.2f;
			}
			for (auto &troop : world->troops) {
				if (troop->is_selected()) {
					troop->give_order(order);
				}
			}
			GameAudio::play(GameAudio::Sfx::Order);
		}
		return true;
	}
	if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.key == SDLK_SPACE) {
		paused_ = !paused_;
		GameAudio::play(paused_ ? GameAudio::Sfx::Pause : GameAudio::Sfx::Resume);
		GameAudio::set_music_level(paused_ ? 0.35f : 1.0f);
		return true;
	}
	return false;
}

void PlayMode::update(float elapsed) {
	if (practice_) {
		time_ += elapsed;
		advance_practice();
		if (leave_practice_ || start_real_) {
			auto keep_alive = shared_from_this();
			if (start_real_) Mode::set_current(std::make_shared< PlayMode >());
			else Mode::set_current(std::make_shared< TitleMode >());
			return;
		}
		World::get_world()->update(elapsed);
		return;
	}

	if (restart_) {
		auto keep_alive = shared_from_this();
		Mode::set_current(std::make_shared< PlayMode >());
		return;
	}

	if (paused_ || won_ || lost_) return;

	time_ += elapsed;
	troop_spawn_timer -= elapsed;

	World::get_world()->update(elapsed);
	spawn_director->update(elapsed);

	if (enemy_basket_.expired()) {
		won_ = true;
		GameAudio::play_jingle(GameAudio::Jingle::Victory);
		dragging_ = false;
		box_dragging_ = false;
		return;
	}
	if (basket_.expired()) {
		lost_ = true;
		GameAudio::play_jingle(GameAudio::Jingle::Defeat);
		dragging_ = false;
		box_dragging_ = false;
		return;
	}

	if (troop_spawn_timer < 0.0f){
		spawn_vegetable_troop();
		troop_spawn_timer = troop_spawn_rate;
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size_) {
	drawable_size = drawable_size_;
	glClearColor(0.20f, 0.11f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	int bar_px = (int)std::lround(float(drawable_size.y) * bar_frac_);
	bar_px = std::clamp(bar_px, 0, (int)drawable_size.y - 1);
	glViewport(0, bar_px, (GLsizei)drawable_size.x, (GLsizei)(drawable_size.y - bar_px));

	glm::mat4 clip = clip_from_world(map_aspect());
	refresh_hover();
	World::get_world()->map.draw(clip, time_, hover_col, hover_row, {});
	draw_troops(clip);
	if (practice_) draw_practice_hints(clip);
	World::get_world()->map.draw_film(glm::mat4(1.0f), time_);
	if (paused_) draw_pause_overlay();
	if (dragging_ && World::get_world()->map.can_place(hover_col, hover_row)) {
		glm::u8vec4 ghost(255, 255, 255, 170);
		CharacterAnim const *anim = find_anim(selected_troop_name(selected_troop), Team::BLUE);
		if (anim != nullptr && anim->walk.ready()) {
		glm::vec2 ghost_half = anim->walk.half;
		glm::vec2 ghost_at = anim->sprite_center(glm::vec2(World::get_world()->map.cell_center(hover_col, hover_row)), false, false);
		glm::vec2 ghost_shadow = ghost_at;
		if (selected_troop == TroopType::ORANGE_FLYER) ghost_at.y += flight_altitude;
		GLuint ghost_tex = anim->walk.frames[0];
		glm::vec2 ghost_uv_min = anim->walk.uv_min;
		glm::vec2 ghost_uv_max = anim->walk.uv_max;
		if (bar_vao_ != 0) {
			std::vector< ColorVertex > shadow;
			add_foot_shadow(shadow, ghost_shadow, ghost_half, glm::u8vec3(18, 10, 6));
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
			glBufferData(GL_ARRAY_BUFFER, shadow.size() * sizeof(ColorVertex), shadow.data(), GL_DYNAMIC_DRAW);
			glUseProgram(color_program->program);
			glBindVertexArray(bar_vao_);
			glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip));
			glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shadow.size());
			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glUseProgram(0);
		}
		draw_sprite(clip, ghost_at, ghost_half, ghost, ghost_tex, ghost_uv_min, ghost_uv_max);
		}
	}

	glViewport(0, 0, (GLsizei)drawable_size.x, (GLsizei)drawable_size.y);
	draw_bar();
	draw_unit_stats();
	if (!practice_) draw_match_clock();
	if (practice_) draw_practice_banner();
	if (won_) draw_victory_overlay();
	else if (lost_) draw_defeat_overlay();

	GL_ERRORS();
}

glm::mat4 PlayMode::clip_from_world(float aspect) const {
	glm::vec3 mn = World::get_world()->map.bounds_min();
	glm::vec3 mx = World::get_world()->map.bounds_max();
	constexpr float margin = 1.2f;
	float half_w = 0.5f * (mx.x - mn.x) + margin;
	float half_h = 0.5f * (mx.y - mn.y) + margin;
	if (half_w < half_h * aspect) half_w = half_h * aspect;
	else half_h = half_w / aspect;
	float cx = 0.5f * (mn.x + mx.x);
	float cy = 0.5f * (mn.y + mx.y);
	return glm::ortho(cx - half_w, cx + half_w, cy - half_h, cy + half_h, -5.0f, 5.0f);
}

namespace {

struct BarPlaces {
	float slot_x = 0.0f;
	float slot_x2 = 0.0f;
	float slot_y = 0.0f;
	float side = 0.0f;
	float coin_cx = 0.0f;
	float coin_cy = 0.0f;
	float coin_r = 0.0f;
	float text_left = 0.0f;
	float text_h = 0.0f;
};

BarPlaces bar_places(glm::uvec2 window, float bar_frac) {
	BarPlaces p;
	float map_h = float(window.y) * (1.0f - bar_frac);
	float bar_h = float(window.y) - map_h;
	float pad = bar_h * 0.16f;
	p.side = bar_h - pad * 2.0f;
	p.coin_r = p.side * 0.28f;
	float gap = pad * 0.55f;
	p.text_h = bar_h * 0.42f;
	p.coin_cx = pad + p.coin_r;
	p.coin_cy = map_h + bar_h * 0.5f;
	p.text_left = p.coin_cx + p.coin_r + gap;
	constexpr float resource_digits = 3.0f;
	float text_w = resource_digits * p.text_h;

	p.slot_x = p.text_left + text_w + gap;
	p.slot_x2 = p.slot_x + p.side + gap + 0.5f;
	p.slot_y = map_h + pad;
	return p;
}

float bar_slot_x(BarPlaces const &p, int index) {
	return p.slot_x + (p.slot_x2 - p.slot_x) * float(index);
}

} // namespace

float PlayMode::map_aspect() const {
	float map_h = float(std::max(window_size.y, 1u)) * (1.0f - bar_frac_);
	return float(std::max(window_size.x, 1u)) / map_h;
}

bool PlayMode::pointer_on_slot(){
	if (window_size.x == 0 || window_size.y == 0) return false;
	BarPlaces slot = bar_places(window_size, bar_frac_);
	int index = 0;
	for (auto const &anim : characters_) {
		if (!anim.show_in_bar) continue;
		float x = bar_slot_x(slot, index);
		++index;
		if (mouse.x >= x && mouse.x <= x + slot.side
			&& mouse.y >= slot.slot_y && mouse.y <= slot.slot_y + slot.side) {
			selected_troop = troop_type_from_name(anim.troop_name);
			return true;
		}
	}
	return false;
}

bool PlayMode::mouse_to_world(glm::vec3 &hit) const {
	if (window_size.x == 0 || window_size.y == 0 || drawable_size.y == 0) return false;
	float map_h = float(window_size.y) * (1.0f - bar_frac_);
	if (map_h <= 1.0f || mouse.y >= map_h) return false;
	glm::mat4 world_from_clip = glm::inverse(clip_from_world(map_aspect()));
	float ndc_x = (mouse.x / float(window_size.x)) * 2.0f - 1.0f;
	float ndc_y = 1.0f - (mouse.y / map_h) * 2.0f;
	glm::vec4 w = world_from_clip * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
	if (w.w == 0.0f) return false;
	w /= w.w;
	hit = glm::vec3(w);
	return true;
}

void PlayMode::refresh_hover() {
	hover_col = -1;
	hover_row = -1;
	if (!dragging_) return;
	glm::vec3 hit;
	if (!mouse_to_world(hit)) return;
	int col = 0;
	int row = 0;
	if (World::get_world()->map.world_to_cell(hit, col, row)) {
		hover_col = col;
		hover_row = row;
	}
}

void PlayMode::place_troop(glm::vec2 spawn_pos) {
	int col = 0;
	int row = 0;
	if (!World::get_world()->map.world_to_cell(glm::vec3(spawn_pos, 0.0f), col, row)) return;
	if (!World::get_world()->map.can_place(col, row)) return;

	int resources_ = World::get_world()->get_player_resources();

	if (selected_troop == TroopType::CHERRY_BRAWLER){
		if (resources_ < player_data.cherry.resource_cost) return;

		World::get_world()->subtract_player_resources(player_data.cherry.resource_cost);
		glm::vec2 pos(World::get_world()->map.cell_center(col, row));

		World::get_world()->troops.push_back(std::make_shared<Brawler>(pos, &player_data.cherry, Team::BLUE));
	} 
	else if (selected_troop == TroopType::BANANA_RANGED){
		if (resources_ < player_data.banana.resource_cost) return;

		World::get_world()->subtract_player_resources(player_data.banana.resource_cost);
		glm::vec2 pos(World::get_world()->map.cell_center(col, row));
		World::get_world()->troops.push_back(std::make_shared<Ranged>(pos, &player_data.banana, Team::BLUE));
	} 
	else if (selected_troop == TroopType::PINEAPPLE_HEALER){
		if (resources_ < player_data.pineapple.resource_cost) return;

		World::get_world()->subtract_player_resources(player_data.pineapple.resource_cost);
		glm::vec2 pos(World::get_world()->map.cell_center(col, row));
		World::get_world()->troops.push_back(std::make_shared<Healer>(pos, &player_data.pineapple, Team::BLUE));
	}
	else if (selected_troop == TroopType::ORANGE_FLYER){
		if (resources_ < player_data.orange.resource_cost) return;

		World::get_world()->subtract_player_resources(player_data.orange.resource_cost);
		glm::vec2 pos(World::get_world()->map.cell_center(col, row));
		World::get_world()->troops.push_back(std::make_shared<Flyer>(pos, &player_data.orange, Team::BLUE));
	}
	else if (selected_troop == TroopType::BLUEBERRY_COLLECTOR) {
		if (resources_ < player_data.blueberry.resource_cost) return;

		World::get_world()->subtract_player_resources(player_data.blueberry.resource_cost);
		glm::vec2 pos(World::get_world()->map.cell_center(col, row));
		World::get_world()->troops.push_back(std::make_shared<Collector>(pos, &player_data.blueberry, Team::BLUE));
	}

	
}

// Temp function for prototype
void PlayMode::spawn_vegetable_troop()
{
	int col = (rand() % 6) + 10; // place between cols 10-16
	int row = (rand() % 11); // place bewteen rows 0-10

	if (!World::get_world()->map.is_walkable(col, row)) return;
	
	glm::vec2 pos(World::get_world()->map.cell_center(col, row));
	World::get_world()->troops.push_back(std::make_shared<Brawler>(pos, &player_data.cherry, Team::RED));
}

namespace {

char const *practice_prompt(int lesson) {
	switch (lesson) {
		case 0: return "Drag the cherry from the bar onto your side.";
		case 1: return "Drag a box around the cherry to select it.";
		case 2: return "Right-click open ground. The flag is one example.";
		case 3: return "Right-click the enemy cherry to attack it.";
		default: return "Destroy the enemy basket on the right to win.";
	}
}

std::shared_ptr< Troop > first_named(char const *name, Team team) {
	for (auto const &troop : World::get_world()->troops) {
		if (troop && !troop->is_dead() && troop->get_team() == team && troop->get_name() == name) return troop;
	}
	return nullptr;
}

bool blue_has_order(OrderType type) {
	for (auto const &troop : World::get_world()->troops) {
		if (!troop || troop->is_dead() || troop->get_team() != Team::BLUE) continue;
		if (troop->get_order().type == type) return true;
	}
	return false;
}

glm::vec2 window_from_clip(glm::vec2 clip, glm::uvec2 window) {
	return glm::vec2(
		(clip.x * 0.5f + 0.5f) * float(window.x),
		(0.5f - clip.y * 0.5f) * float(window.y)
	);
}

} // namespace

void PlayMode::spawn_practice_enemy() {
	Map &map = World::get_world()->map;
	int row = map.rows() / 2;
	for (int col = map.cols() - 1; col >= 0; --col) {
		if (!map.is_walkable(col, row) || map.can_place(col, row)) continue;
		glm::vec2 pos(map.cell_center(col, row));
		World::get_world()->troops.push_back(std::make_shared<Brawler>(
			pos, &player_data.cherry, Team::RED));
		return;
	}
}

void PlayMode::advance_practice() {
	if (lesson_ == 0 && first_named("CherryBrawler", Team::BLUE)) lesson_ = 1;
	if (lesson_ == 1) {
		for (auto const &troop : World::get_world()->troops) {
			if (troop && !troop->is_dead() && troop->get_name() == "CherryBrawler" && troop->is_selected()) {
				lesson_ = 2;
				break;
			}
		}
	}
	if (lesson_ == 2 && blue_has_order(OrderType::Move)) lesson_ = 3;
	if (lesson_ == 3) {
		if (!first_named("CherryBrawler", Team::RED) && !blue_has_order(OrderType::Attack)) spawn_practice_enemy();
		if (blue_has_order(OrderType::Attack)) lesson_ = 4;
	}
}

bool PlayMode::practice_button(glm::vec2 cursor, bool &start_game) const {
	if (!practice_ || lesson_ < 4 || window_size.x == 0 || window_size.y == 0) return false;
	struct Choice { glm::vec2 center; bool start; };
	Choice choices[2] = {
		{glm::vec2(-0.28f, 0.62f), true},
		{glm::vec2(0.28f, 0.62f), false},
	};
	glm::vec2 half(0.20f, 0.065f);
	for (Choice choice : choices) {
		glm::vec2 at = window_from_clip(choice.center, window_size);
		glm::vec2 span(half.x * float(window_size.x), half.y * float(window_size.y));
		if (std::abs(cursor.x - at.x) <= span.x && std::abs(cursor.y - at.y) <= span.y) {
			start_game = choice.start;
			return true;
		}
	}
	return false;
}

void PlayMode::draw_practice_hints(glm::mat4 const &clip) {
	if (bar_vao_ == 0) return;
	struct ColorVertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
	};
	std::vector< ColorVertex > verts;
	auto rect = [&](glm::vec2 mn, glm::vec2 mx, glm::u8vec4 color) {
		verts.push_back({glm::vec3(mn.x, mn.y, 0.0f), color});
		verts.push_back({glm::vec3(mx.x, mn.y, 0.0f), color});
		verts.push_back({glm::vec3(mx.x, mx.y, 0.0f), color});
		verts.push_back({glm::vec3(mn.x, mn.y, 0.0f), color});
		verts.push_back({glm::vec3(mx.x, mx.y, 0.0f), color});
		verts.push_back({glm::vec3(mn.x, mx.y, 0.0f), color});
	};
	float pulse = 0.5f + 0.5f * std::sin(time_ * 4.0f);
	glm::u8vec4 ink(255, 214, 90, (uint8_t)std::lround(140.0f + 80.0f * pulse));

	if (lesson_ == 1 || lesson_ == 2) {
		std::shared_ptr< Troop > fruit;
		for (auto const &troop : World::get_world()->troops) {
			if (troop && !troop->is_dead() && troop->get_team() == Team::BLUE) { fruit = troop; break; }
		}
		if (fruit) {
			glm::vec2 at = fruit->get_position();
			if (lesson_ == 1) {
				float reach = glm::mix(0.35f, 1.5f, pulse);
				rect(at - glm::vec2(reach), at + glm::vec2(reach), glm::u8vec4(255, 214, 90, 50));
				float t = 0.08f;
				rect(at + glm::vec2(-reach, reach - t), at + glm::vec2(reach, reach), ink);
				rect(at + glm::vec2(-reach, -reach), at + glm::vec2(reach, -reach + t), ink);
				rect(at + glm::vec2(-reach, -reach), at + glm::vec2(-reach + t, reach), ink);
				rect(at + glm::vec2(reach - t, -reach), at + glm::vec2(reach, reach), ink);
			} else {
				Map &map = World::get_world()->map;
				int col = 0, row = 0;
				if (map.world_to_cell(glm::vec3(at, 0.0f), col, row)) {
					int dest = std::min(col + 3, map.cols() - 1);
					if (map.is_walkable(dest, row)) {
						glm::vec2 flag(map.cell_center(dest, row));
						rect(flag + glm::vec2(-0.08f, -0.7f), flag + glm::vec2(0.08f, 0.55f), ink);
						verts.push_back({glm::vec3(flag.x, flag.y + 0.85f, 0.0f), ink});
						verts.push_back({glm::vec3(flag.x + 0.7f, flag.y + 0.55f, 0.0f), ink});
						verts.push_back({glm::vec3(flag.x, flag.y + 0.25f, 0.0f), ink});
					}
				}
			}
		}
	}
	if (lesson_ == 3) {
		auto enemy = first_named("CherryBrawler", Team::RED);
		if (enemy) {
			glm::vec2 at = enemy->get_position();
			float radius = glm::mix(0.9f, 1.35f, pulse);
			constexpr int segs = 18;
			for (int i = 0; i < segs; ++i) {
				float a0 = float(i) / float(segs) * 6.2831853f;
				float a1 = float(i + 1) / float(segs) * 6.2831853f;
				glm::vec2 p0 = at + radius * glm::vec2(std::cos(a0), std::sin(a0));
				glm::vec2 p1 = at + radius * glm::vec2(std::cos(a1), std::sin(a1));
				glm::vec2 q0 = at + (radius + 0.12f) * glm::vec2(std::cos(a0), std::sin(a0));
				glm::vec2 q1 = at + (radius + 0.12f) * glm::vec2(std::cos(a1), std::sin(a1));
				verts.push_back({glm::vec3(p0, 0.0f), ink});
				verts.push_back({glm::vec3(q0, 0.0f), ink});
				verts.push_back({glm::vec3(q1, 0.0f), ink});
				verts.push_back({glm::vec3(p0, 0.0f), ink});
				verts.push_back({glm::vec3(q1, 0.0f), ink});
				verts.push_back({glm::vec3(p1, 0.0f), ink});
			}
		}
	}
	if (verts.empty()) return;
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(ColorVertex), verts.data(), GL_DYNAMIC_DRAW);
	glUseProgram(color_program->program);
	glBindVertexArray(bar_vao_);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip));
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

void PlayMode::draw_practice_banner() {
	if (bar_vao_ == 0 || window_size.x == 0 || window_size.y == 0) return;
	struct ColorVertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
	};
	std::vector< ColorVertex > verts;
	auto rect = [&](glm::vec2 mn, glm::vec2 mx, glm::u8vec4 color) {
		verts.push_back({glm::vec3(mn.x, mn.y, 0.0f), color});
		verts.push_back({glm::vec3(mx.x, mn.y, 0.0f), color});
		verts.push_back({glm::vec3(mx.x, mx.y, 0.0f), color});
		verts.push_back({glm::vec3(mn.x, mn.y, 0.0f), color});
		verts.push_back({glm::vec3(mx.x, mx.y, 0.0f), color});
		verts.push_back({glm::vec3(mn.x, mx.y, 0.0f), color});
	};
	std::string prompt = practice_prompt(lesson_);
	float prompt_advance = 0.0f;
	ui_font->layout(prompt, &prompt_advance);
	float prompt_h = 0.05f;
	float prompt_px = (prompt_h * 0.5f * float(window_size.y)) / float(ui_font->pixel_height);
	float prompt_w = prompt_advance * prompt_px * (2.0f / float(window_size.x));
	float pad_x = 0.045f;
	float pad_y = 0.016f;
	float prompt_cy = 0.90f;
	float half_w = std::min(0.5f * prompt_w + pad_x, 0.72f);
	rect(glm::vec2(-half_w, prompt_cy - prompt_h * 0.5f - pad_y), glm::vec2(half_w, prompt_cy + prompt_h * 0.5f + pad_y), glm::u8vec4(236, 222, 186, 230));
	if (lesson_ >= 4) {
		rect(glm::vec2(-0.48f, 0.555f), glm::vec2(-0.08f, 0.685f), glm::u8vec4(196, 58, 52, 255));
		rect(glm::vec2(0.08f, 0.555f), glm::vec2(0.48f, 0.685f), glm::u8vec4(214, 194, 150, 255));
	}
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(ColorVertex), verts.data(), GL_DYNAMIC_DRAW);
	glUseProgram(color_program->program);
	glBindVertexArray(bar_vao_);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	// Clip x and y both span -1..1, so the same scale stretches glyphs on a wide window.
	float px_w = 2.0f / float(window_size.x);
	float px_h = 2.0f / float(window_size.y);
	auto place_text = [&](std::string const &text, glm::vec2 center, float height) {
		float advance = 0.0f;
		ui_font->layout(text, &advance);
		float glyph_px = (height * 0.5f * float(window_size.y)) / float(ui_font->pixel_height);
		float scale_x = glyph_px * px_w;
		float scale_y = glyph_px * px_h;
		glm::vec3 anchor(center.x - 0.5f * advance * scale_x, center.y - kFontCapCenter * height, 0.0f);
		return std::tuple(anchor, glm::vec3(scale_x, 0.0f, 0.0f), glm::vec3(0.0f, scale_y, 0.0f));
	};
	{
		TextRenderer renderer(glm::mat4(1.0f));
		auto [anchor, x_axis, y_axis] = place_text(prompt, glm::vec2(0.0f, prompt_cy), prompt_h);
		renderer.draw_text(*ui_font, prompt, anchor, x_axis, y_axis, glm::u8vec4(27, 16, 18, 255));
		if (lesson_ >= 4) {
			auto label = [&](std::string const &text, glm::vec2 center, glm::u8vec4 color) {
				auto [at, sx, sy] = place_text(text, center, 0.055f);
				renderer.draw_text(*ui_font, text, at, sx, sy, color);
			};
			label("PLAY", glm::vec2(-0.28f, 0.62f), glm::u8vec4(255, 236, 196, 255));
			label("TITLE", glm::vec2(0.28f, 0.62f), glm::u8vec4(27, 16, 18, 255));
		}
	}
}

void PlayMode::load_sprite_png(std::string const &file, GLuint &tex, glm::vec2 &uv_min, glm::vec2 &uv_max, glm::vec2 &half, float height) {
	glm::uvec2 size(0);
	std::vector< glm::u8vec4 > pixels;
	load_png(data_path(file), &size, &pixels, LowerLeftOrigin);
	if (size.x == 0 || size.y == 0 || pixels.empty()) {
		throw std::runtime_error("PlayMode: " + file + " is empty");
	}

	int min_x = (int)size.x;
	int min_y = (int)size.y;
	int max_x = -1;
	int max_y = -1;
	for (int y = 0; y < (int)size.y; ++y) {
		for (int x = 0; x < (int)size.x; ++x) {
			if (pixels[(size_t)y * size.x + (size_t)x].a <= 20) continue;
			min_x = std::min(min_x, x);
			min_y = std::min(min_y, y);
			max_x = std::max(max_x, x);
			max_y = std::max(max_y, y);
		}
	}
	if (max_x < min_x || max_y < min_y) {
		throw std::runtime_error("PlayMode: " + file + " has no visible pixels");
	}

	uv_min = glm::vec2(float(min_x) / float(size.x), float(min_y) / float(size.y));
	uv_max = glm::vec2(float(max_x + 1) / float(size.x), float(max_y + 1) / float(size.y));
	float aspect = float(max_x - min_x + 1) / float(max_y - min_y + 1);
	half = glm::vec2(height * aspect, height) * 0.5f;

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, (GLsizei)size.x, (GLsizei)size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);
}

float PlayMode::load_frames(std::string const &prefix, int count, GLuint *tex, glm::vec2 &uv_min, glm::vec2 &uv_max, glm::vec2 &half, glm::vec2 &offset, float px_scale) {
	struct Image {
		glm::uvec2 size = glm::uvec2(0);
		std::vector< glm::u8vec4 > pixels;
	};
	std::vector< Image > images(count);
	int min_x = 0;
	int min_y = 0;
	int max_x = -1;
	int max_y = -1;
	for (int i = 0; i < count; ++i) {
		std::string file = prefix + std::to_string(i + 1) + ".png";
		load_png(data_path(file), &images[i].size, &images[i].pixels, LowerLeftOrigin);
		if (images[i].size.x == 0 || images[i].size.y == 0 || images[i].pixels.empty()) {
			throw std::runtime_error("PlayMode: " + file + " is empty");
		}
		if (i > 0 && images[i].size != images[0].size) {
			throw std::runtime_error("PlayMode: " + prefix + " frames are not the same size");
		}
		for (int y = 0; y < (int)images[i].size.y; ++y) {
			for (int x = 0; x < (int)images[i].size.x; ++x) {
				if (images[i].pixels[(size_t)y * images[i].size.x + (size_t)x].a <= 20) continue;
				if (max_x < 0) {
					min_x = max_x = x;
					min_y = max_y = y;
				} else {
					min_x = std::min(min_x, x);
					min_y = std::min(min_y, y);
					max_x = std::max(max_x, x);
					max_y = std::max(max_y, y);
				}
			}
		}
	}
	if (max_x < min_x || max_y < min_y) {
		throw std::runtime_error("PlayMode: " + prefix + " frames have no visible pixels");
	}
	glm::uvec2 size = images[0].size;
	uv_min = glm::vec2(float(min_x) / float(size.x), float(min_y) / float(size.y));
	uv_max = glm::vec2(float(max_x + 1) / float(size.x), float(max_y + 1) / float(size.y));
	int crop_w = max_x - min_x + 1;
	int crop_h = max_y - min_y + 1;
	if (px_scale <= 0.0f) px_scale = 1.7f / float(crop_h);
	half = glm::vec2(float(crop_w), float(crop_h)) * px_scale * 0.5f;
	float crop_cx = (float(min_x) + float(max_x + 1)) * 0.5f;
	float crop_cy = (float(min_y) + float(max_y + 1)) * 0.5f;
	offset = glm::vec2(crop_cx - float(size.x) * 0.5f, crop_cy - float(size.y) * 0.5f) * px_scale;

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	for (int i = 0; i < count; ++i) {
		glGenTextures(1, &tex[i]);
		glBindTexture(GL_TEXTURE_2D, tex[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, (GLsizei)size.x, (GLsizei)size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, images[i].pixels.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	return px_scale;
}

CharacterAnim const *PlayMode::find_anim(std::string const &troop_name, Team team) const {
	CharacterAnim const *fallback = nullptr;
	for (auto const &anim : characters_) {
		if (anim.troop_name != troop_name) continue;
		if (anim.team == team) return &anim;
		if (fallback == nullptr) fallback = &anim;
	}
	return fallback;
}

void PlayMode::load_character(CharacterAnimDesc const &desc) {
	CharacterAnim anim;
	anim.troop_name = desc.troop_name;
	anim.team = desc.team;
	anim.art_faces_left = desc.art_faces_left;
	anim.show_in_bar = desc.show_in_bar;
	anim.fit_bar_icon = desc.fit_bar_icon;

	anim.walk.frames.assign((size_t)desc.walk_frames, 0);
	float scale = load_frames(desc.walk_prefix, desc.walk_frames, anim.walk.frames.data(), anim.walk.uv_min, anim.walk.uv_max, anim.walk.half, anim.walk.offset, 0.0f);
	if (desc.attack_prefix != nullptr && desc.attack_prefix[0] != '\0' && desc.attack_frames > 0) {
		anim.attack.frames.assign((size_t)desc.attack_frames, 0);
		load_frames(desc.attack_prefix, desc.attack_frames, anim.attack.frames.data(), anim.attack.uv_min, anim.attack.uv_max, anim.attack.half, anim.attack.offset, scale);
	}
	// The crop sits on the cell center, so the feet land on the bottom edge. Lift the body into the cell.
	constexpr float place_lift = 0.45f;
	anim.walk.offset.y += place_lift;
	if (anim.attack.ready()) anim.attack.offset.y += place_lift;
	characters_.push_back(std::move(anim));
}

void PlayMode::load_troop_sprite() {
	// Add a row here to give a troop name its walk and attack frames.
	// troop_name must match the name passed when the unit is spawned.
	// art_faces_left: unflipped frames look toward -x. These strips look toward +x.
	// show_in_bar puts it in the bottom bar; fit_bar_icon keeps a non-square crop from stretching.
	static CharacterAnimDesc const roster[] = {
		{"CherryBrawler", Team::BLUE, "BlueTeam/Cherry/Walk/CherryBrawler_Walk-", 11, "BlueTeam/Cherry/Attack/CherryBrawler_Attack-", 11, false, true, false},
		{"CherryBrawler", Team::RED, "RedTeam/Cherry/Walk/CherryBrawler_Walk-", 11, "RedTeam/Cherry/Attack/CherryBrawler_Attack-", 11, false, false, false},
		{"BananaRanged", Team::BLUE, "BlueTeam/Banana/Walk/BananaRanged_Walk-", 11, "BlueTeam/Banana/Attack/BananaRanged_Attack-", 16, false, true, true},
		{"BananaRanged", Team::RED, "RedTeam/Banana/Walk/BananaRanged_Walk-", 11, nullptr, 0, false, false, true},
		{"PineappleHealer", Team::BLUE, "BlueTeam/Pineapple/Walk/PineappleHealer_Walk-", 1, "BlueTeam/Pineapple/Attack/PineappleHealer_Attack-", 1, false, true, true},
		{"OrangeFlyer", Team::BLUE, "BlueTeam/Orange/Walk/OrangeFlyer_Walk-", 1, "BlueTeam/Orange/Attack/OrangeFlyer_Attack-", 1, false, true, true},
		{"BlueberryCollector", Team::BLUE, "BlueTeam/Blueberry/Walk/BlueberryCollector_Walk-", 1, "BlueTeam/Blueberry/Attack/BlueberryCollector_Attack-", 1, false, true, true},
	};
	for (auto const &desc : roster) load_character(desc);
	load_sprite_png("Projectiles/banana_projectile.png", projectile_tex_, projectile_uv_min_, projectile_uv_max_, projectile_half_, 0.2f);
	load_sprite_png("Resources/water_drop.png", water_tex_, water_uv_min_, water_uv_max_, water_half_, 0.4f);

	glGenBuffers(1, &troop_vbo_);
	glGenVertexArrays(1, &troop_vao_);
	glBindVertexArray(troop_vao_);
	glBindBuffer(GL_ARRAY_BUFFER, troop_vbo_);


	glVertexAttribPointer(color_texture_program->Position_vec4, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3) + sizeof(glm::u8vec4) + sizeof(glm::vec2), (void *)0);
	glEnableVertexAttribArray(color_texture_program->Position_vec4);
	glVertexAttribPointer(color_texture_program->Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(glm::vec3) + sizeof(glm::u8vec4) + sizeof(glm::vec2), (void *)sizeof(glm::vec3));
	glEnableVertexAttribArray(color_texture_program->Color_vec4);
	glVertexAttribPointer(color_texture_program->TexCoord_vec2, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec3) + sizeof(glm::u8vec4) + sizeof(glm::vec2), (void *)(sizeof(glm::vec3) + sizeof(glm::u8vec4)));
	glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenBuffers(1, &bar_vbo_);
	glGenVertexArrays(1, &bar_vao_);
	glBindVertexArray(bar_vao_);
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glVertexAttribPointer(color_program->Position_vec4, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3) + sizeof(glm::u8vec4), (void *)0);
	glEnableVertexAttribArray(color_program->Position_vec4);
	glVertexAttribPointer(color_program->Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(glm::vec3) + sizeof(glm::u8vec4), (void *)sizeof(glm::vec3));
	glEnableVertexAttribArray(color_program->Color_vec4);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_ERRORS();
}

void PlayMode::draw_troops(glm::mat4 const &clip_from_world) {
	if (troop_vao_ == 0) return;

	// Depth is the feet, not the body. +Y is up the screen, so a larger foot y is farther away.
	auto foot_y = [&](Troop *troop) {
		float y = troop->get_position().y;
		CharacterAnim const *anim = find_anim(troop->get_name(), troop->get_team());
		if (anim == nullptr) return y;
		bool attacking = anim->attacking_pose(troop->get_pose());
		AnimClip const &clip = anim->clip_for(attacking);
		if (!clip.ready()) return y;
		glm::vec2 center = anim->sprite_center(troop->get_position(), attacking, troop->faces_left());
		float feet = center.y - clip.half.y;
		if (troop->get_data_asset() != nullptr && troop->get_data_asset()->is_flying) feet += flight_altitude;
		return feet;
	};
	struct Layered {
		float y = 0.0f;
		Troop *troop = nullptr;
	};
	std::vector< Layered > layered;
	layered.reserve(World::get_world()->troops.size());
	for (auto const &troop : World::get_world()->troops) {
		if (!troop || troop->is_dead()) continue;
		layered.push_back({foot_y(troop.get()), troop.get()});
	}
	std::stable_sort(layered.begin(), layered.end(), [](Layered const &a, Layered const &b) {
		return a.y > b.y;
	});

	if (bar_vao_ != 0) {
		std::vector< ColorVertex > shadows;

		std::set< Troop const * > attack_targets;
		std::set< std::pair<float, float> > move_targets;
		for (auto &troop : World::get_world()->troops) {
			if (!troop || !troop->is_selected()) continue;
			Order const &order = troop->get_order();
			if (order.type == OrderType::Attack) {
				auto target = order.attack_target.lock();
				attack_targets.insert(target.get());
			} else if (order.type == OrderType::Move) {
				move_targets.insert(std::make_pair(order.move_target.x, order.move_target.y));
			}
		}

		for (auto const &layer : layered) {
			Troop *troop = layer.troop;
			CharacterAnim const *anim = find_anim(troop->get_name(), troop->get_team());
			if (anim == nullptr || !anim->walk.ready()) continue;
			bool attacking = anim->attacking_pose(troop->get_pose());
			AnimClip const &clip = anim->clip_for(attacking);
			glm::vec2 half = clip.half;
			glm::vec2 center = anim->sprite_center(troop->get_position(), attacking, troop->faces_left());

			glm::u8vec3 color;
			if (attack_targets.contains(troop))
				color = glm::u8vec3(255, 0, 0);
			else if (troop->is_selected())
				color = glm::u8vec3(255, 215, 0);
			else
				color = glm::u8vec3(18, 10, 6);
			add_foot_shadow(shadows, center, half, color);
		}
		for (auto target : move_targets) {
			add_foot_shadow(shadows, glm::vec2(target.first, target.second), glm::vec2(0.0f), glm::u8vec3(255, 0, 0));
		}

		if (!shadows.empty()) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
			glBufferData(GL_ARRAY_BUFFER, shadows.size() * sizeof(ColorVertex), shadows.data(), GL_DYNAMIC_DRAW);
			glUseProgram(color_program->program);
			glBindVertexArray(bar_vao_);
			glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_world));
			glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shadows.size());
			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glUseProgram(0);
		}
	}

	struct SpriteVertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};
	static_assert(sizeof(SpriteVertex) == sizeof(glm::vec3) + sizeof(glm::u8vec4) + sizeof(glm::vec2));

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(color_texture_program->program);
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(troop_vao_);
	glBindBuffer(GL_ARRAY_BUFFER, troop_vbo_);
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_world));

	glm::u8vec4 white(255, 255, 255, 255);
	struct SpriteQuad {
		float y = 0.0f;
		GLuint tex = 0;
		SpriteVertex verts[6];
	};
	std::vector< SpriteQuad > quads;
	quads.reserve(layered.size() + World::get_world()->projectiles.size() + World::get_world()->water_drops.size());
	auto push_quad = [&](float y, GLuint tex, glm::vec2 bl, glm::vec2 tr, float u0, float u1, float v0, float v1, glm::u8vec4 color) {
		SpriteQuad quad;
		quad.y = y;
		quad.tex = tex;
		quad.verts[0] = {glm::vec3(bl.x, bl.y, 0.0f), color, glm::vec2(u0, v0)};
		quad.verts[1] = {glm::vec3(tr.x, bl.y, 0.0f), color, glm::vec2(u1, v0)};
		quad.verts[2] = {glm::vec3(tr.x, tr.y, 0.0f), color, glm::vec2(u1, v1)};
		quad.verts[3] = {glm::vec3(bl.x, bl.y, 0.0f), color, glm::vec2(u0, v0)};
		quad.verts[4] = {glm::vec3(tr.x, tr.y, 0.0f), color, glm::vec2(u1, v1)};
		quad.verts[5] = {glm::vec3(bl.x, tr.y, 0.0f), color, glm::vec2(u0, v1)};
		quads.push_back(quad);
	};
	for (auto const &layer : layered) {
		Troop *troop = layer.troop;
		CharacterAnim const *anim = find_anim(troop->get_name(), troop->get_team());
		if (anim == nullptr) continue;
		bool attacking = anim->attacking_pose(troop->get_pose());
		AnimClip const &clip = anim->clip_for(attacking);
		if (!clip.ready()) continue;
		float anim_time = attacking ? troop->get_attack_time() : troop->get_walk_time();
		if (attacking && troop->get_attack_rate() > 1e-4f
			&& (troop->get_name() == "BananaRanged" || troop->get_name() == "CherryBrawler"))
			anim_time *= float(clip.count()) / (troop->get_attack_rate() * 12.0f);
		int frame = anim_frame(anim_time, clip.count());
		if (clip.frames[(size_t)frame] == 0) continue;
		bool face_left = troop->faces_left();
		glm::vec2 c = anim->sprite_center(troop->get_position(), attacking, face_left);
		if (troop->get_data_asset() != nullptr && troop->get_data_asset()->is_flying) c.y += flight_altitude;
		float u0 = clip.uv_min.x;
		float u1 = clip.uv_max.x;
		if (anim->mirrored(face_left)) std::swap(u0, u1);
		push_quad(layer.y, clip.frames[(size_t)frame], c - clip.half, c + clip.half, u0, u1, clip.uv_min.y, clip.uv_max.y, white);
	}
	if (projectile_tex_ != 0) {
		for (auto const &projectile : World::get_world()->projectiles) {
			if (!projectile || projectile->is_inactive()) continue;
			glm::vec2 c = projectile->get_position() + glm::vec2(0.0f, 0.45f);
			float u0 = projectile_uv_min_.x;
			float u1 = projectile_uv_max_.x;
			if (projectile->get_velocity().x < 0.0f) std::swap(u0, u1);
			push_quad(projectile->get_position().y, projectile_tex_, c - projectile_half_, c + projectile_half_, u0, u1, projectile_uv_min_.y, projectile_uv_max_.y, white);
		}
	}
	if (water_tex_ != 0) {
		for (auto const &drop : World::get_world()->water_drops) {
			if (!drop) continue;
			glm::vec2 c = drop->position + glm::vec2(0.0f, 0.45f);
			float y = c.y - water_half_.y;
			if (drop->being_carried) {
				for (auto const &troop : World::get_world()->troops) {
					Collector *collector = dynamic_cast<Collector *>(troop.get());
					if (collector == nullptr) continue;
					auto held = collector->carried_drop();
					if (held.get() != drop.get()) continue;
					y = foot_y(troop.get());
					break;
				}
			}
			push_quad(y, water_tex_, c - water_half_, c + water_half_, water_uv_min_.x, water_uv_max_.x, water_uv_min_.y, water_uv_max_.y, white);
		}
	}
	std::stable_sort(quads.begin(), quads.end(), [](SpriteQuad const &a, SpriteQuad const &b) {
		return a.y > b.y;
	});
	for (size_t i = 0; i < quads.size();) {
		size_t j = i + 1;
		while (j < quads.size() && quads[j].tex == quads[i].tex) ++j;
		std::vector< SpriteVertex > verts;
		verts.reserve((j - i) * 6);
		for (size_t k = i; k < j; ++k) verts.insert(verts.end(), quads[k].verts, quads[k].verts + 6);
		glBindTexture(GL_TEXTURE_2D, quads[i].tex);
		glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(SpriteVertex), verts.data(), GL_DYNAMIC_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
		i = j;
	}

	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	if (bar_vao_ != 0) {
		struct ColorVertex {
			glm::vec3 Position;
			glm::u8vec4 Color;
		};
		std::vector< ColorVertex > bars;
		auto quad = [&](glm::vec2 bl, glm::vec2 tr, glm::u8vec4 color) {
			bars.push_back({glm::vec3(bl.x, bl.y, 0.0f), color});
			bars.push_back({glm::vec3(tr.x, bl.y, 0.0f), color});
			bars.push_back({glm::vec3(tr.x, tr.y, 0.0f), color});
			bars.push_back({glm::vec3(bl.x, bl.y, 0.0f), color});
			bars.push_back({glm::vec3(tr.x, tr.y, 0.0f), color});
			bars.push_back({glm::vec3(bl.x, tr.y, 0.0f), color});
		};
		glm::u8vec4 track(36, 22, 18, 230);
		for (auto const &layer : layered) {
			Troop *troop = layer.troop;
			Team team = troop->get_team();
			if (team != Team::BLUE && team != Team::RED && team != Team::BASKET && team != Team::ENEMY_BASKET) continue;
			int max_hp = troop->get_max_health();
			int hp = troop->get_health();
			if (max_hp <= 0 || hp >= max_hp) continue;
			float filled = std::clamp(float(hp) / float(max_hp), 0.0f, 1.0f);
			CharacterAnim const *anim = find_anim(troop->get_name(), troop->get_team());
			bool attacking = anim != nullptr && anim->attacking_pose(troop->get_pose());
			glm::vec2 half = glm::vec2(0.7f);
			glm::vec2 c = troop->get_position();
			if (anim != nullptr && anim->walk.ready()) {
				half = anim->clip_for(attacking).half;
				c = anim->sprite_center(c, attacking, troop->faces_left());
				if (troop->get_data_asset() != nullptr && troop->get_data_asset()->is_flying) c.y += flight_altitude;
			} else if (team == Team::BASKET || team == Team::ENEMY_BASKET) {
				half = glm::vec2(1.6f);
			}
			float width = std::max(half.x * 1.55f, 0.9f);
			float height = 0.14f;
			float y = c.y + half.y + 0.08f;
			glm::vec2 bg0(c.x - width * 0.5f - 0.04f, y - 0.035f);
			glm::vec2 bg1(c.x + width * 0.5f + 0.04f, y + height + 0.035f);
			quad(bg0, bg1, track);
			if (filled <= 0.0f) continue;
			glm::u8vec4 fill(
				(uint8_t)std::lround(glm::mix(188.0f, 58.0f, filled)),
				(uint8_t)std::lround(glm::mix(40.0f, 164.0f, filled)),
				(uint8_t)std::lround(glm::mix(34.0f, 46.0f, filled)),
				255
			);
			quad(glm::vec2(c.x - width * 0.5f, y), glm::vec2(c.x - width * 0.5f + width * filled, y + height), fill);
		}
		if (!bars.empty()) {
			glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
			glBufferData(GL_ARRAY_BUFFER, bars.size() * sizeof(ColorVertex), bars.data(), GL_DYNAMIC_DRAW);
			glUseProgram(color_program->program);
			glBindVertexArray(bar_vao_);
			glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_world));
			glDrawArrays(GL_TRIANGLES, 0, (GLsizei)bars.size());
			glBindVertexArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glUseProgram(0);
		}
	}

	glDisable(GL_BLEND);
}

void PlayMode::draw_sprite(glm::mat4 const &clip, glm::vec2 center, glm::vec2 half, glm::u8vec4 color, GLuint tex, glm::vec2 uv_min, glm::vec2 uv_max) {
	if (tex == 0 || troop_vao_ == 0) return;
	struct SpriteVertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};
	glm::vec2 bl = center - half;
	glm::vec2 tr = center + half;
	SpriteVertex quad[6] = {
		{glm::vec3(bl.x, bl.y, 0.0f), color, glm::vec2(uv_min.x, uv_min.y)},
		{glm::vec3(tr.x, bl.y, 0.0f), color, glm::vec2(uv_max.x, uv_min.y)},
		{glm::vec3(tr.x, tr.y, 0.0f), color, glm::vec2(uv_max.x, uv_max.y)},
		{glm::vec3(bl.x, bl.y, 0.0f), color, glm::vec2(uv_min.x, uv_min.y)},
		{glm::vec3(tr.x, tr.y, 0.0f), color, glm::vec2(uv_max.x, uv_max.y)},
		{glm::vec3(bl.x, tr.y, 0.0f), color, glm::vec2(uv_min.x, uv_max.y)},
	};
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, troop_vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
	glUseProgram(color_texture_program->program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);
	glBindVertexArray(troop_vao_);
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip));
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
}

TroopDataAsset const *PlayMode::asset_for(std::string const &name) const {
	if (name == base_data.cherry.name) return &base_data.cherry;
	if (name == base_data.banana.name) return &base_data.banana;
	if (name == base_data.pineapple.name) return &base_data.pineapple;
	if (name == base_data.orange.name) return &base_data.orange;
	if (name == base_data.blueberry.name) return &base_data.blueberry;
	if (name == base_data.basket.name) return &base_data.basket;
	return nullptr;
}

void PlayMode::draw_troop_description() {
	if (bar_vao_ == 0 || window_size.x == 0 || window_size.y == 0) return;

	TroopDataAsset const *asset = nullptr;
	BarPlaces slot = bar_places(window_size, bar_frac_);
	int index = 0;
	for (auto const &anim : characters_) {
		if (!anim.show_in_bar) continue;
		float x = bar_slot_x(slot, index);
		++index;
		if (mouse.x >= x && mouse.x <= x + slot.side
			&& mouse.y >= slot.slot_y && mouse.y <= slot.slot_y + slot.side) {
			asset = asset_for(anim.troop_name);
			break;
		}
	}
	if (asset == nullptr || (asset->label.empty() && asset->description.empty())) return;

	std::string title = asset->label;
	std::string body = asset->description;

	std::string first = body;
	std::string second;
	size_t split = first.find('\n');
	if (split != std::string::npos) {
		second = first.substr(split + 1);
		first = first.substr(0, split);
	}

	float title_px = float(window_size.y) * 0.034f;
	float body_px = float(window_size.y) * 0.026f;
	float pad = body_px * 0.7f;
	float gap = body_px * 0.28f;
	auto text_px = [&](std::string const &text, float line_px) {
		if (text.empty() || ui_font->pixel_height == 0) return 0.0f;
		float advance = 0.0f;
		ui_font->layout(text, &advance);
		return advance * (line_px / float(ui_font->pixel_height));
	};
	float card_w = std::max(text_px(title, title_px), std::max(text_px(first, body_px), text_px(second, body_px))) + pad * 2.0f;
	float card_h = pad + title_px + gap + body_px;
	if (!second.empty()) card_h += gap + body_px;
	card_h += pad;

	float px_w = 2.0f / float(window_size.x);
	float px_h = 2.0f / float(window_size.y);
	float gap_px = 14.0f;
	float left = mouse.x + gap_px;
	float top_px = mouse.y - gap_px - card_h;
	if (left + card_w > float(window_size.x) - 8.0f) left = mouse.x - gap_px - card_w;
	if (top_px < 8.0f) top_px = mouse.y + gap_px;
	left = std::clamp(left, 8.0f, std::max(8.0f, float(window_size.x) - card_w - 8.0f));
	top_px = std::clamp(top_px, 8.0f, std::max(8.0f, float(window_size.y) - card_h - 8.0f));

	glm::vec2 card_min(-1.0f + left * px_w, 1.0f - (top_px + card_h) * px_h);
	glm::vec2 card_max(-1.0f + (left + card_w) * px_w, 1.0f - top_px * px_h);

	struct ColorVertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
	};
	auto quad = [](glm::vec2 mn, glm::vec2 mx, glm::u8vec4 color, ColorVertex *out) {
		out[0] = {glm::vec3(mn, 0.0f), color};
		out[1] = {glm::vec3(mx.x, mn.y, 0.0f), color};
		out[2] = {glm::vec3(mx, 0.0f), color};
		out[3] = {glm::vec3(mn, 0.0f), color};
		out[4] = {glm::vec3(mx, 0.0f), color};
		out[5] = {glm::vec3(mn.x, mx.y, 0.0f), color};
	};
	ColorVertex card[12];
	float border = 3.0f;
	quad(card_min - glm::vec2(border * px_w, border * px_h), card_max + glm::vec2(border * px_w, border * px_h), glm::u8vec4(92, 48, 28, 150), card);
	quad(card_min, card_max, glm::u8vec4(248, 236, 204, 170), card + 6);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(card), card, GL_DYNAMIC_DRAW);
	glUseProgram(color_program->program);
	glBindVertexArray(bar_vao_);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glDrawArrays(GL_TRIANGLES, 0, 12);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	{
		TextRenderer renderer(glm::mat4(1.0f));
		auto draw_line = [&](std::string const &text, float y_px, float line_px, glm::u8vec4 color) {
			if (text.empty()) return;
			float scale = line_px / float(ui_font->pixel_height);
			glm::vec3 anchor(
				-1.0f + (left + pad) * px_w,
				1.0f - (y_px + line_px) * px_h,
				0.0f
			);
			renderer.draw_text(*ui_font, text, anchor, glm::vec3(scale * px_w, 0.0f, 0.0f), glm::vec3(0.0f, scale * px_h, 0.0f), color);
		};
		float y = top_px + pad;
		draw_line(title, y, title_px, glm::u8vec4(176, 48, 40, 255));
		y += title_px + gap;
		draw_line(first, y, body_px, glm::u8vec4(27, 16, 18, 255));
		if (!second.empty()) draw_line(second, y + body_px + gap, body_px, glm::u8vec4(27, 16, 18, 255));
	}
	glDisable(GL_BLEND);
}

// Counts living fighters on each side; the baskets are bases, so they aren't included.
void PlayMode::draw_unit_stats() {
	if (bar_vao_ == 0 || window_size.x == 0 || window_size.y == 0) return;

	int blues = 0;
	int reds = 0;
	for (auto const &troop : World::get_world()->troops) {
		if (!troop || troop->is_dead()) continue;
		if (troop->get_team() == Team::BLUE) ++blues;
		else if (troop->get_team() == Team::RED) ++reds;
	}

	float text_px = float(window_size.y) * 0.034f;
	float margin_px = text_px * 0.55f;
	float row_px = text_px * 1.55f;
	auto chip_center = [&](int index) {
		float x = margin_px;
		float y = margin_px + text_px * 0.5f + row_px * float(index);
		return glm::vec2(
			(x / float(window_size.x)) * 2.0f - 1.0f,
			1.0f - (y / float(window_size.y)) * 2.0f
		);
	};

	struct Chip {
		std::string text;
		glm::vec2 anchor_left;
		glm::u8vec4 fill;
	};
	Chip chips[2] = {
		{"BLUE " + std::to_string(blues), chip_center(0), glm::u8vec4(36, 84, 176, 235)},
		{"RED " + std::to_string(reds), chip_center(1), glm::u8vec4(176, 42, 48, 235)},
	};

	float px_w = 2.0f / float(window_size.x);
	float px_h = 2.0f / float(window_size.y);
	float scale = text_px / float(ui_font->pixel_height);
	float pad_x = text_px * 0.42f;
	float pad_y = text_px * 0.22f;
	std::vector< ColorVertex > chips_mesh;
	glm::vec3 anchors[2];
	for (int i = 0; i < 2; ++i) {
		float advance = 0.0f;
		ui_font->layout(chips[i].text, &advance);
		float text_w = advance * scale;
		float left = chips[i].anchor_left.x;
		float cy = chips[i].anchor_left.y;
		glm::vec2 mn(left - pad_x * px_w, cy - (text_px * 0.5f + pad_y) * px_h);
		glm::vec2 mx(left + (text_w + pad_x) * px_w, cy + (text_px * 0.5f + pad_y) * px_h);
		add_round_rect(chips_mesh, mn, mx, pad_y * px_w, pad_y * px_h, glm::u8vec4(27, 16, 18, 255));
		float inset = text_px * 0.06f;
		add_round_rect(chips_mesh,
			mn + glm::vec2(inset * px_w, inset * px_h),
			mx - glm::vec2(inset * px_w, inset * px_h),
			pad_y * px_w, pad_y * px_h, chips[i].fill);
		anchors[i] = glm::vec3(left, cy - kFontCapCenter * text_px * px_h, 0.0f);
	}

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glBufferData(GL_ARRAY_BUFFER, chips_mesh.size() * sizeof(ColorVertex), chips_mesh.data(), GL_DYNAMIC_DRAW);
	glUseProgram(color_program->program);
	glBindVertexArray(bar_vao_);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)chips_mesh.size());
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
	{
		TextRenderer renderer(glm::mat4(1.0f));
		glm::vec3 x_axis(scale * px_w, 0.0f, 0.0f);
		glm::vec3 y_axis(0.0f, scale * px_h, 0.0f);
		glm::u8vec4 cream(255, 236, 196, 255);
		renderer.draw_text(*ui_font, chips[0].text, anchors[0], x_axis, y_axis, cream);
		renderer.draw_text(*ui_font, chips[1].text, anchors[1], x_axis, y_axis, cream);
	}
	glDisable(GL_BLEND);
}

void PlayMode::draw_match_clock() {
	if (bar_vao_ == 0 || window_size.x == 0 || window_size.y == 0) return;

	int remain = (int)std::ceil(World::get_world()->match_time_remaining);
	if (remain < 0) remain = 0;
	int minutes = remain / 60;
	int seconds = remain % 60;
	std::string text = std::to_string(minutes) + ":" + (seconds < 10 ? "0" : "") + std::to_string(seconds);
	bool urgent = remain <= 60;

	float text_px = float(window_size.y) * 0.04f;
	float advance = 0.0f;
	ui_font->layout(text, &advance);
	float scale = text_px / float(ui_font->pixel_height);
	float text_w = advance * scale;
	float pad_x = text_px * 0.55f;
	float pad_y = text_px * 0.28f;
	float px_w = 2.0f / float(window_size.x);
	float px_h = 2.0f / float(window_size.y);
	float top = text_px * 0.45f;
	float cy = 1.0f - (top + text_px * 0.5f) * px_h;
	glm::vec2 mn(-(text_w * 0.5f + pad_x) * px_w, cy - (text_px * 0.5f + pad_y) * px_h);
	glm::vec2 mx( (text_w * 0.5f + pad_x) * px_w, cy + (text_px * 0.5f + pad_y) * px_h);

	glm::u8vec4 fill = urgent ? glm::u8vec4(176, 42, 48, 240) : glm::u8vec4(236, 222, 186, 235);
	glm::u8vec4 glyph = urgent ? glm::u8vec4(255, 236, 196, 255) : glm::u8vec4(27, 16, 18, 255);
	std::vector< ColorVertex > mesh;
	add_round_rect(mesh, mn, mx, pad_y * px_w, pad_y * px_h, glm::u8vec4(27, 16, 18, 255));
	float inset = text_px * 0.06f;
	add_round_rect(mesh, mn + glm::vec2(inset * px_w, inset * px_h), mx - glm::vec2(inset * px_w, inset * px_h), pad_y * px_w, pad_y * px_h, fill);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(ColorVertex), mesh.data(), GL_DYNAMIC_DRAW);
	glUseProgram(color_program->program);
	glBindVertexArray(bar_vao_);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh.size());
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
	{
		TextRenderer renderer(glm::mat4(1.0f));
		glm::vec3 anchor(-0.5f * text_w * px_w, cy - kFontCapCenter * text_px * px_h, 0.0f);
		renderer.draw_text(*ui_font, text, anchor, glm::vec3(scale * px_w, 0.0f, 0.0f), glm::vec3(0.0f, scale * px_h, 0.0f), glyph);
	}
	glDisable(GL_BLEND);
}

void PlayMode::draw_victory_overlay() {
	draw_end_overlay("VICTORY!", glm::u8vec4(255, 214, 96, 255));
}

void PlayMode::draw_defeat_overlay() {
	draw_end_overlay("DEFEAT!", glm::u8vec4(214, 64, 48, 255));
}

void PlayMode::draw_end_overlay(std::string const &label, glm::u8vec4 color) {
	if (bar_vao_ == 0 || window_size.x == 0 || window_size.y == 0) return;

	glm::u8vec4 dim(12, 6, 4, 150);
	ColorVertex screen[6] = {
		{glm::vec3(-1.0f, -1.0f, 0.0f), dim},
		{glm::vec3(1.0f, -1.0f, 0.0f), dim},
		{glm::vec3(1.0f, 1.0f, 0.0f), dim},
		{glm::vec3(-1.0f, -1.0f, 0.0f), dim},
		{glm::vec3(1.0f, 1.0f, 0.0f), dim},
		{glm::vec3(-1.0f, 1.0f, 0.0f), dim},
	};

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(screen), screen, GL_DYNAMIC_DRAW);
	glUseProgram(color_program->program);
	glBindVertexArray(bar_vao_);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glDrawArrays(GL_TRIANGLES, 0, 6);

	glm::vec2 restart_min = restart_center() - restart_half();
	glm::vec2 restart_max = restart_center() + restart_half();
	glm::u8vec4 restart_fill(196, 58, 52, 255);
	ColorVertex restart_rect[6] = {
		{glm::vec3(restart_min, 0.0f), restart_fill},
		{glm::vec3(restart_max.x, restart_min.y, 0.0f), restart_fill},
		{glm::vec3(restart_max, 0.0f), restart_fill},
		{glm::vec3(restart_min, 0.0f), restart_fill},
		{glm::vec3(restart_max, 0.0f), restart_fill},
		{glm::vec3(restart_min.x, restart_max.y, 0.0f), restart_fill},
	};
	glBufferData(GL_ARRAY_BUFFER, sizeof(restart_rect), restart_rect, GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	float px_w = 2.0f / float(window_size.x);
	float px_h = 2.0f / float(window_size.y);
	float line_px = float(window_size.y) * 0.18f;
	float scale = line_px / float(title_font->pixel_height);
	glm::vec3 x_axis(scale * px_w, 0.0f, 0.0f);
	glm::vec3 y_axis(0.0f, scale * px_h, 0.0f);
	float advance = 0.0f;
	title_font->layout(label, &advance);
	glm::vec3 anchor(-0.5f * advance * scale * px_w, -kFontCapCenter * line_px * px_h, 0.0f);

	{
		TextRenderer renderer(glm::mat4(1.0f));
		renderer.draw_text(*title_font, label, anchor + glm::vec3(4.0f * px_w, -4.0f * px_h, 0.0f), x_axis, y_axis, glm::u8vec4(27, 16, 18, 255));
		// The font has one regular weight; a few offset copies make the headline read as bold.
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				renderer.draw_text(*title_font, label, anchor + glm::vec3(float(dx) * 1.5f * px_w, float(dy) * 1.5f * px_h, 0.0f), x_axis, y_axis, color);
			}
		}

		std::string restart_label = "RESTART";
		float restart_advance = 0.0f;
		ui_font->layout(restart_label, &restart_advance);
		float restart_h = restart_half().y * 1.15f;
		float glyph_px = (restart_h * 0.5f * float(window_size.y)) / float(ui_font->pixel_height);
		float restart_sx = glyph_px * px_w;
		float restart_sy = glyph_px * px_h;
		glm::vec3 restart_at(
			restart_center().x - 0.5f * restart_advance * restart_sx,
			restart_center().y - kFontCapCenter * restart_h,
			0.0f
		);
		renderer.draw_text(*ui_font, restart_label, restart_at, glm::vec3(restart_sx, 0.0f, 0.0f), glm::vec3(0.0f, restart_sy, 0.0f), glm::u8vec4(255, 236, 196, 255));
	}
	glDisable(GL_BLEND);
}

// Drawn in the map viewport's clip space, so the bottom bar stays undimmed.
void PlayMode::draw_pause_overlay() {
	if (bar_vao_ == 0) return;

	struct ColorVertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
	};
	std::vector< ColorVertex > verts;
	auto rect = [&](glm::vec2 mn, glm::vec2 mx, glm::u8vec4 color) {
		verts.push_back({glm::vec3(mn.x, mn.y, 0.0f), color});
		verts.push_back({glm::vec3(mx.x, mn.y, 0.0f), color});
		verts.push_back({glm::vec3(mx.x, mx.y, 0.0f), color});
		verts.push_back({glm::vec3(mn.x, mn.y, 0.0f), color});
		verts.push_back({glm::vec3(mx.x, mx.y, 0.0f), color});
		verts.push_back({glm::vec3(mn.x, mx.y, 0.0f), color});
	};

	rect(glm::vec2(-1.0f), glm::vec2(1.0f), glm::u8vec4(12, 6, 4, 90));

	// Two-bar pause icon, top center. Widths are divided by aspect so it isn't stretched.
	float aspect = map_aspect();
	glm::vec2 half(0.03f / aspect, 0.08f);
	float offset = 0.055f / aspect;
	float icon_y = 0.84f;
	glm::vec2 shadow(0.008f / aspect, -0.008f);
	for (float side : {-1.0f, 1.0f}) {
		glm::vec2 c(side * offset, icon_y);
		rect(c - half + shadow, c + half + shadow, glm::u8vec4(27, 16, 18, 200));
		rect(c - half, c + half, glm::u8vec4(255, 236, 196, 255));
	}

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(ColorVertex), verts.data(), GL_DYNAMIC_DRAW);
	glUseProgram(color_program->program);
	glBindVertexArray(bar_vao_);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
}

void PlayMode::draw_bar() {
	if (bar_vao_ == 0 || window_size.x == 0 || window_size.y == 0) return;

	float y1 = -1.0f + 2.0f * bar_frac_;
	glm::u8vec4 wood(96, 62, 40, 255);
	glm::u8vec4 lip(214, 176, 122, 255);
	ColorVertex panel[12] = {
		{glm::vec3(-1.0f, -1.0f, 0.0f), wood},
		{glm::vec3(1.0f, -1.0f, 0.0f), wood},
		{glm::vec3(1.0f, y1, 0.0f), wood},
		{glm::vec3(-1.0f, -1.0f, 0.0f), wood},
		{glm::vec3(1.0f, y1, 0.0f), wood},
		{glm::vec3(-1.0f, y1, 0.0f), wood},
		{glm::vec3(-1.0f, y1 - 0.012f, 0.0f), lip},
		{glm::vec3(1.0f, y1 - 0.012f, 0.0f), lip},
		{glm::vec3(1.0f, y1, 0.0f), lip},
		{glm::vec3(-1.0f, y1 - 0.012f, 0.0f), lip},
		{glm::vec3(1.0f, y1, 0.0f), lip},
		{glm::vec3(-1.0f, y1, 0.0f), lip},
	};

	BarPlaces place = bar_places(window_size, bar_frac_);
	float side = place.side;
	auto ndc_x = [&](float x) { return (x / float(window_size.x)) * 2.0f - 1.0f; };
	auto ndc_y = [&](float y) { return 1.0f - (y / float(window_size.y)) * 2.0f; };
	auto ndc_w = [&](float px) { return px / float(window_size.x) * 2.0f; };
	auto ndc_h = [&](float px) { return px / float(window_size.y) * 2.0f; };
	int resources = World::get_world()->get_player_resources();
	float rad_px = side * 0.18f;
	std::vector< ColorVertex > wells;
	int slot_index = 0;
	for (auto const &anim : characters_) {
		if (!anim.show_in_bar) continue;
		float slot_x = bar_slot_x(place, slot_index);
		++slot_index;
		TroopDataAsset const *asset = asset_for(anim.troop_name);
		bool selected = troop_type_from_name(anim.troop_name) == selected_troop;
		bool affordable = asset == nullptr || resources >= asset->resource_cost;
		float outline = side * (selected ? 0.07f : 0.04f);
		glm::vec2 inner_min(ndc_x(slot_x), ndc_y(place.slot_y + side));
		glm::vec2 inner_max(ndc_x(slot_x + side), ndc_y(place.slot_y));
		glm::vec2 outer_min(ndc_x(slot_x - outline), ndc_y(place.slot_y + side + outline));
		glm::vec2 outer_max(ndc_x(slot_x + side + outline), ndc_y(place.slot_y - outline));
		glm::u8vec4 edge = selected ? glm::u8vec4(255, 210, 70, 255) : glm::u8vec4(27, 16, 18, 255);
		glm::u8vec4 fill = affordable ? glm::u8vec4(62, 38, 24, 255) : glm::u8vec4(38, 26, 20, 255);
		add_round_rect(wells, outer_min, outer_max, ndc_w(rad_px + outline), ndc_h(rad_px + outline), edge);
		add_round_rect(wells, inner_min, inner_max, ndc_w(rad_px), ndc_h(rad_px), fill);
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(panel), panel, GL_DYNAMIC_DRAW);
	glUseProgram(color_program->program);
	glBindVertexArray(bar_vao_);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glDrawArrays(GL_TRIANGLES, 0, 12);
	if (!wells.empty()) {
		glBufferData(GL_ARRAY_BUFFER, wells.size() * sizeof(ColorVertex), wells.data(), GL_DYNAMIC_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)wells.size());
	}
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	// Scale every portrait so its long side matches, then clip anything past the slot.
	constexpr float icon_scale = 1.25f;
	auto cover_slot = [&](glm::vec2 sprite_half) {
		float aspect = sprite_half.x / std::max(sprite_half.y, 1e-4f);
		float icon_w = side * icon_scale;
		float icon_h = side * icon_scale;
		if (aspect >= 1.0f) icon_h = icon_w / aspect;
		else icon_w = icon_h * aspect;
		return glm::vec2(icon_w / float(window_size.x), icon_h / float(window_size.y));
	};
	auto clip_slot = [&](float slot_x) {
		float sx = float(drawable_size.x) / float(std::max(window_size.x, 1u));
		float sy = float(drawable_size.y) / float(std::max(window_size.y, 1u));
		int x = (int)std::floor(slot_x * sx);
		int y = (int)std::floor((float(window_size.y) - (place.slot_y + side)) * sy);
		int w = std::max(1, (int)std::ceil(side * sx));
		int h = std::max(1, (int)std::ceil(side * sy));
		glEnable(GL_SCISSOR_TEST);
		glScissor(x, y, w, h);
	};
	struct CostBadge {
		std::string text;
		glm::vec2 center;
		float height = 0.0f;
		glm::u8vec4 color;
	};
	std::vector< CostBadge > costs;
	std::vector< ColorVertex > badges;
	int icon_index = 0;
	for (auto const &anim : characters_) {
		if (!anim.show_in_bar || !anim.walk.ready()) continue;
		float x = bar_slot_x(place, icon_index);
		++icon_index;
		TroopDataAsset const *asset = asset_for(anim.troop_name);
		bool affordable = asset == nullptr || resources >= asset->resource_cost;
		glm::vec2 icon_center(ndc_x(x + side * 0.5f), ndc_y(place.slot_y + side * 0.5f));
		glm::vec2 icon_half = cover_slot(anim.walk.half);
		glm::u8vec4 tint = affordable ? glm::u8vec4(255, 255, 255, 255) : glm::u8vec4(110, 100, 96, 255);
		clip_slot(x);
		draw_sprite(glm::mat4(1.0f), icon_center, icon_half, tint, anim.walk.frames[0], anim.walk.uv_min, anim.walk.uv_max);
		glDisable(GL_SCISSOR_TEST);
		if (asset == nullptr) continue;
		float badge_h = side * 0.32f;
		float badge_w = side * 0.40f;
		float pad = side * 0.07f;
		float bx1 = x + side - pad;
		float by1 = place.slot_y + side - pad;
		glm::vec2 bmin(ndc_x(bx1 - badge_w), ndc_y(by1));
		glm::vec2 bmax(ndc_x(bx1), ndc_y(by1 - badge_h));
		add_round_rect(badges, bmin, bmax, ndc_w(badge_h * 0.35f), ndc_h(badge_h * 0.35f), glm::u8vec4(27, 16, 18, 230));
		costs.push_back(CostBadge{
			std::to_string(asset->resource_cost),
			0.5f * (bmin + bmax),
			ndc_h(badge_h * 0.72f),
			affordable ? glm::u8vec4(255, 236, 196, 255) : glm::u8vec4(170, 130, 120, 255)
		});
	}
	if (!badges.empty()) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
		glBufferData(GL_ARRAY_BUFFER, badges.size() * sizeof(ColorVertex), badges.data(), GL_DYNAMIC_DRAW);
		glUseProgram(color_program->program);
		glBindVertexArray(bar_vao_);
		glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)badges.size());
		glBindVertexArray(0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUseProgram(0);
		{
			TextRenderer renderer(glm::mat4(1.0f));
			for (CostBadge const &cost : costs) {
				float advance = 0.0f;
				ui_font->layout(cost.text, &advance);
				float glyph_px = cost.height * 0.5f * float(window_size.y);
				float unit = glyph_px / float(ui_font->pixel_height);
				float sx = unit * 2.0f / float(window_size.x);
				float sy = unit * 2.0f / float(window_size.y);
				glm::vec3 at(cost.center.x - 0.5f * advance * sx, cost.center.y - kFontCapCenter * cost.height, 0.0f);
				renderer.draw_text(*ui_font, cost.text, at, glm::vec3(sx, 0.0f, 0.0f), glm::vec3(0.0f, sy, 0.0f), cost.color);
			}
		}
		glDisable(GL_BLEND);
	}

	float coin_cx = ndc_x(place.coin_cx);
	float coin_cy = ndc_y(place.coin_cy);
	float coin_rx = ndc_w(place.coin_r);
	float coin_ry = ndc_h(place.coin_r);
	glm::u8vec4 gold(232, 176, 48, 255);
	constexpr int segs = 16;
	ColorVertex coin[segs * 3];
	for (int i = 0; i < segs; ++i) {
		float a0 = float(i) / float(segs) * 6.2831853f;
		float a1 = float(i + 1) / float(segs) * 6.2831853f;
		coin[i * 3 + 0] = {glm::vec3(coin_cx, coin_cy, 0.0f), gold};
		coin[i * 3 + 1] = {glm::vec3(coin_cx + std::cos(a0) * coin_rx, coin_cy + std::sin(a0) * coin_ry, 0.0f), gold};
		coin[i * 3 + 2] = {glm::vec3(coin_cx + std::cos(a1) * coin_rx, coin_cy + std::sin(a1) * coin_ry, 0.0f), gold};
	}
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(coin), coin, GL_DYNAMIC_DRAW);
	glUseProgram(color_program->program);
	glBindVertexArray(bar_vao_);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glDrawArrays(GL_TRIANGLES, 0, segs * 3);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	std::string label = std::to_string(World::get_world()->get_player_resources());
	float text_h = ndc_h(place.text_h);
	glm::vec3 anchor(ndc_x(place.text_left), coin_cy - kFontCapCenter * text_h, 0.0f);
	glm::u8vec4 ink(255, 236, 196, 255);
	float scale_x = ndc_w(place.text_h) / float(ui_font->pixel_height);
	float scale_y = text_h / float(ui_font->pixel_height);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	{
		TextRenderer renderer(glm::mat4(1.0f));
		renderer.draw_text(*ui_font, label, anchor, glm::vec3(scale_x, 0.0f, 0.0f), glm::vec3(0.0f, scale_y, 0.0f), ink);
	}
	glDisable(GL_BLEND);

	if (practice_ && lesson_ == 0) {
		slot_index = 0;
		for (auto const &anim : characters_) {
			if (!anim.show_in_bar) continue;
			if (slot_index == 0) {
				float x = bar_slot_x(place, slot_index);
				float grow = 6.0f + 4.0f * std::sin(time_ * 5.0f);
				glm::vec2 mn(ndc_x(x - grow), ndc_y(place.slot_y + side + grow));
				glm::vec2 mx(ndc_x(x + side + grow), ndc_y(place.slot_y - grow));
				float t = ndc_w(5.0f);
				ColorVertex frame[24];
				auto edge = [&](int base, glm::vec2 a, glm::vec2 b) {
					glm::u8vec4 gold(255, 210, 70, 255);
					frame[base + 0] = {glm::vec3(a, 0.0f), gold};
					frame[base + 1] = {glm::vec3(b.x, a.y, 0.0f), gold};
					frame[base + 2] = {glm::vec3(b, 0.0f), gold};
					frame[base + 3] = {glm::vec3(a, 0.0f), gold};
					frame[base + 4] = {glm::vec3(b, 0.0f), gold};
					frame[base + 5] = {glm::vec3(a.x, b.y, 0.0f), gold};
				};
				edge(0, mn, glm::vec2(mx.x, mn.y + t));
				edge(6, glm::vec2(mn.x, mx.y - t), mx);
				edge(12, mn, glm::vec2(mn.x + t, mx.y));
				edge(18, glm::vec2(mx.x - t, mn.y), mx);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
				glBufferData(GL_ARRAY_BUFFER, sizeof(frame), frame, GL_DYNAMIC_DRAW);
				glUseProgram(color_program->program);
				glBindVertexArray(bar_vao_);
				glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
				glDrawArrays(GL_TRIANGLES, 0, 24);
				glBindVertexArray(0);
				glBindBuffer(GL_ARRAY_BUFFER, 0);
				glUseProgram(0);
				glDisable(GL_BLEND);
				break;
			}
			++slot_index;
		}
	}

	if (dragging_ && !World::get_world()->map.can_place(hover_col, hover_row)) {
		glm::vec2 cursor(ndc_x(mouse.x), ndc_y(mouse.y));
		CharacterAnim const *anim = find_anim(selected_troop_name(selected_troop), Team::BLUE);
		if (anim != nullptr && anim->walk.ready()) {
			float ghost_aspect = anim->walk.half.x / std::max(anim->walk.half.y, 1e-4f);
			float ghost_w = side;
			float ghost_h = side;
			if (anim->fit_bar_icon) {
				if (ghost_aspect >= 1.0f) ghost_h = side / ghost_aspect;
				else ghost_w = side * ghost_aspect;
			}
			glm::vec2 ghost_half(ghost_w / float(window_size.x), ghost_h / float(window_size.y));
			draw_sprite(glm::mat4(1.0f), cursor, ghost_half, glm::u8vec4(255, 255, 255, 210), anim->walk.frames[0], anim->walk.uv_min, anim->walk.uv_max);
		}
	}

	draw_troop_description();
}
