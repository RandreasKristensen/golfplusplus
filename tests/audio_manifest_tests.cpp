#include "doctest.h"

#include "audio/audio_manifest.h"

#include <string>

namespace {
const audio_sound_definition* find_sound(const audio_manifest& manifest, const std::string& id) {
    for (const audio_sound_definition& sound : manifest.sounds) {
        if (sound.id == id) {
            return &sound;
        }
    }
    return nullptr;
}

std::string asset_root() {
#ifdef VCR_GOLF_ASSETS_DIR
    return VCR_GOLF_ASSETS_DIR;
#else
    return "assets";
#endif
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
    CHECK(sound.description.empty());
    CHECK(sound.target_length_seconds == 0.0f);
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

TEST_CASE("project audio manifest lists initial sound set") {
    const audio_manifest_parse_result result = load_audio_manifest_from_file(asset_root() + "/audio/sounds.json");

    CHECK(result.manifest.has_value());
    if (!result.manifest) {
        return;
    }

    CHECK(find_sound(*result.manifest, "ui_move") != nullptr);
    CHECK(find_sound(*result.manifest, "club_hit_driver") != nullptr);
    CHECK(find_sound(*result.manifest, "ball_cup") != nullptr);
    CHECK(find_sound(*result.manifest, "cart_drive_loop") != nullptr);
    CHECK(find_sound(*result.manifest, "ambience_course_day") != nullptr);
    CHECK(find_sound(*result.manifest, "ambience_menu_vcr") != nullptr);

    const audio_sound_definition* cart_loop = find_sound(*result.manifest, "cart_drive_loop");
    const audio_sound_definition* course_day = find_sound(*result.manifest, "ambience_course_day");
    CHECK(cart_loop != nullptr);
    CHECK(course_day != nullptr);
    if (cart_loop == nullptr || course_day == nullptr) {
        return;
    }
    CHECK(cart_loop->type == audio_sound_type::loop);
    CHECK(course_day->type == audio_sound_type::ambience);
    CHECK(cart_loop->volume_multiplier == 1.0f);
    CHECK(course_day->volume_multiplier == 1.0f);
}
