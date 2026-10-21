#pragma once

// Helpers for tests of the server module's plain C++ (server/golfpp_module/src).

#include "game/content_files.h"
#include "game/course_loader.h"
#include "server_content.h"

#include "test_support.h"

#include <filesystem>
#include <map>
#include <string>

// Content files as the module holds them: shipped tuning, clubs, rewards and
// font, with the fixture courses. `files` points into `texts`, so a fixture
// moves but never copies.
struct content_fixture {
    std::map<std::string, std::string> texts;
    content_files files;

    content_fixture() = default;
    content_fixture(content_fixture&&) = default;
    content_fixture& operator=(content_fixture&&) = default;
    content_fixture(const content_fixture&) = delete;
    content_fixture& operator=(const content_fixture&) = delete;
};

// Every JSON file under `root`/`folder`, keyed by its path relative to `root`.
inline void add_content_folder(content_fixture& fixture, const std::string& root, const std::string& folder) {
    for (const std::filesystem::path& path : json_files_under(std::filesystem::path(root) / folder)) {
        fixture.texts[path.lexically_relative(root).generic_string()] = *read_text_file(path);
    }
}

inline content_fixture fixture_content_files() {
    content_fixture fixture;
    for (const char* folder : {"play", "interface/fonts"}) {
        add_content_folder(fixture, asset_root(), folder);
    }
    add_content_folder(fixture, fixture_root(), courses_path);
    for (const auto& [path, text] : fixture.texts) {
        fixture.files.emplace(path, text);
    }
    return fixture;
}
