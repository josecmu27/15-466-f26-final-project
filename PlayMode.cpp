#include "PlayMode.hpp"

#include "gl_errors.hpp"

#include <glm/gtc/matrix_transform.hpp>

PlayMode::PlayMode() {
}

PlayMode::~PlayMode() {
}

bool PlayMode::handle_event(SDL_Event const &, glm::uvec2 const &) {
	return false;
}

void PlayMode::update(float elapsed) {
	time_ += elapsed;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	glClearColor(0.20f, 0.11f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	float aspect = float(drawable_size.x) / float(drawable_size.y);
	glm::mat4 clip = clip_from_world(aspect);

	map.draw(clip, time_);
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
