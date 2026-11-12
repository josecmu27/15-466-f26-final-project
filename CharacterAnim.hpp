#pragma once

#include "GL.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

// One strip of numbered frames ("prefix1.png", "prefix2.png", ...) sharing a crop.
struct AnimClip {
	std::vector<GLuint> frames;
	glm::vec2 uv_min = glm::vec2(0.0f);
	glm::vec2 uv_max = glm::vec2(1.0f);
	glm::vec2 half = glm::vec2(0.7f);
	glm::vec2 offset = glm::vec2(0.0f);

	bool ready() const { return !frames.empty() && frames[0] != 0; }
	int count() const { return (int)frames.size(); }
};

// Walk and attack clips for the troop created with `troop_name`.
struct CharacterAnim {
	std::string troop_name;
	AnimClip walk;
	AnimClip attack;
	// Unflipped frames look toward -x when this is true. Current strips look toward +x.
	bool art_faces_left = false;
	bool show_in_bar = false;
	bool fit_bar_icon = false;

	bool attacking_pose(unsigned char pose) const { return pose == 2 && attack.ready(); }
	AnimClip const &clip_for(bool attacking) const { return attacking ? attack : walk; }
	bool mirrored(bool face_left) const { return face_left != art_faces_left; }
	glm::vec2 sprite_center(glm::vec2 pos, bool attacking, bool face_left) const {
		glm::vec2 placed = clip_for(attacking).offset;
		if (mirrored(face_left)) placed.x = -placed.x;
		return pos + placed;
	}
};

// One row registers a character. See the roster in PlayMode::load_troop_sprite.
struct CharacterAnimDesc {
	char const *troop_name;
	char const *walk_prefix;
	int walk_frames;
	char const *attack_prefix;
	int attack_frames;
	bool art_faces_left;
	bool show_in_bar;
	bool fit_bar_icon;
};
