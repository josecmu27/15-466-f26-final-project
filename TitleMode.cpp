#include "TitleMode.hpp"

#include "PlayMode.hpp"
#include "ColorProgram.hpp"
#include "ColorTextureProgram.hpp"
#include "GameAudio.hpp"
#include "GameFonts.hpp"
#include "Settings.hpp"
#include "TextRenderer.hpp"
#include "data_path.hpp"
#include "gl_errors.hpp"
#include "load_save_png.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

using namespace UiDraw;

namespace {

// Buttons sit low, under the two lineups (all the same size):
const glm::vec2 kStartCenter(0.0f, -0.47f);
const glm::vec2 kHowCenter(0.0f, -0.62f);
const glm::vec2 kSettingsCenter(0.0f, -0.77f);
const glm::vec2 kQuitCenter(0.0f, -0.92f);
const glm::vec2 kButtonHalf(0.40f, 0.062f);

bool inside_button(glm::vec2 ui, glm::vec2 center, glm::vec2 half = kButtonHalf) {
	glm::vec2 d = glm::abs(ui - center);
	return d.x <= half.x && d.y <= half.y;
}

// Settings panel: one row per option, label on the left and its control on the right.
const float kRowY[5] = {0.34f, 0.17f, 0.0f, -0.17f, -0.34f};
const float kLabelLeft = -0.80f;
const float kMinusX = 0.22f;
const float kValueX = 0.46f;
const float kPlusX = 0.70f;
const glm::vec2 kSmallHalf(0.055f, 0.055f);
const glm::vec2 kToggleHalf(0.25f, 0.055f);
const glm::vec2 kBackCenter(0.0f, -0.60f);

struct ControlRect {
	TitleMode::Control id;
	glm::vec2 center;
	glm::vec2 half;
};

std::array< ControlRect, 8 > settings_controls() {
	using C = TitleMode::Control;
	return {{
		{C::MusicDown, glm::vec2(kMinusX, kRowY[0]), kSmallHalf},
		{C::MusicUp, glm::vec2(kPlusX, kRowY[0]), kSmallHalf},
		{C::SfxDown, glm::vec2(kMinusX, kRowY[1]), kSmallHalf},
		{C::SfxUp, glm::vec2(kPlusX, kRowY[1]), kSmallHalf},
		{C::Fullscreen, glm::vec2(kValueX, kRowY[2]), kToggleHalf},
		{C::Film, glm::vec2(kValueX, kRowY[3]), kToggleHalf},
		{C::Speed, glm::vec2(kValueX, kRowY[4]), kToggleHalf},
		{C::Back, kBackCenter, kButtonHalf},
	}};
}

bool control_is_on(TitleMode::Control control) {
	using C = TitleMode::Control;
	if (control == C::Fullscreen) return Settings::fullscreen;
	if (control == C::Film) return Settings::film_effect;
	if (control == C::Speed) return Settings::game_speed > 1.5f;
	return false;
}

std::string percent(float value) {
	return std::to_string(int(std::lround(value * 100.0f))) + "%";
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
	if (settings_open_) return Button::None;
	if (inside_button(ui, kStartCenter)) return Button::Start;
	if (inside_button(ui, kHowCenter)) return Button::How;
	if (inside_button(ui, kSettingsCenter)) return Button::Settings;
	if (inside_button(ui, kQuitCenter)) return Button::Quit;
	return Button::None;
}

TitleMode::Control TitleMode::control_at(glm::vec2 ui) const {
	if (!settings_open_) return Control::None;
	for (ControlRect const &rect : settings_controls()) {
		if (inside_button(ui, rect.center, rect.half)) return rect.id;
	}
	return Control::None;
}

void TitleMode::apply(Control control) {
	// Volumes move in 10% steps; rounding keeps repeated steps from drifting off the grid.
	auto step = [](float &value, float delta) {
		value = std::clamp(std::round((value + delta) * 10.0f) / 10.0f, 0.0f, 1.0f);
	};
	switch (control) {
		case Control::MusicDown: step(Settings::music_volume, -0.1f); GameAudio::refresh_music_volume(); break;
		case Control::MusicUp: step(Settings::music_volume, 0.1f); GameAudio::refresh_music_volume(); break;
		case Control::SfxDown: step(Settings::sfx_volume, -0.1f); break;
		case Control::SfxUp: step(Settings::sfx_volume, 0.1f); break;
		case Control::Fullscreen:
			Settings::fullscreen = !Settings::fullscreen;
			SDL_SetWindowFullscreen(Mode::window, Settings::fullscreen);
			break;
		case Control::Film: Settings::film_effect = !Settings::film_effect; break;
		case Control::Speed: Settings::game_speed = (Settings::game_speed > 1.5f ? 1.0f : 2.0f); break;
		case Control::Back:
			settings_open_ = false;
			control_hover_ = Control::None;
			break;
		case Control::None: return;
	}
	// Played after the change, so adjusting the effects volume previews the new level.
	GameAudio::play(GameAudio::Sfx::UiClick);
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
		control_pressing_ = control_at(mouse_ui());
		return true;
	}
	// Act on release so PlayMode never sees the click that started it.
	if (evt.type == SDL_EVENT_MOUSE_BUTTON_UP && evt.button.button == SDL_BUTTON_LEFT) {
		mouse_ = glm::vec2(evt.button.x, evt.button.y);
		if (settings_open_) {
			Control released = control_at(mouse_ui());
			if (released != Control::None && released == control_pressing_) apply(released);
			control_pressing_ = Control::None;
			pressing_ = Button::None;
			return true;
		}
		Button released = button_at(mouse_ui());
		if (released != Button::None && released == pressing_) {
			if (released == Button::Start) {
				GameAudio::play(GameAudio::Sfx::UiConfirm);
				start_requested_ = true;
			} else if (released == Button::How) {
				GameAudio::play(GameAudio::Sfx::UiConfirm);
				how_requested_ = true;
			} else if (released == Button::Settings) {
				GameAudio::play(GameAudio::Sfx::UiConfirm);
				settings_open_ = true;
				hover_ = Button::None;
			} else {
				GameAudio::play(GameAudio::Sfx::UiClick);
				quit();
			}
		}
		pressing_ = Button::None;
		return true;
	}
	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (settings_open_) {
			// Escape backs out of the panel instead of quitting; other keys mustn't start the game behind it.
			if (evt.key.key == SDLK_ESCAPE) apply(Control::Back);
			return true;
		}
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
	Control was_control = control_hover_;
	control_hover_ = control_at(mouse_ui());
	if (control_hover_ != was_control && control_hover_ != Control::None) GameAudio::play(GameAudio::Sfx::UiHover);

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
	if (!settings_open_) {
		Mesh controls;
		// A dark plate so the labels stay readable over the fighters' feet.
		paint_polygon(controls, round_rect(glm::vec2(-0.52f, -1.04f), glm::vec2(0.52f, -0.38f), 0.08f, 4), glm::u8vec4(16, 10, 12, 215));
		auto menu_button = [&](Button button, glm::vec2 center, glm::u8vec4 fill, glm::u8vec4 fill_hover) {
			paint_button(controls, center, kButtonHalf, button_lift(button), hover_ == button ? fill_hover : fill);
		};
		menu_button(Button::Start, kStartCenter, kCherry, kCherryLight);
		menu_button(Button::How, kHowCenter, kPaperShade, kPaperLight);
		menu_button(Button::Settings, kSettingsCenter, kPaperShade, kPaperLight);
		menu_button(Button::Quit, kQuitCenter, kPaperShade, kPaperLight);
		draw_color_mesh(controls, clip_from_ui);

		TextRenderer text(clip_from_ui);
		draw_bold_text(text, *ui_font, "START", kStartCenter + button_lift(Button::Start), 0.068f, 0.002f, kCream);
		draw_bold_text(text, *ui_font, "HOW TO PLAY", kHowCenter + button_lift(Button::How), 0.056f, 0.002f, kInk);
		draw_bold_text(text, *ui_font, "SETTINGS", kSettingsCenter + button_lift(Button::Settings), 0.056f, 0.002f, kInk);
		draw_bold_text(text, *ui_font, "QUIT", kQuitCenter + button_lift(Button::Quit), 0.068f, 0.002f, kInk);
	} else {
		auto control_lift = [&](Control control) {
			if (control_hover_ != control) return glm::vec2(0.0f);
			return control_pressing_ == control ? glm::vec2(0.0f, -0.01f) : glm::vec2(0.0f, 0.01f);
		};

		Mesh panel;
		// Dim the poster, then a paper card for the options.
		glm::u8vec4 dim(10, 6, 8, 170);
		tri(panel, glm::vec2(-asp, -1.0f), glm::vec2(asp, -1.0f), glm::vec2(asp, 1.0f), dim);
		tri(panel, glm::vec2(-asp, -1.0f), glm::vec2(asp, 1.0f), glm::vec2(-asp, 1.0f), dim);
		float card_hw = std::min(asp - 0.06f, 0.98f);
		glm::vec2 card_min(-card_hw, -0.80f), card_max(card_hw, 0.80f);
		paint_polygon(panel, round_rect(card_min + glm::vec2(0.02f, -0.02f), card_max + glm::vec2(0.02f, -0.02f), 0.07f, 4), kInk);
		auto card = round_rect(card_min, card_max, 0.07f, 4);
		paint_polygon(panel, card, kPaperLight);
		paint_outline(panel, card, 0.018f, kInk, true);

		for (ControlRect const &rect : settings_controls()) {
			bool on = control_is_on(rect.id);
			glm::vec2 lift = control_lift(rect.id);
			auto body = round_rect(rect.center - rect.half + lift, rect.center + rect.half + lift, 0.035f, 3);
			auto shadow = round_rect(rect.center - rect.half + glm::vec2(0.0f, -0.016f), rect.center + rect.half + glm::vec2(0.0f, -0.016f), 0.035f, 3);
			paint_polygon(panel, shadow, kInk);
			glm::u8vec4 fill = on ? kCherry : kPaperShade;
			glm::u8vec4 fill_hover = on ? kCherryLight : kCream;
			paint_polygon(panel, body, control_hover_ == rect.id ? fill_hover : fill);
			paint_outline(panel, body, 0.012f, kInk, true);
		}
		draw_color_mesh(panel, clip_from_ui);

		TextRenderer text(clip_from_ui);
		draw_bold_text(text, *ui_font, "SETTINGS", glm::vec2(0.0f, 0.60f), 0.10f, 0.003f, kInk);

		constexpr float LabelH = 0.055f;
		char const *labels[5] = {"MUSIC VOLUME", "SOUND EFFECTS", "FULLSCREEN", "FILM EFFECT", "GAME SPEED"};
		for (int row = 0; row < 5; ++row) {
			float center_x = kLabelLeft + 0.5f * text_width(*ui_font, labels[row]) * LabelH;
			draw_bold_text(text, *ui_font, labels[row], glm::vec2(center_x, kRowY[row]), LabelH, 0.0015f, kInk);
		}

		draw_bold_text(text, *ui_font, percent(Settings::music_volume), glm::vec2(kValueX, kRowY[0]), LabelH, 0.0015f, kInk);
		draw_bold_text(text, *ui_font, percent(Settings::sfx_volume), glm::vec2(kValueX, kRowY[1]), LabelH, 0.0015f, kInk);

		for (ControlRect const &rect : settings_controls()) {
			glm::vec2 at = rect.center + control_lift(rect.id);
			glm::u8vec4 ink = control_is_on(rect.id) ? kCream : kInk;
			switch (rect.id) {
				case Control::MusicDown: case Control::SfxDown: draw_bold_text(text, *ui_font, "-", at, 0.07f, 0.002f, ink); break;
				case Control::MusicUp: case Control::SfxUp: draw_bold_text(text, *ui_font, "+", at, 0.07f, 0.002f, ink); break;
				case Control::Fullscreen: draw_bold_text(text, *ui_font, Settings::fullscreen ? "ON" : "OFF", at, LabelH, 0.0015f, ink); break;
				case Control::Film: draw_bold_text(text, *ui_font, Settings::film_effect ? "ON" : "OFF", at, LabelH, 0.0015f, ink); break;
				case Control::Speed: draw_bold_text(text, *ui_font, Settings::game_speed > 1.5f ? "2x" : "1x", at, LabelH, 0.0015f, ink); break;
				case Control::Back: draw_bold_text(text, *ui_font, "BACK", at, 0.062f, 0.002f, ink); break;
				case Control::None: break;
			}
		}
	}

	glDisable(GL_BLEND);
	GL_ERRORS();
}
