#include "TitleMode.hpp"

#include "PlayMode.hpp"
#include "ColorProgram.hpp"
#include "DrawLines.hpp"
#include "PathFont.hpp"
#include "gl_errors.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstddef>

namespace {

const std::string kTitle = "FRUIT VS VEGETABLE";

constexpr float kTau = 6.28318530718f;

const glm::u8vec4 kInk(27, 16, 18, 255);
const glm::u8vec4 kPaper(236, 222, 186, 255);
const glm::u8vec4 kPaperShade(214, 194, 150, 255);
const glm::u8vec4 kPaperLight(228, 212, 172, 255);
const glm::u8vec4 kGrass(104, 128, 58, 255);
const glm::u8vec4 kGrassDark(58, 78, 36, 255);
const glm::u8vec4 kCherry(196, 58, 52, 255);
const glm::u8vec4 kCherryLight(222, 92, 70, 255);

// Buttons, in ui space (both the same size):
const glm::vec2 kStartCenter(0.0f, -0.04f);
const glm::vec2 kQuitCenter(0.0f, -0.31f);
const glm::vec2 kButtonHalf(0.34f, 0.11f);

bool inside_button(glm::vec2 ui, glm::vec2 center) {
	glm::vec2 d = glm::abs(ui - center);
	return d.x <= kButtonHalf.x && d.y <= kButtonHalf.y;
}

float hash01(int a, int b, int c) {
	uint32_t n = (uint32_t)a * 1664525u ^ (uint32_t)b * 1013904223u ^ (uint32_t)c * 2246822519u;
	n ^= n >> 16;
	n *= 0x7feb352du;
	n ^= n >> 15;
	n *= 0x846ca68bu;
	n ^= n >> 16;
	return float(n & 0xFFFFFFu) / float(0xFFFFFFu);
}

using Mesh = std::vector< TitleMode::Vertex >;

void tri(Mesh &dst, glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::u8vec4 color) {
	dst.push_back(TitleMode::Vertex{glm::vec3(a, 0.0f), color});
	dst.push_back(TitleMode::Vertex{glm::vec3(b, 0.0f), color});
	dst.push_back(TitleMode::Vertex{glm::vec3(c, 0.0f), color});
}

void paint_polygon(Mesh &dst, std::vector< glm::vec2 > const &pts, glm::u8vec4 color) {
	if (pts.size() < 3) return;
	glm::vec2 c(0.0f);
	for (glm::vec2 p : pts) c += p;
	c /= float(pts.size());
	for (size_t i = 0; i < pts.size(); ++i) {
		tri(dst, c, pts[i], pts[(i + 1) % pts.size()], color);
	}
}

// Thick outline: one quad per segment plus a small fan at each joint so corners stay round.
void paint_outline(Mesh &dst, std::vector< glm::vec2 > const &pts, float width, glm::u8vec4 color, bool closed) {
	size_t n = pts.size();
	if (n < 2) return;
	float r = 0.5f * width;
	size_t segs = closed ? n : n - 1;
	for (size_t i = 0; i < segs; ++i) {
		glm::vec2 a = pts[i];
		glm::vec2 b = pts[(i + 1) % n];
		glm::vec2 d = b - a;
		float len = glm::length(d);
		if (len < 1e-6f) continue;
		glm::vec2 side = glm::vec2(-d.y, d.x) / len * r;
		tri(dst, a - side, b - side, b + side, color);
		tri(dst, a - side, b + side, a + side, color);
	}
	for (size_t i = 0; i < n; ++i) {
		for (int k = 0; k < 6; ++k) {
			float a0 = float(k) / 6.0f * kTau;
			float a1 = float(k + 1) / 6.0f * kTau;
			tri(dst, pts[i], pts[i] + r * glm::vec2(std::cos(a0), std::sin(a0)), pts[i] + r * glm::vec2(std::cos(a1), std::sin(a1)), color);
		}
	}
}

std::vector< glm::vec2 > round_rect(glm::vec2 mn, glm::vec2 mx, float rad, int segs) {
	rad = std::min(rad, 0.5f * std::min(mx.x - mn.x, mx.y - mn.y));
	glm::vec2 centers[4] = {
		glm::vec2(mx.x - rad, mn.y + rad),
		glm::vec2(mx.x - rad, mx.y - rad),
		glm::vec2(mn.x + rad, mx.y - rad),
		glm::vec2(mn.x + rad, mn.y + rad),
	};
	std::vector< glm::vec2 > pts;
	for (int c = 0; c < 4; ++c) {
		float a0 = -kTau / 4.0f + float(c) * kTau / 4.0f;
		for (int i = 0; i <= segs; ++i) {
			float a = a0 + float(i) / float(segs) * kTau / 4.0f;
			pts.push_back(centers[c] + rad * glm::vec2(std::cos(a), std::sin(a)));
		}
	}
	return pts;
}

// Old-film overlay in clip space, matching Map::draw_film in PlayMode:
// a dark vignette, flickering grain, and an occasional vertical scratch.
void paint_film(Mesh &fx, float time) {
	int frame = (int)std::floor(time * 8.0f);
	glm::u8vec4 clear(0, 0, 0, 0);
	glm::u8vec4 dark(12, 6, 4, 150);
	int segs = 28;
	for (int i = 0; i < segs; ++i) {
		float a0 = float(i) / float(segs) * kTau;
		float a1 = float(i + 1) / float(segs) * kTau;
		glm::vec2 i0 = 0.62f * glm::vec2(std::cos(a0), std::sin(a0));
		glm::vec2 i1 = 0.62f * glm::vec2(std::cos(a1), std::sin(a1));
		glm::vec2 o0 = 1.55f * glm::vec2(std::cos(a0), std::sin(a0));
		glm::vec2 o1 = 1.55f * glm::vec2(std::cos(a1), std::sin(a1));
		fx.push_back(TitleMode::Vertex{glm::vec3(i0, 0.0f), clear});
		fx.push_back(TitleMode::Vertex{glm::vec3(o0, 0.0f), dark});
		fx.push_back(TitleMode::Vertex{glm::vec3(o1, 0.0f), dark});
		fx.push_back(TitleMode::Vertex{glm::vec3(i0, 0.0f), clear});
		fx.push_back(TitleMode::Vertex{glm::vec3(o1, 0.0f), dark});
		fx.push_back(TitleMode::Vertex{glm::vec3(i1, 0.0f), clear});
	}
	for (int i = 0; i < 140; ++i) {
		glm::vec2 p(hash01(i, frame / 2, 1) * 2.0f - 1.0f, hash01(i, frame / 2, 2) * 2.0f - 1.0f);
		float s = 0.0025f + hash01(i, 3, 3) * 0.0035f;
		bool light = hash01(i, 4, 4) > 0.55f;
		glm::u8vec4 c = light ? glm::u8vec4(255, 244, 220, 50) : glm::u8vec4(20, 10, 8, 60);
		tri(fx, p, p + glm::vec2(s, 0.0f), p + glm::vec2(0.0f, s), c);
	}
	if (hash01(frame, 8, 8) > 0.35f) {
		float x = hash01(frame, 9, 1) * 1.6f - 0.8f;
		glm::u8vec4 scratch(255, 250, 240, 35);
		glm::vec2 a(x, -1.0f), b(x + 0.003f, -1.0f), c(x + 0.001f, 1.0f), d(x + 0.004f, 1.0f);
		tri(fx, a, b, c, scratch);
		tri(fx, b, d, c, scratch);
	}
}

float text_width(std::string const &text) {
	// Mirrors the glyph lookup in DrawLines::draw_text so text can be centered.
	float width = 0.0f;
	uint32_t start = 0;
	while (start < text.size()) {
		uint32_t end = start;
		uint32_t glyph = -1U;
		while (end < text.size()) {
			end += 1;
			auto f = PathFont::font.glyph_map.find(text.substr(start, end - start));
			if (f == PathFont::font.glyph_map.end()) {
				end -= 1;
				break;
			}
			glyph = f->second;
		}
		if (glyph == -1U) {
			end += 1;
			width += 0.6f;
		} else {
			width += PathFont::font.glyph_widths[glyph];
		}
		start = end;
	}
	return width;
}

// PathFont glyphs are single-pixel lines; stamping them in a small disc makes them read as bold.
void draw_bold_text(DrawLines &lines, std::string const &text, glm::vec2 center, float height, float weight, glm::u8vec4 color) {
	glm::vec2 anchor = center - glm::vec2(0.5f * text_width(text) * height, 0.5f * height);
	int rings = 3;
	for (int ring = 0; ring <= rings; ++ring) {
		float r = weight * float(ring) / float(rings);
		int count = (ring == 0) ? 1 : 6 * ring;
		for (int i = 0; i < count; ++i) {
			float a = float(i) / float(count) * kTau;
			glm::vec2 at = anchor + r * glm::vec2(std::cos(a), std::sin(a));
			lines.draw_text(text, glm::vec3(at, 0.0f), glm::vec3(height, 0.0f, 0.0f), glm::vec3(0.0f, height, 0.0f), color);
		}
	}
}

} // namespace

TitleMode::TitleMode() {
	int w = 0, h = 0;
	SDL_GetWindowSize(Mode::window, &w, &h);
	if (w > 0 && h > 0) window_size_ = glm::uvec2((unsigned)w, (unsigned)h);
	SDL_GetWindowSizeInPixels(Mode::window, &w, &h);
	if (w > 0 && h > 0) drawable_size_ = glm::uvec2((unsigned)w, (unsigned)h);

	float mx = 0.0f, my = 0.0f;
	SDL_GetMouseState(&mx, &my);
	mouse_ = glm::vec2(mx, my);

	glGenBuffers(1, &shapes_vbo_);
	glGenVertexArrays(1, &shapes_vao_);
	glBindVertexArray(shapes_vao_);
	glBindBuffer(GL_ARRAY_BUFFER, shapes_vbo_);
	glVertexAttribPointer(color_program->Position_vec4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Position));
	glEnableVertexAttribArray(color_program->Position_vec4);
	glVertexAttribPointer(color_program->Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Color));
	glEnableVertexAttribArray(color_program->Color_vec4);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	GL_ERRORS();
}

TitleMode::~TitleMode() {
	if (shapes_vao_ != 0) glDeleteVertexArrays(1, &shapes_vao_);
	if (shapes_vbo_ != 0) glDeleteBuffers(1, &shapes_vbo_);
}

float TitleMode::aspect() const {
	if (drawable_size_.y == 0) return 1.0f;
	return float(drawable_size_.x) / float(drawable_size_.y);
}

glm::vec2 TitleMode::mouse_ui() const {
	if (window_size_.x == 0 || window_size_.y == 0) return glm::vec2(1e6f);
	return glm::vec2(
		(mouse_.x / float(window_size_.x) * 2.0f - 1.0f) * aspect(),
		1.0f - mouse_.y / float(window_size_.y) * 2.0f
	);
}

TitleMode::Button TitleMode::button_at(glm::vec2 ui) const {
	if (inside_button(ui, kStartCenter)) return Button::Start;
	if (inside_button(ui, kQuitCenter)) return Button::Quit;
	return Button::None;
}

void TitleMode::quit() {
	// Let main's loop see a normal quit event so it tears down the same way as closing the window.
	SDL_Event evt{};
	evt.type = SDL_EVENT_QUIT;
	SDL_PushEvent(&evt);
}

bool TitleMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	window_size_ = window_size;
	if (evt.type == SDL_EVENT_MOUSE_MOTION) {
		mouse_ = glm::vec2(evt.motion.x, evt.motion.y);
		return true;
	}
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN && evt.button.button == SDL_BUTTON_LEFT) {
		mouse_ = glm::vec2(evt.button.x, evt.button.y);
		pressing_ = button_at(mouse_ui());
		return true;
	}
	// Act on release so PlayMode never sees the click that started it.
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_UP && evt.button.button == SDL_BUTTON_LEFT) {
		mouse_ = glm::vec2(evt.button.x, evt.button.y);
		Button released = button_at(mouse_ui());
		if (released != Button::None && released == pressing_) {
			if (released == Button::Start) start_requested_ = true;
			else quit();
		}
		pressing_ = Button::None;
		return true;
	}
	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_RETURN || evt.key.key == SDLK_KP_ENTER || evt.key.key == SDLK_SPACE) {
			start_requested_ = true;
			return true;
		}
		if (evt.key.key == SDLK_ESCAPE) {
			quit();
			return true;
		}
	}
	return false;
}

void TitleMode::update(float elapsed) {
	time_ += elapsed;
	hover_ = button_at(mouse_ui());

	if (start_requested_) {
		// Keep this mode alive until update() returns; set_current drops the last other reference.
		auto keep_alive = shared_from_this();
		Mode::set_current(std::make_shared< PlayMode >());
	}
}

void TitleMode::draw(glm::uvec2 const &drawable_size) {
	drawable_size_ = drawable_size;
	float asp = aspect();
	glm::mat4 clip_from_ui(
		1.0f / asp, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	glClearColor(0.20f, 0.11f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Paper card that fills most of the window, with a meadow along its bottom.
	float card_hw = std::min(asp - 0.08f, 1.7f);
	glm::vec2 card_min(-card_hw, -0.92f);
	glm::vec2 card_max(card_hw, 0.92f);
	float meadow_top = -0.46f;

	Mesh shapes;
	{
		auto card = round_rect(card_min, card_max, 0.08f, 4);
		paint_polygon(shapes, round_rect(card_min + glm::vec2(0.03f, -0.03f), card_max + glm::vec2(0.03f, -0.03f), 0.08f, 4), glm::u8vec4(10, 5, 4, 120));
		paint_polygon(shapes, card, kPaper);

		// Meadow with a wavy top edge.
		std::vector< glm::vec2 > hill;
		int steps = 40;
		for (int i = 0; i <= steps; ++i) {
			float x = glm::mix(card_min.x + 0.02f, card_max.x - 0.02f, float(i) / float(steps));
			hill.push_back(glm::vec2(x, meadow_top + 0.035f * std::sin(x * 3.1f) + 0.02f * std::sin(x * 7.3f + 1.0f)));
		}
		for (size_t i = 0; i + 1 < hill.size(); ++i) {
			// Split into vertical strips so the concave edge fills correctly.
			glm::vec2 a = hill[i], b = hill[i + 1];
			glm::vec2 a0(a.x, card_min.y + 0.02f), b0(b.x, card_min.y + 0.02f);
			tri(shapes, a0, b0, b, kGrass);
			tri(shapes, a0, b, a, kGrass);
		}
		for (int i = 0; i < 30; ++i) {
			float x = glm::mix(card_min.x + 0.1f, card_max.x - 0.1f, hash01(i, 5, 50));
			float y = glm::mix(card_min.y + 0.06f, meadow_top - 0.06f, hash01(i, 6, 51));
			glm::vec2 base(x, y);
			float lean = (hash01(i, 7, 52) - 0.5f) * 0.03f;
			paint_outline(shapes, {base, base + glm::vec2(lean, 0.05f)}, 0.012f, kGrassDark, false);
		}
		paint_outline(shapes, hill, 0.018f, kInk, false);
		paint_outline(shapes, card, 0.022f, kInk, true);
	}

	// Buttons lift on hover and press down while held.
	auto button_lift = [&](Button button) {
		if (hover_ != button) return glm::vec2(0.0f);
		return pressing_ == button ? glm::vec2(0.0f, -0.012f) : glm::vec2(0.0f, 0.012f);
	};
	auto paint_button = [&](Button button, glm::vec2 center, glm::u8vec4 fill, glm::u8vec4 fill_hover) {
		glm::vec2 lift = button_lift(button);
		auto body = round_rect(center - kButtonHalf + lift, center + kButtonHalf + lift, 0.06f, 4);
		auto shadow = round_rect(center - kButtonHalf + glm::vec2(0.0f, -0.025f), center + kButtonHalf + glm::vec2(0.0f, -0.025f), 0.06f, 4);
		paint_polygon(shapes, shadow, kInk);
		paint_polygon(shapes, body, hover_ == button ? fill_hover : fill);
		paint_outline(shapes, body, 0.02f, kInk, true);
	};
	paint_button(Button::Start, kStartCenter, kCherry, kCherryLight);
	paint_button(Button::Quit, kQuitCenter, kPaperShade, kPaperLight);

	glBindBuffer(GL_ARRAY_BUFFER, shapes_vbo_);
	glBufferData(GL_ARRAY_BUFFER, shapes.size() * sizeof(Vertex), shapes.data(), GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(color_program->program);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_ui));
	glBindVertexArray(shapes_vao_);
	glDrawArrays(GL_TRIANGLES, 0, (GLsizei)shapes.size());
	glBindVertexArray(0);
	glUseProgram(0);

	{ // Text goes last so it sits on top of everything.
		DrawLines lines(clip_from_ui);
		// Shrink the title on narrow windows so it stays inside the card.
		float title_h = std::min(0.24f, (2.0f * card_hw - 0.3f) / text_width(kTitle));
		draw_bold_text(lines, kTitle, glm::vec2(0.0f, 0.42f) + glm::vec2(0.012f, -0.012f), title_h, 0.012f, kInk);
		draw_bold_text(lines, kTitle, glm::vec2(0.0f, 0.42f), title_h, 0.010f, kCherry);

		draw_bold_text(lines, "START", kStartCenter + button_lift(Button::Start), 0.11f, 0.005f, kPaper);
		draw_bold_text(lines, "QUIT", kQuitCenter + button_lift(Button::Quit), 0.11f, 0.005f, kInk);
	}

	{ // Film overlay last, already in clip space, so it covers the whole screen including text.
		Mesh fx;
		paint_film(fx, time_);
		glBindBuffer(GL_ARRAY_BUFFER, shapes_vbo_);
		glBufferData(GL_ARRAY_BUFFER, fx.size() * sizeof(Vertex), fx.data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUseProgram(color_program->program);
		glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(glm::mat4(1.0f)));
		glBindVertexArray(shapes_vao_);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)fx.size());
		glBindVertexArray(0);
		glUseProgram(0);
	}

	glDisable(GL_BLEND);
	GL_ERRORS();
}
