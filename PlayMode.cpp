#include "PlayMode.hpp"

#include "ColorProgram.hpp"
#include "ColorTextureProgram.hpp"
#include "DrawLines.hpp"
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

PlayMode::PlayMode() {
	int w = 0, h = 0;
	SDL_GetWindowSize(Mode::window, &w, &h);
	if (w > 0 && h > 0) window_size = glm::uvec2((unsigned)w, (unsigned)h);

	float mx = 0.0f, my = 0.0f;
	SDL_GetMouseState(&mx, &my);
	mouse = glm::vec2(mx, my);

	// TESTING Troop Placement
	// auto radish1 = std::make_shared<Brawler>(map.cell_center(6, 6), "RadishBrawler1", 
    //                             100, // Health
    //                             2,   // Resource Cost
    //                             25,  // Attack Damage
    //                             1.0f, // Attack Range
    //                             1.5f, // Attack Rate (Intentionally slower)
    //                             1.0f, // Movement Speed
    //                             Team::VEGETABLE);
	// auto radish2 = std::make_shared<Brawler>(map.cell_center(9, 0), "RadishBrawler2", 
    //                             100, // Health
    //                             2,   // Resource Cost
    //                             25,  // Attack Damage
    //                             1.0f, // Attack Range
    //                             1.5f, // Attack Rate (Intentionally slower)
    //                             1.0f, // Movement Speed
    //                             Team::VEGETABLE);

	// auto radish3 = std::make_shared<Brawler>(map.cell_center(10, 1), "RadishBrawler3", 
    //                             100, // Health
    //                             2,   // Resource Cost
    //                             25,  // Attack Damage
    //                             1.0f, // Attack Range
    //                             1.5f, // Attack Rate (Intentionally slower)
    //                             1.0f, // Movement Speed
    //                             Team::VEGETABLE);

	// auto radish4 = std::make_shared<Brawler>(map.cell_center(12, 4), "RadishBrawle4", 
    //                             100, // Health
    //                             2,   // Resource Cost
    //                             25,  // Attack Damage
    //                             1.0f, // Attack Range
    //                             1.5f, // Attack Rate (Intentionally slower)
    //                             1.0f, // Movement Speed
    //                             Team::VEGETABLE);

	// troops.emplace_back(radish1);
	// troops.emplace_back(radish2);
	// troops.emplace_back(radish3);
	// troops.emplace_back(radish4);

	load_troop_sprite();
}

PlayMode::~PlayMode() {
	if (troop_vao_ != 0) glDeleteVertexArrays(1, &troop_vao_);
	if (troop_vbo_ != 0) glDeleteBuffers(1, &troop_vbo_);
	if (troop_tex_ != 0) glDeleteTextures(1, &troop_tex_);
	if (bar_vao_ != 0) glDeleteVertexArrays(1, &bar_vao_);
	if (bar_vbo_ != 0) glDeleteBuffers(1, &bar_vbo_);
	troop_vao_ = troop_vbo_ = troop_tex_ = 0;
	bar_vao_ = bar_vbo_ = 0;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size_) {
	window_size = window_size_;
	if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		mouse = glm::vec2(evt.motion.x, evt.motion.y);
		return true;
	}
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt.button.button == SDL_BUTTON_LEFT) {
		mouse = glm::vec2(evt.button.x, evt.button.y);
		dragging_ = pointer_on_slot();
		return true;
	}
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_UP && evt.button.button == SDL_BUTTON_LEFT) {
		mouse = glm::vec2(evt.button.x, evt.button.y);
		if (dragging_) {
			glm::vec3 hit;
			if (mouse_to_world(hit)) place_troop(glm::vec2(hit));
		}
		dragging_ = false;
		return true;
	}
	return false;
}

void PlayMode::update(float elapsed) {
	time_ += elapsed;
	troop_spawn_timer -= elapsed;

	glm::vec2 left(map.bounds_min());
	glm::vec2 right(map.bounds_max());

	for (auto &troop : troops) {
		if (troop->is_dead()) continue;
		troop->update(elapsed, troops, paths);

		glm::vec2 p = glm::clamp(troop->get_position(), left, right);
		troop->set_position(p);
	}

	if (troop_spawn_timer < 0.0f){
		// spawn_vegetable_troop();
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
	map.draw(clip, time_, hover_col, hover_row, {});
	draw_troops(clip);
	map.draw_film(glm::mat4(1.0f), time_);
	if (dragging_ && map.is_walkable(hover_col, hover_row)) {
		glm::u8vec4 ghost(255, 255, 255, 170);
		draw_sprite(clip, glm::vec2(map.cell_center(hover_col, hover_row)), troop_half_, ghost);
	}

	glViewport(0, 0, (GLsizei)drawable_size.x, (GLsizei)drawable_size.y);
	draw_bar();

	GL_ERRORS();
}

glm::mat4 PlayMode::clip_from_world(float aspect) const {
	glm::vec3 mn = map.bounds_min();
	glm::vec3 mx = map.bounds_max();
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
	p.slot_y = map_h + pad;
	return p;
}

} // namespace

float PlayMode::map_aspect() const {
	float map_h = float(std::max(window_size.y, 1u)) * (1.0f - bar_frac_);
	return float(std::max(window_size.x, 1u)) / map_h;
}

bool PlayMode::pointer_on_slot() const {
	if (window_size.x == 0 || window_size.y == 0) return false;
	BarPlaces slot = bar_places(window_size, bar_frac_);
	return mouse.x >= slot.slot_x && mouse.x <= slot.slot_x + slot.side
		&& mouse.y >= slot.slot_y && mouse.y <= slot.slot_y + slot.side;
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
	glm::vec3 hit;
	if (!mouse_to_world(hit)) return;
	int col = 0;
	int row = 0;
	if (map.world_to_cell(hit, col, row)) {
		hover_col = col;
		hover_row = row;
	}
}

void PlayMode::place_troop(glm::vec2 world) {
	int col = 0;
	int row = 0;
	if (!map.world_to_cell(glm::vec3(world, 0.0f), col, row)) return;
	if (!map.is_walkable(col, row)) return;
	if (resources_ < cherry_cost_) return;
	resources_ -= cherry_cost_;
	glm::vec2 pos(map.cell_center(col, row));
	troops.push_back(std::make_shared<Brawler>(
		pos, "CherryBrawler",
		100, cherry_cost_, 
		25,
		1.0f, 
		1.0f, 
		1.0f,
		Team::FRUIT
	));
}

// Temp function for prototype
void PlayMode::spawn_vegetable_troop()
{
	int col = (rand() % 6) + 10; // place between cols 10-16
	int row = (rand() % 11); // place bewteen rows 0-10

	if (!map.is_walkable(col, row)) return;
	
	glm::vec2 pos(map.cell_center(col, row));
	troops.push_back(std::make_shared<Brawler>(
		pos, "RadishBrawler",
		100, // Health
		2,   // Resource Cost
		25,  // Attack Damage
		1.0f, // Attack Range
		1.5f, // Attack Rate (Intentionally slower for testing purposes)
		1.0f, // Movement Speed
		Team::VEGETABLE)
	);
}

void PlayMode::load_troop_sprite() {
	glm::uvec2 size(0);
	std::vector< glm::u8vec4 > pixels;
	load_png(data_path("cherry_brawler.png"), &size, &pixels, LowerLeftOrigin);
	if (size.x == 0 || size.y == 0 || pixels.empty()) {
		throw std::runtime_error("PlayMode: cherry_brawler.png is empty");
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
		throw std::runtime_error("PlayMode: cherry_brawler.png has no visible pixels");
	}

	troop_uv_min_ = glm::vec2(float(min_x) / float(size.x), float(min_y) / float(size.y));
	troop_uv_max_ = glm::vec2(float(max_x + 1) / float(size.x), float(max_y + 1) / float(size.y));
	float aspect = float(max_x - min_x + 1) / float(max_y - min_y + 1);
	float height = 1.7f;
	troop_half_ = glm::vec2(height * aspect, height) * 0.5f;

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGenTextures(1, &troop_tex_);
	glBindTexture(GL_TEXTURE_2D, troop_tex_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, (GLsizei)size.x, (GLsizei)size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

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
	if (troop_tex_ == 0 || troop_vao_ == 0) return;

	struct SpriteVertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};
	static_assert(sizeof(SpriteVertex) == sizeof(glm::vec3) + sizeof(glm::u8vec4) + sizeof(glm::vec2));

	std::vector< SpriteVertex > verts;
	glm::u8vec4 white(255, 255, 255, 255);
	for (auto const &troop : troops) {
		if (!troop || troop->is_dead()) continue;
		glm::vec2 c = troop->get_position();
		glm::vec2 bl = c - troop_half_;
		glm::vec2 tr = c + troop_half_;
		float u0 = troop_uv_min_.x;
		float u1 = troop_uv_max_.x;
		if (troop->get_team() == Team::VEGETABLE) std::swap(u0, u1);
		SpriteVertex quad[6] = {
			{glm::vec3(bl.x, bl.y, 0.0f), white, glm::vec2(u0, troop_uv_min_.y)},
			{glm::vec3(tr.x, bl.y, 0.0f), white, glm::vec2(u1, troop_uv_min_.y)},
			{glm::vec3(tr.x, tr.y, 0.0f), white, glm::vec2(u1, troop_uv_max_.y)},
			{glm::vec3(bl.x, bl.y, 0.0f), white, glm::vec2(u0, troop_uv_min_.y)},
			{glm::vec3(tr.x, tr.y, 0.0f), white, glm::vec2(u1, troop_uv_max_.y)},
			{glm::vec3(bl.x, tr.y, 0.0f), white, glm::vec2(u0, troop_uv_max_.y)},
		};
		verts.insert(verts.end(), quad, quad + 6);
	}
	if (verts.empty()) return;

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, troop_vbo_);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(SpriteVertex), verts.data(), GL_DYNAMIC_DRAW);
	glUseProgram(color_texture_program->program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, troop_tex_);
	glBindVertexArray(troop_vao_);
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_world));
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
	glDisable(GL_BLEND);
}

void PlayMode::draw_sprite(glm::mat4 const &clip, glm::vec2 center, glm::vec2 half, glm::u8vec4 color) {
	if (troop_tex_ == 0 || troop_vao_ == 0) return;
	struct SpriteVertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};
	glm::vec2 bl = center - half;
	glm::vec2 tr = center + half;
	SpriteVertex quad[6] = {
		{glm::vec3(bl.x, bl.y, 0.0f), color, glm::vec2(troop_uv_min_.x, troop_uv_min_.y)},
		{glm::vec3(tr.x, bl.y, 0.0f), color, glm::vec2(troop_uv_max_.x, troop_uv_min_.y)},
		{glm::vec3(tr.x, tr.y, 0.0f), color, glm::vec2(troop_uv_max_.x, troop_uv_max_.y)},
		{glm::vec3(bl.x, bl.y, 0.0f), color, glm::vec2(troop_uv_min_.x, troop_uv_min_.y)},
		{glm::vec3(tr.x, tr.y, 0.0f), color, glm::vec2(troop_uv_max_.x, troop_uv_max_.y)},
		{glm::vec3(bl.x, tr.y, 0.0f), color, glm::vec2(troop_uv_min_.x, troop_uv_max_.y)},
	};
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, troop_vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
	glUseProgram(color_texture_program->program);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, troop_tex_);
	glBindVertexArray(troop_vao_);
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip));
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
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
	float sx0 = ndc_x(place.slot_x);
	float sx1 = ndc_x(place.slot_x + side);
	float sy0 = ndc_y(place.slot_y + side);
	float sy1 = ndc_y(place.slot_y);
	ColorVertex well[6] = {
		{glm::vec3(sx0, sy0, 0.0f), slot},
		{glm::vec3(sx1, sy0, 0.0f), slot},
		{glm::vec3(sx1, sy1, 0.0f), slot},
		{glm::vec3(sx0, sy0, 0.0f), slot},
		{glm::vec3(sx1, sy1, 0.0f), slot},
		{glm::vec3(sx0, sy1, 0.0f), slot},
	};

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glBindBuffer(GL_ARRAY_BUFFER, bar_vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(panel), panel, GL_DYNAMIC_DRAW);
	glUseProgram(color_program->program);
	glBindVertexArray(bar_vao_);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
	glDrawArrays(GL_TRIANGLES, 0, 12);
	glBufferData(GL_ARRAY_BUFFER, sizeof(well), well, GL_DYNAMIC_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);

	float inset = side * 0.08f;
	glm::vec2 icon_center(ndc_x(place.slot_x + side * 0.5f), ndc_y(place.slot_y + side * 0.5f));
	glm::vec2 icon_half((side - inset * 2.0f) / float(window_size.x), (side - inset * 2.0f) / float(window_size.y));
	draw_sprite(glm::mat4(1.0f), icon_center, icon_half, glm::u8vec4(255, 255, 255, 255));

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
	glm::vec3 anchor(ndc_x(place.text_left), coin_cy - 0.5f * text_h, 0.0f);
	glm::u8vec4 ink(255, 236, 196, 255);
	float ox = ndc_w(1.2f);
	float oy = ndc_h(1.2f);
	glm::vec2 stamps[5] = {
		glm::vec2(0.0f, 0.0f),
		glm::vec2(ox, 0.0f), glm::vec2(-ox, 0.0f),
		glm::vec2(0.0f, oy), glm::vec2(0.0f, -oy),
	};
	{
		DrawLines lines(glm::mat4(1.0f));
		for (glm::vec2 stamp : stamps) {
			lines.draw_text(label, anchor + glm::vec3(stamp, 0.0f), glm::vec3(text_h, 0.0f, 0.0f), glm::vec3(0.0f, text_h, 0.0f), ink);
		}
	}

	if (dragging_ && !map.is_walkable(hover_col, hover_row)) {
		glm::vec2 cursor(ndc_x(mouse.x), ndc_y(mouse.y));
		glm::vec2 ghost_half(side / float(window_size.x), side / float(window_size.y));
		draw_sprite(glm::mat4(1.0f), cursor, ghost_half, glm::u8vec4(255, 255, 255, 210));
	}
}
