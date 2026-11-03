#include "renderer/renderer.h"

#include <algorithm>

// Passes keep their framebuffers when made again, so the camcorder's iris
// and the tape's targets carry on; ensure_framebuffer_size resizes what the
// line count changes on the next frame. The shafts' targets are made again
// with their programs.
bool renderer::set_look(const std::string& asset_root, const look& next, const look_changes& changes) {
    picture_style_ = next.picture;
    colors_ = next.lighting.colors;
    const lighting_palette& palette = next.lighting;
    const auto path = [&asset_root](const char* name) { return shader_path(asset_root, name); };
    bool ok = true;
    if (changes.picture) {
        ok = camcorder_pass_.init(path(""), screen_vao_, next.picture.camcorder, next.picture.tape.fields_per_second) &&
            tape_pass_.init(path("crt.vert"), path("tape.frag"), screen_vao_, next.picture.tape) &&
            tv_pass_.init(path("crt.vert"), path("crt.frag"), screen_vao_, next.picture.tv) && ok;
    }
    if (changes.sky) {
        ok = backdrop_pass_.init(asset_root, path("backdrop.vert"), path("backdrop.frag"), screen_vao_, palette.sky) && ok;
    }
    if (changes.shadows) {
        ok = shadow_map_pass_.init(path("shadow_map.vert"), path("shadow_map.frag"), palette.shadows) &&
            blob_shadow_renderer_.init(path("blob_shadow.vert"), path("blob_shadow.frag")) && ok;
    }
    if (changes.shafts) {
        sun_shaft_pass_.shutdown();
        ok = lamp_shadow_pass_.init(path("shadow_map.vert"), path("shadow_map.frag"), palette.shafts) &&
            sun_shaft_pass_.init(path(""), screen_vao_, palette.shafts, shadow_map_unit, lamp_map_unit) && ok;
    }
    if (changes.streaks) {
        ok = weather_renderer_.init(path("weather_streak.vert"), path("weather_streak.frag"),
                                    std::max(palette.rain.max_drops, palette.wind.max_specks)) &&
            ok;
    }
    return ok;
}
