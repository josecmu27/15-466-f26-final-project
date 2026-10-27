#include "PlayMode.hpp"

#include "gl_errors.hpp"

#include <glm/gtc/matrix_transform.hpp>

PlayMode::PlayMode() {
	int w = 0, h = 0;
	SDL_GetWindowSize(Mode::window, &w, &h);
	if (w > 0 && h > 0) window_size = glm::uvec2((unsigned)w, (unsigned)h);

	float mx = 0.0f, my = 0.0f;
	SDL_GetMouseState(&mx, &my);
	mouse = glm::vec2(mx, my);

	// TESTING Troop Placement
	// Create Troops
	auto cherry = std::make_shared<Brawler>(map.cell_center(0, 0), "CherryBrawler", 
                                100, // Health
                                2,   // Resource Cost
                                25,  // Attack Damage
                                1.0f, // Attack Range
                                1.0f, // Attack Rate
                                1.0f, // Movement Speed
                                Team::FRUIT);

	auto radish1 = std::make_shared<Brawler>(map.cell_center(4, 0), "RadishBrawler", 
                                100, // Health
                                2,   // Resource Cost
                                25,  // Attack Damage
                                1.0f, // Attack Range
                                1.5f, // Attack Rate (Intentionally slower)
                                1.0f, // Movement Speed
                                Team::VEGETABLE);
	auto radish2 = std::make_shared<Brawler>(map.cell_center(9, 0), "RadishBrawler", 
                                100, // Health
                                2,   // Resource Cost
                                25,  // Attack Damage
                                1.0f, // Attack Range
                                1.5f, // Attack Rate (Intentionally slower)
                                1.0f, // Movement Speed
                                Team::VEGETABLE);
	cherry->set_target(radish1);
	radish1->set_target(cherry);
	radish2->set_target(cherry);
	
	troops.emplace_back(cherry);
	troops.emplace_back(radish1);
	troops.emplace_back(radish2);


}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size_) {
	window_size = window_size_;
	if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		mouse = glm::vec2(evt.motion.x, evt.motion.y);
		return true;
	}
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt.button.button == SDL_BUTTON_LEFT) {
		mouse = glm::vec2(evt.button.x, evt.button.y);
		glm::vec3 hit;
		int col = 0, row = 0;
		if (mouse_to_world(hit) && map.world_to_cell(hit, col, row) && map.is_walkable(col, row)) {
			auto cherry = std::make_shared<Brawler>(map.cell_center(col, row), "CherryBrawler", 
				100, // Health
				2,   // Resource Cost
				25,  // Attack Damage
				1.0f, // Attack Range
				1.0f, // Attack Rate
				1.0f, // Movement Speed
				Team::FRUIT);
			troops.emplace_back(cherry);
		}
		return true;
	}
	return false;
}

void PlayMode::update(float elapsed) {
	time_ += elapsed;
	for (auto &troop : troops) {
		if (troop->is_dead()) continue;
		troop->update(elapsed);
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size_) {
	drawable_size = drawable_size_;
	glClearColor(0.20f, 0.11f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	float aspect = float(drawable_size.x) / float(drawable_size.y);
	glm::mat4 clip = clip_from_world(aspect);
	refresh_hover();

	std::vector<glm::vec2> troop_positions;
	for (auto const &troop : troops) {
		if (!troop->is_dead()) troop_positions.push_back(troop->get_position());
	}

	map.draw(clip, time_, hover_col, hover_row, troop_positions);
	map.draw_film(glm::mat4(1.0f), time_);

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

bool PlayMode::mouse_to_world(glm::vec3 &hit) const {
	if (window_size.x == 0 || window_size.y == 0 || drawable_size.y == 0) return false;
	float aspect = float(drawable_size.x) / float(drawable_size.y);
	glm::mat4 world_from_clip = glm::inverse(clip_from_world(aspect));
	float ndc_x = (mouse.x / float(window_size.x)) * 2.0f - 1.0f;
	float ndc_y = 1.0f - (mouse.y / float(window_size.y)) * 2.0f;
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
