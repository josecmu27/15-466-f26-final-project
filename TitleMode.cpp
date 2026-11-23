#include "TitleMode.hpp"

#include "PlayMode.hpp"
#include "ColorProgram.hpp"
#include "ColorTextureProgram.hpp"
#include "GameAudio.hpp"
#include "GameFonts.hpp"
#include "TextRenderer.hpp"
#include "data_path.hpp"
#include "gl_errors.hpp"
#include "load_save_png.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace {

constexpr float kTau = 6.28318530718f;

const glm::u8vec4 kInk(27, 16, 18, 255);
const glm::u8vec4 kPaperShade(214, 194, 150, 255);
const glm::u8vec4 kPaperLight(228, 212, 172, 255);
const glm::u8vec4 kCherry(196, 58, 52, 255);
const glm::u8vec4 kCherryLight(222, 92, 70, 255);
const glm::u8vec4 kCream(255, 236, 206, 255);

// Buttons sit low, under the two lineups (all the same size):
const glm::vec2 kStartCenter(0.0f, -0.52f);
const glm::vec2 kHowCenter(0.0f, -0.70f);
const glm::vec2 kQuitCenter(0.0f, -0.88f);
const glm::vec2 kButtonHalf(0.40f, 0.072f);

bool inside_button(glm::vec2 ui, glm::vec2 center) {
	glm::vec2 d = glm::abs(ui - center);
	return d.x <= kButtonHalf.x && d.y <= kButtonHalf.y;
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

} // namespace

TitleMode::Portrait TitleMode::load_portrait(std::string const &file) {
	glm::uvec2 size(0);
	std::vector< glm::u8vec4 > pixels;
	load_png(data_path(file), &size, &pixels, LowerLeftOrigin);
	if (size.x == 0 || size.y == 0 || pixels.empty()) {
		throw std::runtime_error("TitleMode: " + file + " is empty");
	}
	int min_x = (int)size.x, min_y = (int)size.y, max_x = -1, max_y = -1;
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
		throw std::runtime_error("TitleMode: " + file + " has no visible pixels");
	}
	TitleMode::Portrait portrait;
	portrait.uv_min = glm::vec2(float(min_x) / float(size.x), float(min_y) / float(size.y));
	portrait.uv_max = glm::vec2(float(max_x + 1) / float(size.x), float(max_y + 1) / float(size.y));
	portrait.aspect = float(max_x - min_x + 1) / float(max_y - min_y + 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGenTextures(1, &portrait.tex);
	glBindTexture(GL_TEXTURE_2D, portrait.tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, (GLsizei)size.x, (GLsizei)size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// These are drawn much larger than the source, so keep the full image and sample it directly.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
	return portrait;
}

namespace {

// Width of `text` in units of its height.
float text_width(Font const &font, std::string const &text) {
	float advance = 0.0f;
	font.layout(text, &advance);
	return advance / float(font.pixel_height);
}

// The font has a single regular weight; stamping it in a small disc makes it read as bold.
void draw_bold_text(TextRenderer &renderer, Font const &font, std::string const &text, glm::vec2 center, float height, float weight, glm::u8vec4 color) {
	glm::vec2 anchor = center - glm::vec2(0.5f * text_width(font, text) * height, kFontCapCenter * height);
	float scale = height / float(font.pixel_height);
	int rings = 3;
	for (int ring = 0; ring <= rings; ++ring) {
		float r = weight * float(ring) / float(rings);
		int count = (ring == 0) ? 1 : 6 * ring;
		for (int i = 0; i < count; ++i) {
			float a = float(i) / float(count) * kTau;
			glm::vec2 at = anchor + r * glm::vec2(std::cos(a), std::sin(a));
			renderer.draw_text(font, text, glm::vec3(at, 0.0f), glm::vec3(scale, 0.0f, 0.0f), glm::vec3(0.0f, scale, 0.0f), color);
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

	GameAudio::play_music(GameAudio::Music::Title);

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

	portraits_.push_back(load_portrait("title.png"));

	glGenBuffers(1, &sprite_vbo_);
	glGenVertexArrays(1, &sprite_vao_);
	glBindVertexArray(sprite_vao_);
	glBindBuffer(GL_ARRAY_BUFFER, sprite_vbo_);
	glVertexAttribPointer(color_texture_program->Position_vec4, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3) + sizeof(glm::u8vec4) + sizeof(glm::vec2), (void *)0);
	glEnableVertexAttribArray(color_texture_program->Position_vec4);
	glVertexAttribPointer(color_texture_program->Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(glm::vec3) + sizeof(glm::u8vec4) + sizeof(glm::vec2), (void *)sizeof(glm::vec3));
	glEnableVertexAttribArray(color_texture_program->Color_vec4);
	glVertexAttribPointer(color_texture_program->TexCoord_vec2, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec3) + sizeof(glm::u8vec4) + sizeof(glm::vec2), (void *)(sizeof(glm::vec3) + sizeof(glm::u8vec4)));
	glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	GL_ERRORS();
}

TitleMode::~TitleMode() {
	for (Portrait &portrait : portraits_) {
		if (portrait.tex != 0) glDeleteTextures(1, &portrait.tex);
	}
	if (sprite_vao_ != 0) glDeleteVertexArrays(1, &sprite_vao_);
	if (sprite_vbo_ != 0) glDeleteBuffers(1, &sprite_vbo_);
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
	if (inside_button(ui, kHowCenter)) return Button::How;
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
			if (released == Button::Start) {
				GameAudio::play(GameAudio::Sfx::UiConfirm);
				start_requested_ = true;
			} else if (released == Button::How) {
				GameAudio::play(GameAudio::Sfx::UiConfirm);
				how_requested_ = true;
			} else {
				GameAudio::play(GameAudio::Sfx::UiClick);
				quit();
			}
		}
		pressing_ = Button::None;
		return true;
	}
	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_RETURN || evt.key.key == SDLK_KP_ENTER || evt.key.key == SDLK_SPACE) {
			GameAudio::play(GameAudio::Sfx::UiConfirm);
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
	Button was_hovering = hover_;
	hover_ = button_at(mouse_ui());
	if (hover_ != was_hovering && hover_ != Button::None) GameAudio::play(GameAudio::Sfx::UiHover);

	if (how_requested_) {
		auto keep_alive = shared_from_this();
		Mode::set_current(std::make_shared< PlayMode >(true));
		return;
	}
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

	glClearColor(0.07f, 0.10f, 0.22f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	auto draw_color_mesh = [&](Mesh const &mesh, glm::mat4 const &clip) {
		if (mesh.empty()) return;
		glBindBuffer(GL_ARRAY_BUFFER, shapes_vbo_);
		glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(Vertex), mesh.data(), GL_STREAM_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUseProgram(color_program->program);
		glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip));
		glBindVertexArray(shapes_vao_);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)mesh.size());
		glBindVertexArray(0);
		glUseProgram(0);
	};

	// Cover the window with the poster. A wider window crops the top and bottom.
	if (sprite_vao_ != 0 && !portraits_.empty() && portraits_[0].tex != 0) {
		Portrait const &poster = portraits_[0];
		float image_aspect = std::max(poster.aspect, 0.01f);
		float hw = asp;
		float hh = 1.0f;
		if (asp > image_aspect) hh = asp / image_aspect;
		else hw = image_aspect;
		struct SpriteVertex {
			glm::vec3 Position;
			glm::u8vec4 Color;
			glm::vec2 TexCoord;
		};
		glm::u8vec4 white(255, 255, 255, 255);
		SpriteVertex quad[6] = {
			{glm::vec3(-hw, -hh, 0.0f), white, glm::vec2(poster.uv_min.x, poster.uv_min.y)},
			{glm::vec3( hw, -hh, 0.0f), white, glm::vec2(poster.uv_max.x, poster.uv_min.y)},
			{glm::vec3( hw,  hh, 0.0f), white, glm::vec2(poster.uv_max.x, poster.uv_max.y)},
			{glm::vec3(-hw, -hh, 0.0f), white, glm::vec2(poster.uv_min.x, poster.uv_min.y)},
			{glm::vec3( hw,  hh, 0.0f), white, glm::vec2(poster.uv_max.x, poster.uv_max.y)},
			{glm::vec3(-hw,  hh, 0.0f), white, glm::vec2(poster.uv_min.x, poster.uv_max.y)},
		};
		glBindVertexArray(sprite_vao_);
		glBindBuffer(GL_ARRAY_BUFFER, sprite_vbo_);
		glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
		glUseProgram(color_texture_program->program);
		glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_ui));
		glUniform1f(color_texture_program->SHARPEN_float, 0.35f);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, poster.tex);
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glUniform1f(color_texture_program->SHARPEN_float, 0.0f);
		glBindTexture(GL_TEXTURE_2D, 0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		glUseProgram(0);
	}

	// Buttons lift on hover and press down while held.
	auto button_lift = [&](Button button) {
		if (hover_ != button) return glm::vec2(0.0f);
		return pressing_ == button ? glm::vec2(0.0f, -0.012f) : glm::vec2(0.0f, 0.012f);
	};
	Mesh controls;
	{
		// A dark plate so the labels stay readable over the fighters' feet.
		paint_polygon(controls, round_rect(glm::vec2(-0.52f, -1.05f), glm::vec2(0.52f, -0.40f), 0.08f, 4), glm::u8vec4(16, 10, 12, 215));
		auto paint_button = [&](Button button, glm::vec2 center, glm::u8vec4 fill, glm::u8vec4 fill_hover) {
			glm::vec2 lift = button_lift(button);
			auto body = round_rect(center - kButtonHalf + lift, center + kButtonHalf + lift, 0.05f, 4);
			auto shadow = round_rect(center - kButtonHalf + glm::vec2(0.0f, -0.02f), center + kButtonHalf + glm::vec2(0.0f, -0.02f), 0.05f, 4);
			paint_polygon(controls, shadow, kInk);
			paint_polygon(controls, body, hover_ == button ? fill_hover : fill);
			paint_outline(controls, body, 0.016f, kInk, true);
		};
		paint_button(Button::Start, kStartCenter, kCherry, kCherryLight);
		paint_button(Button::How, kHowCenter, kPaperShade, kPaperLight);
		paint_button(Button::Quit, kQuitCenter, kPaperShade, kPaperLight);
	}
	draw_color_mesh(controls, clip_from_ui);

	{
		TextRenderer text(clip_from_ui);
		draw_bold_text(text, *ui_font, "START", kStartCenter + button_lift(Button::Start), 0.075f, 0.002f, kCream);
		draw_bold_text(text, *ui_font, "HOW TO PLAY", kHowCenter + button_lift(Button::How), 0.062f, 0.002f, kInk);
		draw_bold_text(text, *ui_font, "QUIT", kQuitCenter + button_lift(Button::Quit), 0.075f, 0.002f, kInk);
	}

	glDisable(GL_BLEND);
	GL_ERRORS();
}
