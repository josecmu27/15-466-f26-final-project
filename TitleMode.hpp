#pragma once

/*
 * Start screen shown before PlayMode.
 *
 * Draws in "ui space": y runs from -1 (bottom) to 1 (top), x from -aspect to
 * aspect, so layout does not stretch when the window is resized.
 * Clicking START or pressing Enter/Space switches to PlayMode;
 * clicking QUIT or pressing Escape closes the game.
 */

#include "Mode.hpp"
#include "GL.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

struct TitleMode : Mode {
	TitleMode();
	virtual ~TitleMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	struct Vertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
	};

private:
	bool start_requested_ = false;

	glm::uvec2 window_size_ = glm::uvec2(1280, 720);
	glm::uvec2 drawable_size_ = glm::uvec2(1280, 720);
	glm::vec2 mouse_ = glm::vec2(0.0f);

	enum class Button { None, Start, Quit };
	Button hover_ = Button::None;
	Button pressing_ = Button::None; // button the left mouse went down on

	float aspect() const;
	glm::vec2 mouse_ui() const;
	Button button_at(glm::vec2 ui) const;
	void quit();

	GLuint shapes_vbo_ = 0;
	GLuint shapes_vao_ = 0;
};
