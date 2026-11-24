#pragma once

// Hand-drawn UI pieces shared by the title screen and in-game screens, so buttons and
// bold labels look the same everywhere. Shapes are triangle lists for ColorProgram,
// usually built in "ui space" (y from -1 to 1, x scaled by the window's aspect).

#include "Font.hpp"
#include "TextRenderer.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace UiDraw {

struct Vertex {
	glm::vec3 Position;
	glm::u8vec4 Color;
};
using Mesh = std::vector< Vertex >;

inline const glm::u8vec4 kInk(27, 16, 18, 255);
inline const glm::u8vec4 kPaperShade(214, 194, 150, 255);
inline const glm::u8vec4 kPaperLight(228, 212, 172, 255);
inline const glm::u8vec4 kCherry(196, 58, 52, 255);
inline const glm::u8vec4 kCherryLight(222, 92, 70, 255);
inline const glm::u8vec4 kCream(255, 236, 206, 255);

void tri(Mesh &dst, glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::u8vec4 color);
void paint_polygon(Mesh &dst, std::vector< glm::vec2 > const &pts, glm::u8vec4 color);
// Thick outline: one quad per segment plus a small fan at each joint so corners stay round.
void paint_outline(Mesh &dst, std::vector< glm::vec2 > const &pts, float width, glm::u8vec4 color, bool closed);
std::vector< glm::vec2 > round_rect(glm::vec2 mn, glm::vec2 mx, float rad, int segs);

// The title screen's button: an ink drop shadow, the rounded body, and an ink outline.
// `lift` moves the body (up on hover, down while pressed); the shadow stays put.
void paint_button(Mesh &dst, glm::vec2 center, glm::vec2 half, glm::vec2 lift, glm::u8vec4 fill);

// Width of `text` in units of its height.
float text_width(Font const &font, std::string const &text);
// The font has a single regular weight; stamping it in a small disc makes it read as bold.
void draw_bold_text(TextRenderer &renderer, Font const &font, std::string const &text, glm::vec2 center, float height, float weight, glm::u8vec4 color);

}
