#include "GameAudio.hpp"

#include "Load.hpp"
#include "Settings.hpp"
#include "Sound.hpp"
#include "World.hpp"
#include "data_path.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

struct SfxSpec {
	std::vector< std::string > files; // one is picked at random each time, so repeats don't sound robotic
	float volume;
	float min_gap; // seconds; drops repeats closer than this so big fights don't turn into noise
	bool enabled;
};

SfxSpec const &spec(GameAudio::Sfx sfx) {
	using GameAudio::Sfx;
	static std::array< SfxSpec, size_t(Sfx::Count) > const specs = {{
		// Gains are set from each file's loudness so they come out at the same level.
		/* UiHover     */ {{"ui_hover.wav"}, 0.48f, 0.03f, true},
		/* UiConfirm   */ {{"ui_confirm.wav"}, 0.32f, 0.0f, true},
		/* UiClick     */ {{"ui_click.wav"}, 0.61f, 0.0f, true},
		/* Place       */ {{"place.wav"}, 0.49f, 0.0f, true},
		/* PlaceFail   */ {{"place_fail.wav"}, 0.40f, 0.2f, true},
		/* SelectUnits */ {{"select_units.wav"}, 0.46f, 0.0f, true},
		/* Order       */ {{"order.wav"}, 0.73f, 0.0f, true},
		/* Pause       */ {{"pause.wav"}, 0.56f, 0.0f, true},
		/* Resume      */ {{"resume.wav"}, 0.55f, 0.0f, true},
		/* Hit         */ {{"hit_0.wav", "hit_1.wav", "hit_2.wav", "hit_3.wav", "hit_4.wav"}, 0.45f, 0.06f, true},
		/* BasketHit   */ {{"basket_hit_0.wav", "basket_hit_1.wav", "basket_hit_2.wav"}, 0.55f, 0.12f, true},
		/* BasketBreak */ {{"basket_break.wav"}, 0.51f, 0.0f, true},
		/* BrawlerAttack */ {{"brawler_attack.wav"}, 0.80f, 0.05f, true},
		/* RangedAttack  */ {{"ranged_attack.wav"}, 0.70f, 0.05f, true},
		/* HealerAttack  */ {{"healer_attack.wav"}, 2.09f, 0.05f, true},
		/* Pickup       */ {{"pickup.wav"}, 0.94f, 0.0f, true},
	}};
	return specs[size_t(sfx)];
}

struct Bank {
	std::array< std::vector< std::unique_ptr< Sound::Sample > >, size_t(GameAudio::Sfx::Count) > sfx;
	std::unique_ptr< Sound::Sample > title_music, battle_music, victory, defeat;
};

Load< Bank > bank(LoadTagDefault, []() -> Bank const * {
	auto load = [](std::string const &file) {
		return std::make_unique< Sound::Sample >(data_path("Sounds/" + file));
	};
	Bank *ret = new Bank();
	for (size_t i = 0; i < ret->sfx.size(); ++i) {
		for (std::string const &file : spec(GameAudio::Sfx(i)).files) {
			ret->sfx[i].emplace_back(load(file));
		}
	}
	ret->title_music = load("bgm_title.opus");
	ret->battle_music = load("bgm_battle.opus");
	ret->victory = load("jingle_victory.opus");
	ret->defeat = load("jingle_defeat.opus");
	return ret;
});

double now_seconds() {
	using namespace std::chrono;
	return duration< double >(steady_clock::now().time_since_epoch()).count();
}

std::array< double, size_t(GameAudio::Sfx::Count) > last_played = {};
std::mt19937 rng(0x5eed);

std::shared_ptr< Sound::PlayingSample > music;
Sound::Sample const *music_sample = nullptr;
float music_volume = 0.0f;
float music_level = 1.0f;

constexpr bool MusicEnabled = true;
constexpr bool JinglesEnabled = true;

constexpr float TitleMusicVolume = 0.35f;
constexpr float BattleMusicVolume = 0.3f;
constexpr float JingleVolume = 0.8f;
constexpr float MusicFade = 0.6f;

void fade_out_music() {
	if (music) music->stop(MusicFade);
	music.reset();
	music_sample = nullptr;
}

}

void GameAudio::play(Sfx sfx, float pan) {
	if (!spec(sfx).enabled) return;
	size_t i = size_t(sfx);
	auto const &samples = bank->sfx[i];
	if (samples.empty()) return;

	double t = now_seconds();
	if (t - last_played[i] < spec(sfx).min_gap) return;
	last_played[i] = t;

	size_t pick = std::uniform_int_distribution< size_t >(0, samples.size() - 1)(rng);
	Sound::play(*samples[pick], spec(sfx).volume * Settings::sfx_volume, std::clamp(pan, -1.0f, 1.0f));
}

void GameAudio::play_at(Sfx sfx, glm::vec2 world_position) {
	float half_width = World::get_world()->map.bounds_max().x;
	float pan = half_width > 0.0f ? 0.7f * world_position.x / half_width : 0.0f;
	play(sfx, pan);
}

void GameAudio::play_music(Music which) {
	if (!MusicEnabled) return;
	Sound::Sample const *sample = (which == Music::Title ? bank->title_music : bank->battle_music).get();
	if (music && music_sample == sample) return;
	fade_out_music();
	music_volume = (which == Music::Title ? TitleMusicVolume : BattleMusicVolume);
	music = Sound::loop(*sample, 0.0f);
	music->set_volume(music_volume * music_level * Settings::music_volume, MusicFade);
	music_sample = sample;
}

void GameAudio::set_music_level(float level) {
	music_level = level;
	refresh_music_volume();
}

void GameAudio::refresh_music_volume() {
	if (music) music->set_volume(music_volume * music_level * Settings::music_volume, 0.2f);
}

void GameAudio::stop_music() {
	fade_out_music();
}

void GameAudio::play_jingle(Jingle jingle) {
	fade_out_music();
	if (!JinglesEnabled) return;
	Sound::play(*(jingle == Jingle::Victory ? bank->victory : bank->defeat), JingleVolume * Settings::music_volume);
}
