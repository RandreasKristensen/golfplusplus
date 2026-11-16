#include "core/scene_signs.h"

#include "core/render_frame.h"
#include "core/tee_book.h"
#include "game/model_kinds.h"

bool refresh_scene_hole_signs(render_hole_signs& signs,
                              const game_state& game,
                              const startup_catalog& catalog,
                              const text_assets& text,
                              const scene_colors& colors,
                              const prop_catalog& props) {
    return refresh_render_hole_signs(signs, game.area, game.terrain_render_revision, hole_sign_texts(game),
                                     prop_boards(game.area, props, prop_board_labels(game, catalog, text)), text, colors,
                                     props.at(hole_sign_model), signs.revision + 1);
}
