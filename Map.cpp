#include "Map.hpp"

#include "ColorProgram.hpp"
#include "ColorTextureProgram.hpp"
#include "gl_errors.hpp"
#include "load_save_png.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <cstddef>

namespace {

constexpr float kTau = 6.28318530718f;
const glm::u8vec4 kInk(27, 16, 18, 255);
const glm::u8vec4 kGrass(104, 128, 58, 255);
const glm::u8vec4 kGrassDark(58, 78, 36, 255);
const glm::u8vec4 kGrassLight(116, 140, 66, 255);
const glm::u8vec4 kGrassShade(92, 116, 50, 255);

float hash01(int a, int b, int c) {
	uint32_t n = (uint32_t)a * 1664525u ^ (uint32_t)b * 1013904223u ^ (uint32_t)c * 2246822519u;
	n ^= n >> 16;
	n *= 0x7feb352du;
	n ^= n >> 15;
	n *= 0x846ca68bu;
	n ^= n >> 16;
	return float(n & 0xFFFFFFu) / float(0xFFFFFFu);
}

using Mesh = std::vector< Map::Vertex >;

void vert(Mesh &dst, glm::vec2 p, glm::u8vec4 color) {
	dst.push_back(Map::Vertex{glm::vec3(p, 0.0f), color});
}

void tri(Mesh &dst, glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::u8vec4 color) {
	vert(dst, a, color);
	vert(dst, b, color);
	vert(dst, c, color);
}

void paint_ellipse(Mesh &dst, glm::vec2 c, float rx, float ry, float rotation, glm::u8vec4 color, int segs, int seed, float jitter) {
	if (rx <= 0.0f || ry <= 0.0f || segs < 3) return;
	float cs = std::cos(rotation);
	float sn = std::sin(rotation);
	auto ring = [&](int i) {
		float a = float(i) / float(segs) * kTau;
		float j = 1.0f;
		if (jitter > 0.0f) j += (hash01(i, seed, 3) - 0.5f) * 2.0f * jitter;
		glm::vec2 d(std::cos(a) * rx * j, std::sin(a) * ry * j);
		return c + glm::vec2(cs * d.x - sn * d.y, sn * d.x + cs * d.y);
	};
	for (int i = 0; i < segs; ++i) {
		tri(dst, c, ring(i), ring(i + 1), color);
	}
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

std::vector< glm::vec2 > shape_round_rect(glm::vec2 mn, glm::vec2 mx, float rad, int segs, int seed, float deckle) {
	rad = std::min(rad, 0.5f * std::min(mx.x - mn.x, mx.y - mn.y));
	std::vector< glm::vec2 > pts;
	struct Corner { glm::vec2 c; float a0; };
	Corner corners[4] = {
		{glm::vec2(mx.x - rad, mn.y + rad), -kTau / 4.0f},
		{glm::vec2(mx.x - rad, mx.y - rad), 0.0f},
		{glm::vec2(mn.x + rad, mx.y - rad), kTau / 4.0f},
		{glm::vec2(mn.x + rad, mn.y + rad), kTau / 2.0f},
	};
	glm::vec2 mid = (mn + mx) * 0.5f;
	for (int k = 0; k < 4; ++k) {
		for (int i = 0; i <= segs; ++i) {
			float a = corners[k].a0 + (kTau / 4.0f) * (float(i) / float(segs));
			glm::vec2 p = corners[k].c + glm::vec2(std::cos(a), std::sin(a)) * rad;
			if (deckle > 0.0f) {
				glm::vec2 n = p - mid;
				float L = glm::length(n);
				if (L > 1e-4f) p += (n / L) * (hash01(k * 16 + i, seed, 9) - 0.5f) * 2.0f * deckle;
			}
			pts.push_back(p);
		}
	}
	return pts;
}

glm::vec2 tangent_at(std::vector< glm::vec2 > const &pts, size_t i, bool closed) {
	int n = (int)pts.size();
	int i0 = (int)i - 1;
	int i1 = (int)i + 1;
	if (closed) {
		i0 = (i0 % n + n) % n;
		i1 = (i1 % n + n) % n;
	} else {
		i0 = std::clamp(i0, 0, n - 1);
		i1 = std::clamp(i1, 0, n - 1);
	}
	glm::vec2 t = pts[(size_t)i1] - pts[(size_t)i0];
	float L = glm::length(t);
	if (L < 1e-5f) return glm::vec2(1.0f, 0.0f);
	return t / L;
}

void paint_ribbon(Mesh &dst, std::vector< glm::vec2 > const &pts, float width, glm::u8vec4 color, bool closed) {
	if (pts.size() < 2 || width <= 0.0f) return;
	float hw = width * 0.5f;
	size_t n = pts.size();
	std::vector< glm::vec2 > left(n), right(n);
	for (size_t i = 0; i < n; ++i) {
		glm::vec2 t = tangent_at(pts, i, closed);
		glm::vec2 normal(-t.y, t.x);
		left[i] = pts[i] + normal * hw;
		right[i] = pts[i] - normal * hw;
	}
	size_t segs = closed ? n : n - 1;
	for (size_t i = 0; i < segs; ++i) {
		size_t j = (i + 1) % n;
		tri(dst, left[i], left[j], right[j], color);
		tri(dst, left[i], right[j], right[i], color);
	}
	if (!closed) {
		paint_ellipse(dst, pts.front(), hw, hw, 0.0f, color, 8, 1, 0.0f);
		paint_ellipse(dst, pts.back(), hw, hw, 0.0f, color, 8, 2, 0.0f);
	}
}

std::vector< glm::vec2 > boil_points(std::vector< glm::vec2 > pts, float amount, int frame, int salt) {
	if (amount <= 0.0f) return pts;
	for (size_t i = 0; i < pts.size(); ++i) {
		float ang = hash01((int)i, frame, salt) * kTau;
		float mag = hash01((int)i, frame, salt + 19) * amount;
		pts[i] += glm::vec2(std::cos(ang), std::sin(ang)) * mag;
	}
	return pts;
}

} // namespace

Map::Map() : Map(
	"................\n"
	"................\n"
	"................\n"
	"................\n"
	"................\n"
	"................\n"
	"................\n"
	"................\n"
	"................\n"
	"................\n"
) {
}

Map::Map(std::string const &ascii) {
	build_from_ascii(ascii);
	load_base_sprite();
	rebuild();
}

Map::~Map() {
	if (backdrop_vao_ != 0) glDeleteVertexArrays(1, &backdrop_vao_);
	if (backdrop_vbo_ != 0) glDeleteBuffers(1, &backdrop_vbo_);
	if (frame_vao_ != 0) glDeleteVertexArrays(1, &frame_vao_);
	if (frame_vbo_ != 0) glDeleteBuffers(1, &frame_vbo_);
	if (base_vao_ != 0) glDeleteVertexArrays(1, &base_vao_);
	if (base_vbo_ != 0) glDeleteBuffers(1, &base_vbo_);
	if (base_tex_ != 0) glDeleteTextures(1, &base_tex_);
	backdrop_vao_ = backdrop_vbo_ = frame_vao_ = frame_vbo_ = 0;
	base_vao_ = base_vbo_ = base_tex_ = 0;
}

bool Map::in_bounds(int col, int row) const {
	return col >= 0 && row >= 0 && col < cols_ && row < rows_;
}

glm::vec3 Map::cell_center(int col, int row) const {
	if (!in_bounds(col, row)) throw std::runtime_error("Map::cell_center: cell out of bounds");
	float origin_x = -0.5f * float(cols_) * tile_size_;
	float origin_y = 0.5f * float(rows_) * tile_size_;
	return glm::vec3(
		origin_x + (float(col) + 0.5f) * tile_size_,
		origin_y - (float(row) + 0.5f) * tile_size_,
		0.0f
	);
}

bool Map::world_to_cell(glm::vec3 const &world, int &col, int &row) const {
	float origin_x = -0.5f * float(cols_) * tile_size_;
	float origin_y = 0.5f * float(rows_) * tile_size_;
	col = (int)std::floor((world.x - origin_x) / tile_size_);
	row = (int)std::floor((origin_y - world.y) / tile_size_);
	return in_bounds(col, row);
}

bool Map::is_walkable(int col, int row) const {
	return in_bounds(col, row) && tiles_[index(col, row)] == Tile::Grass;
}

glm::vec3 Map::bounds_min() const {
	return glm::vec3(-0.5f * float(cols_) * tile_size_, -0.5f * float(rows_) * tile_size_, 0.0f);
}

glm::vec3 Map::bounds_max() const {
	return glm::vec3(0.5f * float(cols_) * tile_size_, 0.5f * float(rows_) * tile_size_, 0.0f);
}

void Map::draw(glm::mat4 const &clip_from_world, float time, int hover_col, int hover_row, std::vector<glm::vec2> const &troop_positions) const {
	int frame = (int)std::floor(time * 12.0f);
	frame_.clear();

	for (Stroke const &stroke : strokes_) {
		std::vector< glm::vec2 > pts = boil_points(stroke.pts, stroke.boil, frame, stroke.salt);
		paint_ribbon(frame_, pts, stroke.width, stroke.color, stroke.closed);
	}

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	draw_uploaded(backdrop_vao_, backdrop_count_, clip_from_world);
	upload(frame_vbo_, frame_vao_, frame_);
	draw_uploaded(frame_vao_, (GLuint)frame_.size(), clip_from_world);

	if (base_tex_ != 0 && base_vao_ != 0) {
		glUseProgram(color_texture_program->program);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, base_tex_);
		glBindVertexArray(base_vao_);
		glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_world));
		glDrawArrays(GL_TRIANGLES, 0, 6);
		glBindVertexArray(0);
		glBindTexture(GL_TEXTURE_2D, 0);
		glUseProgram(0);
	}

	if (in_bounds(hover_col, hover_row)) {
		glm::vec2 c(cell_center(hover_col, hover_row));
		float h = tile_size_ * 0.46f;
		bool base = tiles_[index(hover_col, hover_row)] == Tile::Base;
		glm::u8vec4 wash = base ? glm::u8vec4(176, 48, 40, 90) : glm::u8vec4(70, 140, 60, 90);
		glm::u8vec4 line = base ? glm::u8vec4(120, 24, 22, 255) : glm::u8vec4(28, 70, 24, 255);
		auto box = shape_round_rect(c - glm::vec2(h), c + glm::vec2(h), 0.12f, 3, 1, 0.0f);
		Mesh mark;
		paint_polygon(mark, box, wash);
		std::vector< glm::vec2 > edge = boil_points(box, 0.02f, frame, 90);
		paint_ribbon(mark, edge, 0.07f, line, true);
		upload(frame_vbo_, frame_vao_, mark);
		draw_uploaded(frame_vao_, (GLuint)mark.size(), clip_from_world);
	}

	if (!troop_positions.empty()) {
		Mesh tokens;
		glm::u8vec4 ink(27, 16, 18, 255);
		glm::u8vec4 body(196, 84, 58, 255);
		float radius = tile_size_ * 0.28f;
		for (glm::vec2 pos : troop_positions) {
			paint_ellipse(tokens, pos, radius + 0.06f, radius + 0.06f, 0.0f, ink, 18, 0, 0.0f);
			paint_ellipse(tokens, pos, radius, radius, 0.0f, body, 18, 0, 0.0f);
		}
		upload(frame_vbo_, frame_vao_, tokens);
		draw_uploaded(frame_vao_, (GLuint)tokens.size(), clip_from_world);
	}

	glDisable(GL_BLEND);
	glBindVertexArray(0);
	glUseProgram(0);
	GL_ERRORS();
}

void Map::draw_film(glm::mat4 const &clip_from_clip, float time) const {
	int frame = (int)std::floor(time * 8.0f);
	Mesh fx;
	int segs = 28;
	for (int i = 0; i < segs; ++i) {
		float a0 = float(i) / float(segs) * kTau;
		float a1 = float(i + 1) / float(segs) * kTau;
		glm::vec2 i0(std::cos(a0) * 0.62f, std::sin(a0) * 0.62f);
		glm::vec2 i1(std::cos(a1) * 0.62f, std::sin(a1) * 0.62f);
		glm::vec2 o0(std::cos(a0) * 1.55f, std::sin(a0) * 1.55f);
		glm::vec2 o1(std::cos(a1) * 1.55f, std::sin(a1) * 1.55f);
		glm::u8vec4 clear(0, 0, 0, 0);
		glm::u8vec4 dark(12, 6, 4, 150);
		fx.push_back(Vertex{glm::vec3(i0, 0.0f), clear});
		fx.push_back(Vertex{glm::vec3(o0, 0.0f), dark});
		fx.push_back(Vertex{glm::vec3(o1, 0.0f), dark});
		fx.push_back(Vertex{glm::vec3(i0, 0.0f), clear});
		fx.push_back(Vertex{glm::vec3(o1, 0.0f), dark});
		fx.push_back(Vertex{glm::vec3(i1, 0.0f), clear});
	}
	for (int i = 0; i < 140; ++i) {
		float x = hash01(i, frame / 2, 1) * 2.0f - 1.0f;
		float y = hash01(i, frame / 2, 2) * 2.0f - 1.0f;
		float s = 0.0025f + hash01(i, 3, 3) * 0.0035f;
		bool light = hash01(i, 4, 4) > 0.55f;
		glm::u8vec4 c = light ? glm::u8vec4(255, 244, 220, 50) : glm::u8vec4(20, 10, 8, 60);
		glm::vec2 p(x, y);
		tri(fx, p, p + glm::vec2(s, 0.0f), p + glm::vec2(0.0f, s), c);
	}
	if (hash01(frame, 8, 8) > 0.35f) {
		float x = hash01(frame, 9, 1) * 1.6f - 0.8f;
		glm::u8vec4 scratch(255, 250, 240, 35);
		glm::vec2 a(x, -1.0f);
		glm::vec2 b(x + 0.003f, -1.0f);
		glm::vec2 c(x + 0.001f, 1.0f);
		glm::vec2 d(x + 0.004f, 1.0f);
		tri(fx, a, b, c, scratch);
		tri(fx, b, d, c, scratch);
	}

	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	upload(frame_vbo_, frame_vao_, fx);
	draw_uploaded(frame_vao_, (GLuint)fx.size(), clip_from_clip);
	glDisable(GL_BLEND);
	glBindVertexArray(0);
	glUseProgram(0);
}

void Map::build_from_ascii(std::string const &ascii) {
	std::vector< std::string > rows;
	std::string line;
	auto push_line = [&]() {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (!line.empty()) rows.push_back(line);
		line.clear();
	};
	for (char ch : ascii) {
		if (ch == '\n') push_line();
		else line.push_back(ch);
	}
	push_line();

	if (rows.empty()) throw std::runtime_error("Map: level is empty");
	cols_ = (int)rows[0].size();
	rows_ = (int)rows.size();
	if (cols_ <= 0) throw std::runtime_error("Map: level has no columns");

	tiles_.assign(size_t(cols_ * rows_), Tile::Grass);

	for (int row = 0; row < rows_; ++row) {
		if ((int)rows[row].size() != cols_) {
			throw std::runtime_error("Map: level rows must all be the same width");
		}
		for (int col = 0; col < cols_; ++col) {
			if (rows[row][col] != '.') {
				throw std::runtime_error(std::string("Map: unknown tile '") + rows[row][col] + "'");
			}
		}
	}
}

void Map::load_base_sprite() {
	glm::uvec2 size(0);
	std::vector< glm::u8vec4 > pixels;
	load_png(data_path("basket.png"), &size, &pixels, LowerLeftOrigin);
	if (size.x == 0 || size.y == 0 || pixels.empty()) {
		throw std::runtime_error("Map: basket.png is empty");
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
		throw std::runtime_error("Map: basket.png has no visible pixels");
	}

	base_uv_min_ = glm::vec2(float(min_x) / float(size.x), float(min_y) / float(size.y));
	base_uv_max_ = glm::vec2(float(max_x + 1) / float(size.x), float(max_y + 1) / float(size.y));

	// Leftmost 2 columns, vertically centered across 2 rows. Ten rows have no
	// single middle row, so the block starts at (rows - 2) / 2.
	constexpr int kBaseCols = 2;
	constexpr int kBaseRows = 2;
	if (cols_ < kBaseCols || rows_ < kBaseRows) {
		throw std::runtime_error("Map: board is too small for a 2x2 base");
	}
	int base_col0 = 0;
	int base_row0 = (rows_ - kBaseRows) / 2;

	glm::vec2 half_tile(tile_size_ * 0.5f);
	glm::vec2 block_min(cell_center(base_col0, base_row0 + kBaseRows - 1));
	glm::vec2 block_max(cell_center(base_col0 + kBaseCols - 1, base_row0));
	block_min -= half_tile;
	block_max += half_tile;
	base_anchor_ = 0.5f * (block_min + block_max);

	float aspect = float(max_x - min_x + 1) / float(max_y - min_y + 1);
	float block_w = block_max.x - block_min.x;
	float block_h = block_max.y - block_min.y;
	float height = block_h;
	float width = height * aspect;
	if (width > block_w) {
		width = block_w;
		height = width / aspect;
	}
	base_half_ = glm::vec2(width, height) * 0.5f;

	for (int row = base_row0; row < base_row0 + kBaseRows; ++row) {
		for (int col = base_col0; col < base_col0 + kBaseCols; ++col) {
			if (tiles_[index(col, row)] == Tile::Grass) {
				tiles_[index(col, row)] = Tile::Base;
			}
		}
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGenTextures(1, &base_tex_);
	glBindTexture(GL_TEXTURE_2D, base_tex_);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, (GLsizei)size.x, (GLsizei)size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, 0);

	struct SpriteVertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};
	glm::u8vec4 white(255, 255, 255, 255);
	glm::vec2 bl = base_anchor_ - base_half_;
	glm::vec2 tr = base_anchor_ + base_half_;
	SpriteVertex verts[6] = {
		{glm::vec3(bl.x, bl.y, 0.0f), white, glm::vec2(base_uv_min_.x, base_uv_min_.y)},
		{glm::vec3(tr.x, bl.y, 0.0f), white, glm::vec2(base_uv_max_.x, base_uv_min_.y)},
		{glm::vec3(tr.x, tr.y, 0.0f), white, glm::vec2(base_uv_max_.x, base_uv_max_.y)},
		{glm::vec3(bl.x, bl.y, 0.0f), white, glm::vec2(base_uv_min_.x, base_uv_min_.y)},
		{glm::vec3(tr.x, tr.y, 0.0f), white, glm::vec2(base_uv_max_.x, base_uv_max_.y)},
		{glm::vec3(bl.x, tr.y, 0.0f), white, glm::vec2(base_uv_min_.x, base_uv_max_.y)},
	};

	glGenBuffers(1, &base_vbo_);
	glBindBuffer(GL_ARRAY_BUFFER, base_vbo_);
	glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

	glGenVertexArrays(1, &base_vao_);
	glBindVertexArray(base_vao_);
	glBindBuffer(GL_ARRAY_BUFFER, base_vbo_);
	glVertexAttribPointer(color_texture_program->Position_vec4, 3, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (GLbyte *)0 + offsetof(SpriteVertex, Position));
	glEnableVertexAttribArray(color_texture_program->Position_vec4);
	glVertexAttribPointer(color_texture_program->Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(SpriteVertex), (GLbyte *)0 + offsetof(SpriteVertex, Color));
	glEnableVertexAttribArray(color_texture_program->Color_vec4);
	glVertexAttribPointer(color_texture_program->TexCoord_vec2, 2, GL_FLOAT, GL_FALSE, sizeof(SpriteVertex), (GLbyte *)0 + offsetof(SpriteVertex, TexCoord));
	glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
	GL_ERRORS();
}

void Map::rebuild() {
	backdrop_.clear();
	strokes_.clear();

	glm::vec2 grid_min(bounds_min());
	glm::vec2 grid_max(bounds_max());

	auto add_stroke = [&](std::vector< glm::vec2 > pts, float width, glm::u8vec4 color, float boil, int salt, bool closed) {
		if (pts.size() < 2) return;
		Stroke stroke;
		stroke.pts = std::move(pts);
		stroke.width = width;
		stroke.color = color;
		stroke.boil = boil;
		stroke.salt = salt;
		stroke.closed = closed;
		strokes_.push_back(std::move(stroke));
	};

	glm::vec2 meadow_min = grid_min - glm::vec2(0.12f);
	glm::vec2 meadow_max = grid_max + glm::vec2(0.12f);
	paint_polygon(backdrop_, shape_round_rect(meadow_min - glm::vec2(0.12f), meadow_max + glm::vec2(0.12f), 0.45f, 4, 5, 0.04f), kGrassDark);
	auto meadow = shape_round_rect(meadow_min, meadow_max, 0.4f, 4, 6, 0.05f);
	paint_polygon(backdrop_, meadow, kGrass);

	// Alternating lawn tones, one per cell. A hair of overlap hides cracks.
	float half = tile_size_ * 0.5f;
	glm::vec2 pad(0.02f);
	for (int row = 0; row < rows_; ++row) {
		for (int col = 0; col < cols_; ++col) {
			glm::vec2 c(cell_center(col, row));
			glm::vec2 mn = c - glm::vec2(half) - pad;
			glm::vec2 mx = c + glm::vec2(half) + pad;
			glm::u8vec4 tone = ((col + row) & 1) ? kGrassShade : kGrassLight;
			tri(backdrop_, glm::vec2(mn.x, mn.y), glm::vec2(mx.x, mn.y), glm::vec2(mx.x, mx.y), tone);
			tri(backdrop_, glm::vec2(mn.x, mn.y), glm::vec2(mx.x, mx.y), glm::vec2(mn.x, mx.y), tone);
		}
	}

	for (int i = 0; i < 14; ++i) {
		float x = glm::mix(meadow_min.x, meadow_max.x, hash01(i, 1, 20));
		float y = glm::mix(meadow_min.y, meadow_max.y, hash01(i, 2, 21));
		float rx = 0.7f + hash01(i, 3, 22) * 1.3f;
		float ry = 0.5f + hash01(i, 4, 23) * 0.9f;
		bool light = hash01(i, 5, 24) > 0.5f;
		glm::u8vec4 wash = light ? glm::u8vec4(170, 176, 96, 55) : glm::u8vec4(50, 72, 32, 60);
		paint_ellipse(backdrop_, glm::vec2(x, y), rx, ry, hash01(i, 6, 25) * 3.0f, wash, 12, i, 0.08f);
	}

	add_stroke(meadow, 0.12f, kInk, 0.04f, 102, true);

	for (int row = 0; row < rows_; ++row) {
		for (int col = 0; col < cols_; ++col) {
			if (tiles_[index(col, row)] != Tile::Grass) continue;
			glm::vec2 c(cell_center(col, row));
			int seed = col * 17 + row * 53;
			for (int b = 0; b < 2; ++b) {
				glm::vec2 a = c + glm::vec2((hash01(seed, b, 6) - 0.5f) * 1.2f, (hash01(seed, b, 7) - 0.5f) * 1.2f);
				float ang = hash01(seed, b, 8) * kTau;
				glm::vec2 d(std::cos(ang), std::sin(ang));
				float len = 0.18f + hash01(seed, b, 9) * 0.22f;
				add_stroke({a, a + d * len}, 0.035f, glm::u8vec4(36, 58, 28, 255), 0.02f, seed + b, false);
			}
		}
	}

	upload(backdrop_vbo_, backdrop_vao_, backdrop_);
	backdrop_count_ = (GLuint)backdrop_.size();
}

void Map::upload(GLuint &vbo, GLuint &vao, std::vector< Vertex > const &verts) const {
	if (vbo == 0) glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.empty() ? nullptr : verts.data(), GL_DYNAMIC_DRAW);

	if (vao == 0) {
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glVertexAttribPointer(color_program->Position_vec4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Position));
		glEnableVertexAttribArray(color_program->Position_vec4);
		glVertexAttribPointer(color_program->Color_vec4, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (GLbyte *)0 + offsetof(Vertex, Color));
		glEnableVertexAttribArray(color_program->Color_vec4);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Map::draw_uploaded(GLuint vao, GLuint count, glm::mat4 const &clip_from_world) const {
	if (vao == 0 || count == 0) return;
	glUseProgram(color_program->program);
	glBindVertexArray(vao);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(clip_from_world));
	glDrawArrays(GL_TRIANGLES, 0, count);
}

size_t Map::index(int col, int row) const {
	return size_t(row * cols_ + col);
}
