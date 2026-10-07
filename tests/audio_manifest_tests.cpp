#include "doctest.h"

#include "audio/audio_manifest.h"
#include "game/club_definition.h"

#include "test_support.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {
const audio_sound_definition* find_sound(const audio_manifest& manifest, const std::string& id) {
    for (const audio_sound_definition& sound : manifest.sounds) {
        if (sound.id == id) {
            return &sound;
        }
    }
    return nullptr;
}

// SDL_mixer picks a stream's decoder by the file's extension, so a file
// whose contents are another format never plays as music.
bool contents_match_extension(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::string head(12, ' ');
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    if (file.gcount() < static_cast<std::streamsize>(head.size())) {
        return false;
    }
    const std::string extension = path.extension().string();
    if (extension == ".wav") {
        return head.compare(0, 4, "RIFF") == 0 && head.compare(8, 4, "WAVE") == 0;
    }
    if (extension == ".ogg") {
        return head.compare(0, 4, "OggS") == 0;
    }
    if (extension == ".mp3") {
        const auto byte = [&head](const std::size_t i) { return static_cast<unsigned char>(head[i]); };
        return head.compare(0, 3, "ID3") == 0 || (byte(0) == 0xFFU && (byte(1) & 0xE0U) == 0xE0U);
    }
    return false;
}
}

TEST_CASE("audio manifest parser accepts valid manifest") {
    const audio_manifest_parse_result result = parse_audio_manifest(R"({
      "master_volume": 0.8,
      "categories": {
        "ui": 0.55,
        "gameplay": 0.85,
        "ambience": 0.45
      },
      "sounds": [
        {
          "id": "ui_move",
          "file": "sfx/ui_move.wav",
          "type": "sfx",
          "category": "ui",
          "description": "Short VHS menu cursor tick.",
          "target_length_seconds": 0.05,
          "volume_multiplier": 1.25
        },
        {
          "id": "ambience_menu_vcr",
          "file": "ambience/menu_vcr.ogg",
          "type": "ambience",
          "category": "ambience"
        }
      ]
    })");

    CHECK(result.manifest.has_value());
    if (!result.manifest) {
        return;
    }
    CHECK(result.manifest->master_volume == 0.8f);
    CHECK(result.manifest->category_volumes.at("ui") == 0.55f);
    CHECK(result.manifest->sounds.size() == 2);
    if (result.manifest->sounds.size() != 2) {
        return;
    }
    CHECK(result.manifest->sounds[0].id == "ui_move");
    CHECK(result.manifest->sounds[0].type == audio_sound_type::sfx);
    CHECK(result.manifest->sounds[0].volume_multiplier == 1.25f);
    CHECK(result.manifest->sounds[1].type == audio_sound_type::ambience);
}

TEST_CASE("audio manifest parser defaults optional fields") {
    const audio_manifest_parse_result result = parse_audio_manifest(R"({
      "sounds": [
        {
          "id": "club_change",
          "file": "sfx/club_change.wav"
        }
      ]
    })");

    CHECK(result.manifest.has_value());
    if (!result.manifest) {
        return;
    }
    CHECK(result.manifest->sounds.size() == 1);
    if (result.manifest->sounds.size() != 1) {
        return;
    }
    const audio_sound_definition& sound = result.manifest->sounds[0];
    CHECK(result.manifest->master_volume == 1.0f);
    CHECK(sound.category == "gameplay");
    CHECK(sound.volume_multiplier == 1.0f);
    CHECK(sound.type == audio_sound_type::sfx);
}

TEST_CASE("audio manifest parser clamps negative sound multipliers") {
    const audio_manifest_parse_result result = parse_audio_manifest(R"({
      "sounds": [
        { "id": "muted", "file": "sfx/muted.wav", "volume_multiplier": -2.0 }
      ]
    })");

    CHECK(result.manifest.has_value());
    if (!result.manifest || result.manifest->sounds.empty()) {
        return;
    }
    CHECK(result.manifest->sounds[0].volume_multiplier == 0.0f);
}

TEST_CASE("audio manifest parser rejects invalid json") {
    const audio_manifest_parse_result result = parse_audio_manifest("{ bad json");

    CHECK(!result.manifest.has_value());
    CHECK(!result.error.empty());
}

TEST_CASE("audio manifest parser rejects duplicate ids") {
    const audio_manifest_parse_result result = parse_audio_manifest(R"({
      "sounds": [
        { "id": "ui_move", "file": "sfx/ui_move.wav" },
        { "id": "ui_move", "file": "sfx/ui_move_alt.wav" }
      ]
    })");

    CHECK(!result.manifest.has_value());
    CHECK(result.error.find("duplicate") != std::string::npos);
}

TEST_CASE("audio manifest parser keeps SDL_mixer asset classes explicit") {
    const audio_manifest_parse_result result = parse_audio_manifest(R"({
      "sounds": [
        { "id": "ball_roll", "file": "sfx/ball_roll.wav", "type": "loop" },
        { "id": "ambience_course_day", "file": "ambience/course_day.ogg", "type": "ambience", "category": "ambience" }
      ]
    })");

    CHECK(result.manifest.has_value());
    if (!result.manifest) {
        return;
    }
    const audio_sound_definition* roll = find_sound(*result.manifest, "ball_roll");
    const audio_sound_definition* ambience = find_sound(*result.manifest, "ambience_course_day");
    CHECK(roll != nullptr);
    CHECK(ambience != nullptr);
    if (roll == nullptr || ambience == nullptr) {
        return;
    }
    CHECK(roll->type == audio_sound_type::loop);
    CHECK(ambience->type == audio_sound_type::ambience);
    CHECK(ambience->file == "ambience/course_day.ogg");
}

TEST_CASE("every sound the game plays is in the manifest with an existing file") {
    const audio_manifest_parse_result result = load_audio_manifest_from_file(asset_root() + "/audio/sounds.json");
    REQUIRE(result.manifest.has_value());

    std::vector<std::string> played;
    std::ifstream header(std::string(GOLFPP_SOURCE_DIR) + "/audio/sound_ids.h");
    std::stringstream contents;
    contents << header.rdbuf();
    const std::string text = contents.str();
    const std::regex pattern(R"re(constexpr const char\* sound_\w+ = "([^"]*)";)re");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it) {
        played.push_back((*it)[1].str());
    }
    CHECK(played.size() > 10U);
    for (const club_definition& club : shipped_content().clubs) {
        played.push_back(club.hit_sound);
    }

    for (const std::string& id : played) {
        const audio_sound_definition* sound = find_sound(*result.manifest, id);
        CHECK(sound != nullptr);
        if (sound != nullptr) {
            CHECK(std::filesystem::exists(std::filesystem::path(asset_root()) / "audio" / sound->file));
        }
    }
}

TEST_CASE("every file in the manifest exists") {
    const audio_manifest_parse_result result = load_audio_manifest_from_file(asset_root() + "/audio/sounds.json");
    REQUIRE(result.manifest.has_value());
    for (const audio_sound_definition& sound : result.manifest->sounds) {
        CHECK(std::filesystem::exists(std::filesystem::path(asset_root()) / "audio" / sound.file));
    }
}

TEST_CASE("every file in the manifest is the format its extension says") {
    const audio_manifest_parse_result result = load_audio_manifest_from_file(asset_root() + "/audio/sounds.json");
    REQUIRE(result.manifest.has_value());
    for (const audio_sound_definition& sound : result.manifest->sounds) {
        CHECK(contents_match_extension(std::filesystem::path(asset_root()) / "audio" / sound.file));
    }
}

TEST_CASE("the master volume scales every sound and multiplies with the music volume for the ambience") {
    const audio_manifest_parse_result result = parse_audio_manifest(R"({
      "master_volume": 0.8,
      "categories": { "ui": 0.5 },
      "sounds": [ { "id": "click", "file": "click.wav", "category": "ui", "volume_multiplier": 0.5 } ]
    })");
    REQUIRE(result.manifest.has_value());
    const float mix = sound_mix_gain(*result.manifest, result.manifest->sounds[0]);
    CHECK(near(mix, 0.2f));

    const audio_levels full;
    CHECK(near(played_gain(mix, audio_sound_type::sfx, full), 0.2f));
    CHECK(near(played_gain(mix, audio_sound_type::ambience, full), 0.2f));

    audio_levels levels;
    levels.master = 0.5f;
    levels.music = 0.4f;
    CHECK(near(played_gain(mix, audio_sound_type::sfx, levels), 0.1f));
    CHECK(near(played_gain(mix, audio_sound_type::loop, levels), 0.1f));
    CHECK(near(played_gain(mix, audio_sound_type::ambience, levels), 0.04f));
    levels.master = 0.0f;
    CHECK(near(played_gain(1.0f, audio_sound_type::sfx, levels), 0.0f));
    CHECK(near(played_gain(1.0f, audio_sound_type::ambience, levels), 0.0f));
    levels.master = 2.0f;
    levels.music = 1.0f;
    CHECK(near(played_gain(1.0f, audio_sound_type::sfx, levels), 1.0f));
}
