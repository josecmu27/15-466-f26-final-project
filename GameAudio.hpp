#pragma once

// Game-wide sound effects and music. Sources and licenses: dist/Sounds/README-sounds.txt.

#include <glm/glm.hpp>

namespace GameAudio {

enum class Sfx {
	UiHover, UiConfirm, UiClick,
	Place, PlaceFail, SelectUnits, Order,
	Pause, Resume,
	Hit, BasketHit, BasketBreak,
	Count
};

enum class Music { Title, Battle };
enum class Jingle { Victory, Defeat };

void play(Sfx sfx, float pan = 0.0f);
// Pans by horizontal position on the map, so a fight on one side is heard on that side.
void play_at(Sfx sfx, glm::vec2 world_position);

// Crossfades from whatever music is playing; does nothing if `music` is already playing.
void play_music(Music music);
// Scales the current music's volume (1.0 = normal), e.g. to duck it while paused.
void set_music_level(float level);
void stop_music();

// Stops the music and plays a one-shot end-of-game tune.
void play_jingle(Jingle jingle);

}
