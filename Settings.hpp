#pragma once

// Player options from the title screen's settings panel. They last until the game closes.
namespace Settings {

extern float music_volume; // 0..1, scales background music and the victory/defeat jingles
extern float sfx_volume;   // 0..1, scales sound effects
extern bool fullscreen;
extern bool film_effect;   // old-film vignette, grain and scratches over the battlefield
extern float game_speed;   // 1 or 2; speeds up the battle itself, not the camera

}
