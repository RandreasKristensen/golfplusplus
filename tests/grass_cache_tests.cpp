#include "doctest.h"

#include "grass_oracle.h"
#include "renderer/grass_cache.h"
#include "renderer/grass_tiles.h"
#include "test_support.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

// A directory of its own, removed with it.
class scratch_directory {
public:
    scratch_directory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("golfpp_grass_cache_" + std::to_string(stamp));
    }
    ~scratch_directory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    scratch_directory(const scratch_directory&) = delete;
    scratch_directory& operator=(const scratch_directory&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

struct meshes {
    render_static_mesh ground;
    render_static_mesh overlay;
    std::vector<tee_box> tees;
};

meshes course() {
    meshes made;
    for (const glm::vec2 p : {glm::vec2(-50.3f, -40.2f), glm::vec2(60.1f, -40.2f), glm::vec2(60.1f, 45.7f), glm::vec2(-50.3f, 45.7f)}) {
        render_terrain_vertex v;
        v.position = glm::vec3(p.x, 3.0f + 0.05f * p.x, p.y);
        v.set_blades(1.0f);
        made.ground.vertices.push_back(v);
    }
    made.ground.indices = {0, 1, 2, 0, 2, 3};
    made.ground.bounds = compute_render_mesh_bounds(made.ground.vertices);
    made.ground.revision = 3;
    for (const glm::vec2 p : {glm::vec2(-10.0f, -10.0f), glm::vec2(20.0f, -10.0f), glm::vec2(20.0f, 5.0f)}) {
        render_terrain_vertex v;
        v.position = glm::vec3(p.x, 3.0f, p.y);
        made.overlay.vertices.push_back(v);
    }
    made.overlay.indices = {0, 1, 2};
    made.overlay.bounds = compute_render_mesh_bounds(made.overlay.vertices);
    tee_box tee;
    tee.center = glm::vec3(-30.0f, 3.0f, 20.0f);
    tee.half_width = 1.0f;
    tee.half_length = 2.0f;
    made.tees = {tee};
    return made;
}

grass_field bake(const meshes& made, const fs::path& directory) {
    return bake_grass_field(made.ground, made.overlay, made.tees, directory);
}

std::vector<fs::path> cache_files(const fs::path& directory) {
    std::vector<fs::path> files;
    std::error_code error;
    for (fs::directory_iterator at(directory, error), end; !error && at != end; at.increment(error)) { files.push_back(at->path()); }
    return files;
}

// Every stored tile of two fields decodes to the same texels.
void same_tiles(const grass_field& a, const grass_field& b) {
    REQUIRE(a.tiles.size() == b.tiles.size());
    grass_tile_texels x;
    grass_tile_texels y;
    for (int tz = 0; tz < a.tile_counts.y; ++tz) {
        for (int tx = 0; tx < a.tile_counts.x; ++tx) {
            const glm::ivec2 tile = a.first_tile + glm::ivec2(tx, tz);
            REQUIRE(fill_grass_tile(a, tile, x));
            REQUIRE(fill_grass_tile(b, tile, y));
            REQUIRE(x.height == y.height);
            REQUIRE(x.clearance == y.clearance);
            REQUIRE(x.kind == y.kind);
            const grass_tile_info& info_a = *grass_tile_at(a, tile);
            const grass_tile_info& info_b = *grass_tile_at(b, tile);
            REQUIRE(info_a.low == info_b.low);
            REQUIRE(info_a.high == info_b.high);
            REQUIRE(info_a.max_clearance == info_b.max_clearance);
        }
    }
}
}

TEST_CASE("a course's grass is baked into a cache file once and reused, the same tiles either way") {
    const scratch_directory directory;
    const meshes made = course();
    const grass_field memory = bake(made, {});
    CHECK(memory.storage == grass_storage::memory);
    const grass_field written = bake(made, directory.path());
    REQUIRE(written.storage == grass_storage::file_written);
    CHECK(cache_files(directory.path()).size() == 1);
    // Almost nothing of the tiles stays in memory.
    CHECK(grass_field_memory_bytes(written) < grass_field_memory_bytes(memory));
    CHECK(written.backing->file_bytes() > 0);
    const grass_field reused = bake(made, directory.path());
    REQUIRE(reused.storage == grass_storage::file_reused);
    CHECK(cache_files(directory.path()).size() == 1);
    CHECK(reused.revision == made.ground.revision);
    same_tiles(memory, written);
    same_tiles(memory, reused);
    CHECK(reused.bake_index_bytes == 0);
}

TEST_CASE("a cache is keyed by every input of the bake, so changed meshes find no old file") {
    const meshes made = course();
    const std::uint64_t key = grass_content_key(made.ground, made.overlay, made.tees);
    CHECK(key == grass_content_key(made.ground, made.overlay, made.tees));
    const auto differs = [&](const meshes& other) { return grass_content_key(other.ground, other.overlay, other.tees) != key; };
    meshes taller = made;
    taller.ground.vertices[2].position.y += 0.001f;
    CHECK(differs(taller));
    meshes rougher = made;
    rougher.ground.vertices[1].set_blades(2.0f);
    CHECK(differs(rougher));
    meshes reordered = made;
    reordered.ground.indices = {0, 2, 1, 0, 2, 3};
    CHECK(differs(reordered));
    meshes wider = made;
    wider.overlay.vertices[2].position.x += 0.01f;
    CHECK(differs(wider));
    meshes moved = made;
    moved.tees[0].center.z += 0.01f;
    CHECK(differs(moved));
    meshes without = made;
    without.tees.clear();
    CHECK(differs(without));
    // Colours and normals are not read by the bake.
    meshes tinted = made;
    tinted.ground.vertices[0].set_color(glm::vec3(0.1f));
    tinted.ground.revision = 99;
    CHECK(!differs(tinted));

    const scratch_directory directory;
    const grass_field first = bake(made, directory.path());
    REQUIRE(first.storage == grass_storage::file_written);
    const grass_field second = bake(taller, directory.path());
    CHECK(second.storage == grass_storage::file_written);
    CHECK(cache_files(directory.path()).size() == 2);
    const grass_field again = bake(taller, directory.path());
    CHECK(again.storage == grass_storage::file_reused);
}

TEST_CASE("a damaged or foreign cache file is removed and baked again, never read") {
    const meshes made = course();
    const grass_field memory = bake(made, {});
    const auto damage = [&](const auto& change) {
        const scratch_directory directory;
        // The first field is gone before its file is damaged: an open file cannot be replaced on Windows.
        { REQUIRE(bake(made, directory.path()).storage == grass_storage::file_written); }
        const auto files = cache_files(directory.path());
        REQUIRE(files.size() == 1);
        change(files[0]);
        const grass_field after = bake(made, directory.path());
        CHECK(after.storage == grass_storage::file_written);
        same_tiles(memory, after);
        CHECK(bake(made, directory.path()).storage == grass_storage::file_reused);
    };
    const auto overwrite = [](const fs::path& path, const std::streamoff at, const char byte) {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(at);
        file.put(byte);
    };
    // truncated
    damage([](const fs::path& path) {
        std::error_code error;
        fs::resize_file(path, fs::file_size(path, error) / 2, error);
    });
    // header magic
    damage([&](const fs::path& path) { overwrite(path, 0, 'X'); });
    // version
    damage([&](const fs::path& path) { overwrite(path, 8, 99); });
    // lattice
    damage([&](const fs::path& path) { overwrite(path, 24, 7); });
    // index
    damage([&](const fs::path& path) {
        std::error_code error;
        overwrite(path, static_cast<std::streamoff>(fs::file_size(path, error)) - 40, 0x5a);
    });
    // footer
    damage([&](const fs::path& path) {
        std::error_code error;
        overwrite(path, static_cast<std::streamoff>(fs::file_size(path, error)) - 3, 0x5a);
    });
    // empty file
    damage([](const fs::path& path) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
    });
}

TEST_CASE("a tile damaged in a good file is refused when read, and no other tile is") {
    const scratch_directory directory;
    const meshes made = course();
    const grass_field written = bake(made, directory.path());
    REQUIRE(written.storage == grass_storage::file_written);
    const auto files = cache_files(directory.path());
    REQUIRE(files.size() == 1);
    {
        std::fstream file(files[0], std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(64 + 100);
        const char flipped = 0x33;
        file.write(&flipped, 1);
    }
    // The file is read through its own open handle: reopen it as a later run would.
    const grass_field reopened = bake(made, directory.path());
    REQUIRE(reopened.storage == grass_storage::file_reused);
    int refused = 0;
    int read = 0;
    grass_tile_texels texels;
    for (int tz = 0; tz < reopened.tile_counts.y; ++tz) {
        for (int tx = 0; tx < reopened.tile_counts.x; ++tx) {
            const glm::ivec2 tile = reopened.first_tile + glm::ivec2(tx, tz);
            if (grass_tile_at(reopened, tile)->max_clearance == 0.0f) { continue; }
            (fill_grass_tile(reopened, tile, texels) ? read : refused) += 1;
        }
    }
    CHECK(refused == 1);
    CHECK(read >= 1);
    // A refused tile is the empty one: nothing grows from it.
    CHECK(texels.clearance.size() == static_cast<std::size_t>(grass_tile_nodes) * grass_tile_nodes);
}

TEST_CASE("when no cache file can be made the field is held in memory and is the same field") {
    const scratch_directory directory;
    std::error_code error;
    fs::create_directories(directory.path(), error);
    // A regular file where the cache directory should be.
    const fs::path blocked = directory.path() / "not_a_directory";
    { std::ofstream file(blocked, std::ios::binary); file << "x"; }
    const meshes made = course();
    const grass_field memory = bake(made, {});
    const grass_field fallback = bake(made, blocked);
    CHECK(fallback.storage == grass_storage::memory);
    same_tiles(memory, fallback);
    // Nor is a missing parent a problem: it is created.
    const grass_field nested = bake(made, directory.path() / "a" / "b");
    CHECK(nested.storage == grass_storage::file_written);
    // No temporary file is left behind.
    for (const fs::path& path : cache_files(directory.path() / "a" / "b")) { CHECK(path.extension() == ".cache"); }
}
