#pragma once

/*
 * Grid map for a 2D tower defense game.
 *
 * The board lies on the XY plane, z = 0, centered on the origin.
 * +X is right, +Y is up. Column index increases with +X. Row 0 is the
 * top row of the ASCII picture, so row index increases as Y decreases.
 * One cell is `tile_size()` world units. Draw sprites as flat quads in
 * this same plane; an orthographic camera looks down -Z.
 *
 */

#include "GL.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <cstdint>

struct Map {
	struct Vertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
	};

	Map();
	explicit Map(std::string const &ascii);
	~Map();

	Map(Map const &) = delete;
	Map &operator=(Map const &) = delete;

	glm::vec3 bounds_min() const;
	glm::vec3 bounds_max() const;

	// True when `world` (any z) lands inside the grid. Writes the cell.
	bool world_to_cell(glm::vec3 const &world, int &col, int &row) const;

	// A troop moves in world XY. Its position is not a grid cell.
	struct Troop {
		glm::vec2 pos = glm::vec2(0.0f);
	};

	// Drops a stand-in troop at the center of the cell under `pos`.
	// False when that point is off the board or on the base.
	bool place_troop(glm::vec2 pos);
	std::vector< Troop > &troops() { return troops_; }

	// `hover` outside the grid hides the cursor mark.
	void draw(glm::mat4 const &clip_from_world, float time, int hover_col, int hover_row) const;

	// Film grain, a hairline scratch, and a vignette. Vertices are in clip space;
	// pass glm::mat4(1.0f).
	void draw_film(glm::mat4 const &clip_from_clip, float time) const;

private:
	enum class Tile : uint8_t {
		Grass,
		Base,
	};

	struct Stroke {
		std::vector< glm::vec2 > pts;
		float width = 0.08f;
		glm::u8vec4 color = glm::u8vec4(27, 16, 18, 255);
		float boil = 0.03f;
		int salt = 0;
		bool closed = false;
	};

	bool in_bounds(int col, int row) const;
	glm::vec3 cell_center(int col, int row) const;

	void build_from_ascii(std::string const &ascii);
	void rebuild();
	void upload(GLuint &vbo, GLuint &vao, std::vector< Vertex > const &verts) const;
	void draw_uploaded(GLuint vao, GLuint count, glm::mat4 const &clip_from_world) const;

	size_t index(int col, int row) const;

	static constexpr float tile_size_ = 2.0f;

	int cols_ = 0;
	int rows_ = 0;
	std::vector< Tile > tiles_;
	std::vector< Troop > troops_;

	std::vector< Vertex > backdrop_;
	std::vector< Stroke > strokes_;
	GLuint backdrop_vbo_ = 0;
	GLuint backdrop_vao_ = 0;
	GLuint backdrop_count_ = 0;

	mutable std::vector< Vertex > frame_;
	mutable GLuint frame_vbo_ = 0;
	mutable GLuint frame_vao_ = 0;

	void load_base_sprite();
	glm::vec2 base_anchor_ = glm::vec2(0.0f);
	glm::vec2 base_half_ = glm::vec2(1.0f);
	glm::vec2 base_uv_min_ = glm::vec2(0.0f);
	glm::vec2 base_uv_max_ = glm::vec2(1.0f);
	GLuint base_tex_ = 0;
	GLuint base_vbo_ = 0;
	GLuint base_vao_ = 0;
};
