#include "core/app.h"
#include "core/benchmark_csv.h"
#include "core/benchmark_driver.h"
#include "renderer/gl_loader.h"
#include <algorithm>
#include <fstream>

bool app::run_benchmark(const startup_options& options) {
    if (menu_.flow != startup_flow::playing || game_.place.id != options.boot_course_id || SDL_GL_GetSwapInterval() != 0) {
        SDL_Log("Benchmark needs a valid offline place and swap interval zero");
        return false;
    }
    if (!game_.items.find(game_.default_loadout.cart) ||
        (game_.hub && (game_.hub->tee_mats.empty() || game_.hub->floodlights.empty() || !game_.world.bus_stop ||
          std::none_of(game_.hub->anchors.pads.begin(), game_.hub->anchors.pads.end(),
                       [](const hub_pad& pad) { return pad.name == "plaza"; }))) ||
        (!game_.hub && (game_.holes.empty() || game_.selected_club >= game_.clubs.size()))) {
        SDL_Log("Benchmark place lacks a required camera anchor or equipped model");
        return false;
    }
    const auto directory = std::filesystem::path(options.benchmark_directory);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) { return false; }
    if (std::filesystem::exists(directory / "frames.csv", error) || error) {
        SDL_Log("Benchmark output already exists or cannot be read");
        return false;
    }
    refresh_render_meshes();
    int width = 0, height = 0, battery = 0;
    SDL_GL_GetDrawableSize(window_.sdl_window(), &width, &height);
    const auto power = SDL_GetPowerInfo(nullptr, &battery);
    std::ofstream info(directory / "context.txt");
    info << "place=" << game_.place.id << "\nrenderer=" << glGetString(GL_RENDERER)
         << "\ndriver=" << glGetString(GL_VERSION) << "\ndrawable=" << width << 'x' << height
         << "\nswap_interval=" << SDL_GL_GetSwapInterval() << "\npower_sdl=" << power << "\nbattery_percent=" << battery
         << "\ngrass_block_width=" << lighting_.grass.tuft_spacing_meters * grass_block_tufts
         << "\ngrass_reach=" << lighting_.grass.reach_meters
         << "\ncontent_hash=" << content_.content_hash << "\nmeasured_ms=" << options.benchmark_ms
         << "\nreverse=" << options.benchmark_reverse
         << "\ngrass_tile_layers=" << renderer_.grass_tile_capacity() << '\n';
    if (!info || width != options.window_width || height != options.window_height) { return false; }
    std::ofstream frames(directory / "frames.csv");
    if (!frames) { return false; }
    benchmark_csv_header(frames);
    benchmark_driver script(frames, game_, water_, options.benchmark_ms, options.benchmark_reverse);
    benchmark_ = &script;
    run([&](const frame_profile& frame) { script.record(frame); });
    benchmark_ = nullptr;
    frames.close();
    const std::uint64_t field_bytes = grass_field_memory_bytes(grass_field_);
    const std::uint64_t decoded = renderer_.grass_decoded_bytes();
    const std::uint64_t bake_bytes = grass_field_.bake_index_bytes;
    // The baker's indices live only while the course loads, the draw scratch only while it draws.
    info << "grass_gpu_bytes=" << renderer_.grass_gpu_bytes() << "\ngrass_decoded_bytes=" << decoded
         << "\ngrass_field_ram_bytes=" << field_bytes << "\ngrass_cache_file_bytes=" << grass_field_file_bytes(grass_field_)
         << "\ngrass_storage=" << grass_storage_name(grass_field_.storage) << "\ngrass_bake_peak_bytes=" << bake_bytes
         << "\ngrass_tiles_fetched_ahead=" << renderer_.grass().tiles().tiles_fetched_ahead()
         << "\ngrass_tiles_unreadable=" << renderer_.grass().tiles().tiles_unreadable()
         << "\ngrass_warm_frames=" << renderer_.grass().warm_frames()
         << "\ngrass_ram_min_bytes=" << field_bytes + decoded + bake_bytes << "\ngrass_ram_bound_bytes="
         << field_bytes + decoded + std::max<std::uint64_t>(bake_bytes, renderer_.grass_draw_scratch_bound()) << '\n';
    info.close();
    return script.complete() && frames.good() && info.good();
}
