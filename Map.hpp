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
	enum class Tile : uint8_t {
		Grass,
		Base,
	};

	struct Cell {
		int col = 0;
		int row = 0;
		bool operator==(Cell const &o) const { return col == o.col && row == o.row; }
		bool operator!=(Cell const &o) const { return !(*this == o); }
	};

	struct Vertex {
		glm::vec3 Position;
		glm::u8vec4 Color;
	};

	Map();
	explicit Map(std::string const &ascii);
	~Map();

	Map(Map const &) = delete;
	Map &operator=(Map const &) = delete;

	int cols() const { return cols_; }
	int rows() const { return rows_; }
	float tile_size() const { return tile_size_; }

	bool in_bounds(int col, int row) const;
	Tile tile_at(int col, int row) const;
	static char const *tile_name(Tile tile);

	// Cell center on the XY plane (z = 0).
	glm::vec3 cell_center(int col, int row) const;
	glm::vec3 cell_center(Cell cell) const { return cell_center(cell.col, cell.row); }

	// Sprite origin for a tower: the center of a grass cell.
	glm::vec3 tower_anchor(int col, int row) const;

	// True when `world` (any z) lands inside the grid. Writes the cell.
	bool world_to_cell(glm::vec3 const &world, int &col, int &row) const;

	// Center of the base sprite. The shipped map covers the left-middle 2x2 cells.
	glm::vec3 base_position() const;
	Cell base_cell() const { return base_; }

	// Cells whose centers fall inside a circle on the XY plane.
	std::vector< Cell > cells_in_radius(glm::vec3 const &world, float radius) const;

	glm::vec3 bounds_min() const;
	glm::vec3 bounds_max() const;

	void draw(glm::mat4 const &clip_from_world, float time) const;

	// Film grain, a hairline scratch, and a vignette. Vertices are in clip space;
	// pass glm::mat4(1.0f).
	void draw_film(glm::mat4 const &clip_from_clip, float time) const;

private:
	struct Stroke {
		std::vector< glm::vec2 > pts;
		float width = 0.08f;
		glm::u8vec4 color = glm::u8vec4(27, 16, 18, 255);
		float boil = 0.03f;
		int salt = 0;
		bool closed = false;
	};

	void build_from_ascii(std::string const &ascii);
	void rebuild();
	void upload(GLuint &vbo, GLuint &vao, std::vector< Vertex > const &verts) const;
	void draw_uploaded(GLuint vao, GLuint count, glm::mat4 const &clip_from_world) const;

	size_t index(int col, int row) const;

	static constexpr float tile_size_ = 2.0f;

	int cols_ = 0;
	int rows_ = 0;
	std::vector< Tile > tiles_;

	Cell base_{};

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
