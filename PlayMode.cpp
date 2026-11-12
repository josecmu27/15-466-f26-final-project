#include "PlayMode.hpp"
#include "Ranged.hpp"

#include "ColorProgram.hpp"
#include "ColorTextureProgram.hpp"
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
#include <vector>
#include <limits>
#include <set>
#include <utility>

namespace {

struct ColorVertex {
	glm::vec3 Position;
	glm::u8vec4 Color;
};

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

PlayMode::PlayMode() {
	int w = 0, h = 0;
	SDL_GetWindowSize(Mode::window, &w, &h);
	if (w > 0 && h > 0) window_size = glm::uvec2((unsigned)w, (unsigned)h);

	float mx = 0.0f, my = 0.0f;
	SDL_GetMouseState(&mx, &my);
	mouse = glm::vec2(mx, my);

	World::get_world()->play_mode = this;

	glm::vec2 basket_at = 0.5f * (World::get_world()->map.friendly_base_min() + World::get_world()->map.friendly_base_max());
	auto basket = std::make_shared<Brawler>(basket_at, "Basket", 
                                1000, // Health
                                10,   // Resource Cost
                                25,  // Attack Damage
                                1.0f, // Attack Range
                                1.5f, // Attack Rate (Intentionally slower)
                                1.0f, // Movement Speed
								3.0f, // Radius
                                Team::BASKET);

	World::get_world()->troops.emplace_back(basket);
	basket_ = basket;

	glm::vec2 enemy_at = 0.5f * (World::get_world()->map.enemy_base_min() + World::get_world()->map.enemy_base_max());
	auto enemy_basket = std::make_shared<Brawler>(enemy_at, "EnemyBasket",
                                1000, // Health
                                10,   // Resource Cost
                                25,  // Attack Damage
                                1.0f, // Attack Range
                                1.5f, // Attack Rate
                                1.0f, // Movement Speed
								3.0f, // Radius
                                Team::ENEMY_BASKET);
	World::get_world()->troops.emplace_back(enemy_basket);
	enemy_basket_ = enemy_basket;

	load_troop_sprite();
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
	if (troop_vao_ != 0) glDeleteVertexArrays(1, &troop_vao_);
	if (troop_vbo_ != 0) glDeleteBuffers(1, &troop_vbo_);
	if (bar_vao_ != 0) glDeleteVertexArrays(1, &bar_vao_);
	if (bar_vbo_ != 0) glDeleteBuffers(1, &bar_vbo_);
	troop_vao_ = troop_vbo_ = 0;
	bar_vao_ = bar_vbo_ = 0;
}

static const float troop_size = 0.7f;

static int anim_frame(float time, int frames) {
	if (frames <= 0) return 0;
	int frame = (int)std::floor(time * 12.0f) % frames;
	if (frame < 0) frame += frames;
	return frame;
}

static char const *selected_troop_name(TroopType type) {
	return type == TroopType::BANANA_RANGED ? "BananaRanged" : "CherryBrawler";
}

static TroopType troop_type_from_name(std::string const &name) {
	if (name == "BananaRanged") return TroopType::BANANA_RANGED;
	return TroopType::CHERRY_BRAWLER;
}

static bool troop_overlaps(glm::vec2 pos, glm::vec2 mn, glm::vec2 mx) {
	return pos.x - troop_size <= mx.x && pos.x + troop_size >= mn.x && pos.y - troop_size <= mx.y && pos.y + troop_size >= mn.y;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size_) {
	window_size = window_size_;
	if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		mouse = glm::vec2(evt.motion.x, evt.motion.y);
		if (box_dragging_) mouse_to_world(box_end_);
		return true;
	}
	if (won_ || lost_) return false;
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt.button.button == SDL_BUTTON_LEFT) {
		mouse = glm::vec2(evt.button.x, evt.button.y);
		dragging_ = pointer_on_slot();
		box_dragging_ = !dragging_ && mouse_to_world(box_start_);
		box_end_ = box_start_;
		return true;
	}
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_UP && evt.button.button == SDL_BUTTON_LEFT) {
		mouse = glm::vec2(evt.button.x, evt.button.y);
		if (dragging_) {
			glm::vec3 hit;
			if (mouse_to_world(hit)) place_troop(glm::vec2(hit));
		}
		if (box_dragging_) {
			glm::vec2 mn = (glm::min(glm::vec2(box_start_), glm::vec2(box_end_)));
			glm::vec2 mx = (glm::max(glm::vec2(box_start_), glm::vec2(box_end_)));

			for (auto const &troop : World::get_world()->troops) {
				troop->set_selected(false);
				if (troop->get_team() != Team::FRUIT) continue;
				glm::vec2 pos = troop->get_position();
				bool selected = troop_overlaps(pos, mn, mx);
				troop->set_selected(selected);
			}
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
				if (troop->get_team() == Team::VEGETABLE) {
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
		}
		return true;
	}
	if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.key == SDLK_SPACE) {
		paused_ = !paused_;
		return true;
	}
	return false;
}

void PlayMode::update(float elapsed) {
	if (paused_ || won_ || lost_) return;

	time_ += elapsed;
	troop_spawn_timer -= elapsed;

	World::get_world()->update(elapsed);

	if (enemy_basket_.expired()) {
		won_ = true;
		dragging_ = false;
		box_dragging_ = false;
		return;
	}
	if (basket_.expired()) {
		lost_ = true;
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
	World::get_world()->map.draw_film(glm::mat4(1.0f), time_);
	if (paused_) draw_pause_overlay();
	if (dragging_ && World::get_world()->map.can_place(hover_col, hover_row)) {
		glm::u8vec4 ghost(255, 255, 255, 170);
		CharacterAnim const *anim = find_anim(selected_troop_name(selected_troop));
		if (anim != nullptr && anim->walk.ready()) {
		glm::vec2 ghost_half = anim->walk.half;
		glm::vec2 ghost_at = anim->sprite_center(glm::vec2(World::get_world()->map.cell_center(hover_col, hover_row)), false, false);
		GLuint ghost_tex = anim->walk.frames[0];
		glm::vec2 ghost_uv_min = anim->walk.uv_min;
		glm::vec2 ghost_uv_max = anim->walk.uv_max;
		if (bar_vao_ != 0) {
			std::vector< ColorVertex > shadow;
			add_foot_shadow(shadow, ghost_at, ghost_half, glm::u8vec3(18, 10, 6));
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

	if (selected_troop == TroopType::CHERRY_BRAWLER){
		if (resources_ < cherry_cost_) return;

		resources_ -= cherry_cost_;
		glm::vec2 pos(World::get_world()->map.cell_center(col, row));

		World::get_world()->troops.push_back(std::make_shared<Brawler>(
			pos, "CherryBrawler",
			100, 
			cherry_cost_, 
			25,
			1.0f, 
			1.0f, 
			1.0f,
			1.0f, // radius
			Team::FRUIT
		));
	} 
	else if (selected_troop == TroopType::BANANA_RANGED){
		if (resources_ < banana_cost_) return;

		resources_ -= banana_cost_;
		glm::vec2 pos(World::get_world()->map.cell_center(col, row));

		World::get_world()->troops.push_back(std::make_shared<Ranged>(
			pos, "BananaRanged",
			75, 
			banana_cost_, 
			15, // damage
			7.5f, // range
			0.75f, // attack rate
			1.25f, // speed
			1.0f, // radius
			Team::FRUIT
		));
	}

	
}

// Temp function for prototype
void PlayMode::spawn_vegetable_troop()
{
	int col = (rand() % 6) + 10; // place between cols 10-16
	int row = (rand() % 11); // place bewteen rows 0-10

	if (!World::get_world()->map.is_walkable(col, row)) return;
	
	glm::vec2 pos(World::get_world()->map.cell_center(col, row));
	World::get_world()->troops.push_back(std::make_shared<Brawler>(
		pos, "RadishBrawler",
		100, // Health
		2,   // Resource Cost
		25,  // Attack Damage
		1.0f, // Attack Range
		1.5f, // Attack Rate (Intentionally slower for testing purposes)
		1.0f, // Movement Spee
		1.0f, // Radius
		Team::VEGETABLE)
	);
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

CharacterAnim const *PlayMode::find_anim(std::string const &troop_name) const {
	for (auto const &anim : characters_) {
		if (anim.troop_name == troop_name) return &anim;
	}
	return nullptr;
}

void PlayMode::load_character(CharacterAnimDesc const &desc) {
	CharacterAnim anim;
	anim.troop_name = desc.troop_name;
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
		{"CherryBrawler", "BlueTeam/Cherry/Walk/CherryBrawler_Walk-", 11, "BlueTeam/Cherry/Attack/CherryBrawler_Attack-", 11, false, true, false},
		{"BananaRanged", "BlueTeam/Banana/Walk/BananaRanged_Walk-", 11, "BlueTeam/Banana/Attack/BananaRanged_Attack-", 16, false, true, true},
		{"RadishBrawler", "RedTeam/Cherry/Walk/CherryBrawler_Walk-", 11, "RedTeam/Cherry/Attack/CherryBrawler_Attack-", 11, false, false, false},
	};
	for (auto const &desc : roster) load_character(desc);
	load_sprite_png("Projectiles/banana_projectile.png", projectile_tex_, projectile_uv_min_, projectile_uv_max_, projectile_half_, 0.2f);

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

		for (auto const &troop : World::get_world()->troops) {
			if (!troop || troop->is_dead()) continue;
			CharacterAnim const *anim = find_anim(troop->get_name());
			if (anim == nullptr || !anim->walk.ready()) continue;
			bool attacking = anim->attacking_pose(troop->get_pose());
			AnimClip const &clip = anim->clip_for(attacking);
			glm::vec2 half = clip.half;
			glm::vec2 center = anim->sprite_center(troop->get_position(), attacking, troop->faces_left());

			glm::u8vec3 color;
			if (attack_targets.contains(troop.get()))
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
	auto draw_clip = [&](CharacterAnim const &anim, bool attacking) {
		AnimClip const &clip = anim.clip_for(attacking);
		if (!clip.ready()) return;
		for (int frame = 0; frame < clip.count(); ++frame) {
			if (clip.frames[(size_t)frame] == 0) continue;
			std::vector< SpriteVertex > verts;
			for (auto const &troop : World::get_world()->troops) {
				if (!troop || troop->is_dead() || troop->get_name() != anim.troop_name) continue;
				if (anim.attacking_pose(troop->get_pose()) != attacking) continue;
				float anim_time = attacking ? troop->get_attack_time() : troop->get_walk_time();
				if (attacking && troop->get_name() == "BananaRanged" && troop->get_attack_rate() > 1e-4f) {
					anim_time *= float(clip.count()) / (troop->get_attack_rate() * 12.0f);
				}
				if (anim_frame(anim_time, clip.count()) != frame) continue;
				bool face_left = troop->faces_left();
				glm::vec2 c = anim.sprite_center(troop->get_position(), attacking, face_left);
				glm::vec2 bl = c - clip.half;
				glm::vec2 tr = c + clip.half;
				float u0 = clip.uv_min.x;
				float u1 = clip.uv_max.x;
				if (anim.mirrored(face_left)) std::swap(u0, u1);
				SpriteVertex quad[6] = {
					{glm::vec3(bl.x, bl.y, 0.0f), white, glm::vec2(u0, clip.uv_min.y)},
					{glm::vec3(tr.x, bl.y, 0.0f), white, glm::vec2(u1, clip.uv_min.y)},
					{glm::vec3(tr.x, tr.y, 0.0f), white, glm::vec2(u1, clip.uv_max.y)},
					{glm::vec3(bl.x, bl.y, 0.0f), white, glm::vec2(u0, clip.uv_min.y)},
					{glm::vec3(tr.x, tr.y, 0.0f), white, glm::vec2(u1, clip.uv_max.y)},
					{glm::vec3(bl.x, tr.y, 0.0f), white, glm::vec2(u0, clip.uv_max.y)},
				};
				verts.insert(verts.end(), quad, quad + 6);
			}
			if (verts.empty()) continue;
			glBindTexture(GL_TEXTURE_2D, clip.frames[(size_t)frame]);
			glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(SpriteVertex), verts.data(), GL_DYNAMIC_DRAW);
			glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
		}
	};
	for (auto const &anim : characters_) {
		draw_clip(anim, false);
		draw_clip(anim, true);
	}
	if (projectile_tex_ != 0) {
		std::vector< SpriteVertex > verts;
		for (auto const &projectile : World::get_world()->projectiles) {
			if (!projectile || projectile->is_inactive()) continue;
			glm::vec2 c = projectile->get_position() + glm::vec2(0.0f, 0.45f);
			glm::vec2 bl = c - projectile_half_;
			glm::vec2 tr = c + projectile_half_;
			float u0 = projectile_uv_min_.x;
			float u1 = projectile_uv_max_.x;
			if (projectile->get_velocity().x < 0.0f) std::swap(u0, u1);
			SpriteVertex quad[6] = {
				{glm::vec3(bl.x, bl.y, 0.0f), white, glm::vec2(u0, projectile_uv_min_.y)},
				{glm::vec3(tr.x, bl.y, 0.0f), white, glm::vec2(u1, projectile_uv_min_.y)},
				{glm::vec3(tr.x, tr.y, 0.0f), white, glm::vec2(u1, projectile_uv_max_.y)},
				{glm::vec3(bl.x, bl.y, 0.0f), white, glm::vec2(u0, projectile_uv_min_.y)},
				{glm::vec3(tr.x, tr.y, 0.0f), white, glm::vec2(u1, projectile_uv_max_.y)},
				{glm::vec3(bl.x, tr.y, 0.0f), white, glm::vec2(u0, projectile_uv_max_.y)},
			};
			verts.insert(verts.end(), quad, quad + 6);
		}
		if (!verts.empty()) {
			glBindTexture(GL_TEXTURE_2D, projectile_tex_);
			glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(SpriteVertex), verts.data(), GL_DYNAMIC_DRAW);
			glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
		}
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
		for (auto const &troop : World::get_world()->troops) {
			if (!troop || troop->is_dead()) continue;
			Team team = troop->get_team();
			if (team != Team::FRUIT && team != Team::VEGETABLE && team != Team::BASKET && team != Team::ENEMY_BASKET) continue;
			int max_hp = troop->get_max_health();
			int hp = troop->get_health();
			if (max_hp <= 0 || hp >= max_hp) continue;
			float filled = std::clamp(float(hp) / float(max_hp), 0.0f, 1.0f);
			CharacterAnim const *anim = find_anim(troop->get_name());
			bool attacking = anim != nullptr && anim->attacking_pose(troop->get_pose());
			glm::vec2 half = glm::vec2(0.7f);
			glm::vec2 c = troop->get_position();
			if (anim != nullptr && anim->walk.ready()) {
				half = anim->clip_for(attacking).half;
				c = anim->sprite_center(c, attacking, troop->faces_left());
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

// Counts living fighters on each side; the baskets are bases, so they aren't included.
void PlayMode::draw_unit_stats() {
	if (window_size.x == 0 || window_size.y == 0) return;

	int fruits = 0;
	int vegetables = 0;
	for (auto const &troop : World::get_world()->troops) {
		if (!troop || troop->is_dead()) continue;
		if (troop->get_team() == Team::FRUIT) ++fruits;
		else if (troop->get_team() == Team::VEGETABLE) ++vegetables;
	}

	float px_w = 2.0f / float(window_size.x);
	float px_h = 2.0f / float(window_size.y);
	float line_px = float(window_size.y) * 0.04f;
	float margin_px = line_px * 0.5f;
	float scale = line_px / float(ui_font->pixel_height);
	glm::vec3 x_axis(scale * px_w, 0.0f, 0.0f);
	glm::vec3 y_axis(0.0f, scale * px_h, 0.0f);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	{
		TextRenderer renderer(glm::mat4(1.0f));
		auto draw_line = [&](std::string const &text, int index, glm::u8vec4 color) {
			glm::vec3 anchor(
				-1.0f + margin_px * px_w,
				1.0f - (margin_px + line_px * 1.25f * float(index) + line_px) * px_h,
				0.0f
			);
			renderer.draw_text(*ui_font, text, anchor + glm::vec3(1.5f * px_w, -1.5f * px_h, 0.0f), x_axis, y_axis, glm::u8vec4(27, 16, 18, 200));
			renderer.draw_text(*ui_font, text, anchor, x_axis, y_axis, color);
		};
		draw_line("FRUIT " + std::to_string(fruits), 0, glm::u8vec4(255, 236, 196, 255));
		draw_line("VEG " + std::to_string(vegetables), 1, glm::u8vec4(255, 170, 140, 255));
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
	struct ColorVertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
	};
	glm::u8vec4 wood(96, 62, 40, 255);
	glm::u8vec4 lip(214, 176, 122, 255);
	glm::u8vec4 slot(62, 38, 24, 255);
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
	float sy0 = ndc_y(place.slot_y + side);
	float sy1 = ndc_y(place.slot_y);
	auto slot_well = [&](float slot_x) {
		float sx0 = ndc_x(slot_x);
		float sx1 = ndc_x(slot_x + side);
		ColorVertex well[6] = {
			{glm::vec3(sx0, sy0, 0.0f), slot},
			{glm::vec3(sx1, sy0, 0.0f), slot},
			{glm::vec3(sx1, sy1, 0.0f), slot},
			{glm::vec3(sx0, sy0, 0.0f), slot},
			{glm::vec3(sx1, sy1, 0.0f), slot},
			{glm::vec3(sx0, sy1, 0.0f), slot},
		};
		return std::vector< ColorVertex >(well, well + 6);
	};
	std::vector< ColorVertex > wells;
	int slot_index = 0;
	for (auto const &anim : characters_) {
		if (!anim.show_in_bar) continue;
		std::vector< ColorVertex > well = slot_well(bar_slot_x(place, slot_index));
		wells.insert(wells.end(), well.begin(), well.end());
		++slot_index;
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

	float inset = side * 0.08f;
	float icon_px = side - inset * 2.0f;
	glm::vec2 square_half(icon_px / float(window_size.x), icon_px / float(window_size.y));
	// Fit a cropped sprite inside a square slot without stretching it.
	auto fit_slot = [&](glm::vec2 sprite_half, float slot_px) {
		float aspect = sprite_half.x / std::max(sprite_half.y, 1e-4f);
		float icon_w = slot_px;
		float icon_h = slot_px;
		if (aspect >= 1.0f) icon_h = slot_px / aspect;
		else icon_w = slot_px * aspect;
		return glm::vec2(icon_w / float(window_size.x), icon_h / float(window_size.y));
	};
	int icon_index = 0;
	for (auto const &anim : characters_) {
		if (!anim.show_in_bar || !anim.walk.ready()) continue;
		float x = bar_slot_x(place, icon_index);
		++icon_index;
		glm::vec2 icon_center(ndc_x(x + side * 0.5f), ndc_y(place.slot_y + side * 0.5f));
		glm::vec2 icon_half = anim.fit_bar_icon ? fit_slot(anim.walk.half, icon_px) : square_half;
		draw_sprite(glm::mat4(1.0f), icon_center, icon_half, glm::u8vec4(255, 255, 255, 255), anim.walk.frames[0], anim.walk.uv_min, anim.walk.uv_max);
	}
	

	auto ndc_w = [&](float px) { return px / float(window_size.x) * 2.0f; };
	auto ndc_h = [&](float px) { return px / float(window_size.y) * 2.0f; };
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

	std::string label = std::to_string(resources_);
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

	if (dragging_ && !World::get_world()->map.can_place(hover_col, hover_row)) {
		glm::vec2 cursor(ndc_x(mouse.x), ndc_y(mouse.y));
		CharacterAnim const *anim = find_anim(selected_troop_name(selected_troop));
		if (anim != nullptr && anim->walk.ready()) {
			glm::vec2 ghost_half = anim->fit_bar_icon ? fit_slot(anim->walk.half, side) : glm::vec2(side / float(window_size.x), side / float(window_size.y));
			draw_sprite(glm::mat4(1.0f), cursor, ghost_half, glm::u8vec4(255, 255, 255, 210), anim->walk.frames[0], anim->walk.uv_min, anim->walk.uv_max);
		}
	}
}
