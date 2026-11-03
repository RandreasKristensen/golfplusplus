#include "core/app.h"

#include "game/course_session.h"

#include <SDL.h>

void app::update_look(const float dt) {
    const bool panel = god_.enabled && menu_.flow == startup_flow::touring;
    if (const std::optional<look_reload> reload = run_look_tuning(look_tuning_, look_tuning_keys_of(input_), panel, lighting_, dt)) {
        apply_look(*reload);
    }
}

void app::apply_look(const look_reload& reload) {
    if (!renderer_.set_look(asset_root_, reload.next, reload.changes)) {
        SDL_Log("The reloaded look's shaders or targets failed to build");
    }
    lighting_ = reload.next.lighting;
    tape_style_ = reload.next.picture.tape;
    if (reload.changes.shadows) {
        tree_shadows_ = tree_shadow_cache{};
    }
    // Ground colours are baked into the course's meshes, which only loading
    // a course builds: the tour loads its course again, a round shows them
    // from the next course.
    if (reload.changes.ground && menu_.flow == startup_flow::touring) {
        const course_definition course = game_.course;
        start_course(game_, course);
    }
}
