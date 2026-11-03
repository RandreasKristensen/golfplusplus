#include "core/render_gear.h"

#include "core/render_online.h"
#include "game/bag_handling.h"
#include "game/gear_paint.h"
#include "game/golf_bag.h"
#include "game/golf_cart.h"
#include "game/mode_dispatch.h"
#include "game/play_area.h"
#include "physics/vector_math.h"
#include "renderer/bag_batch.h"
#include "renderer/cart_batch.h"

#include <cmath>
#include <optional>

#include <glm/gtc/constants.hpp>
#include <glm/vec4.hpp>

namespace {
const char* mount_of(const cart_seat seat) {
    return seat == cart_seat::driver ? "driver_bag" : "passenger_bag";
}

bool is_me(const game_state& game, const std::uint64_t account) {
    return account == 0 || account == game.online.account_id;
}

// The cart (its owner, 0: mine) that `account`'s bag at `place` is strapped on.
std::uint64_t strapped_on(const game_state& game, const bag_place& place, const std::uint64_t account) {
    const std::uint64_t owner = place.cart_owner != 0 ? place.cart_owner : account;
    return is_me(game, owner) ? 0 : owner;
}

std::string item_model(const game_state& game, const std::string& id) {
    const item_definition* item = game.items.find(id);
    return item != nullptr ? item->model : std::string();
}

float eased(const float t) {
    return t * t * (3.0f - 2.0f * t);
}

// `cart` as drawn with its model: on its wheels, going over or lying on a side.
render_cart drawn_cart(const game_state& game, const cart_in_world& cart, const prop_model& model) {
    const auto ground = [&game](const glm::vec3& point) { return terrain_height(game.area, point); };
    render_cart drawn = cart_tipped(cart.condition)
        ? cart_rolled(cart.pose.position, cart.pose.yaw, cart.condition == cart_condition::on_left_side, cart.fall_angle, model,
                      ground)
        : cart_on_ground(cart.pose.position, cart.pose.yaw, model, ground);
    drawn.driven = cart.driven;
    drawn.steer = cart.steer;
    drawn.paint = gear_paint_of(game, cart.owner);
    return drawn;
}

// `owner`'s cart (0: mine) as drawn, with its model.
std::optional<std::pair<render_cart, const prop_model*>> find_drawn_cart(const game_state& game, const std::uint64_t owner,
                                                                          const prop_catalog& props) {
    for (const cart_in_world& cart : carts_in_world(game)) {
        const auto model = props.find(item_model(game, cart.item));
        if (cart.owner == owner && model != props.end()) {
            return std::make_pair(drawn_cart(game, cart, model->second), &model->second);
        }
    }
    return std::nullopt;
}

// The frame of `owner`'s bag (`model`) at `place`: set down, on `figure`'s
// back, or strapped on a cart.
std::optional<glm::mat4> bag_frame_at(const game_state& game, const bag_place& place, const std::uint64_t owner,
                                      const render_remote_avatar& figure, const prop_model& model, const prop_catalog& props) {
    switch (place.where) {
    case bag_where::on_ground:
        return ground_bag_frame(render_ground_bag{std::string(), place.position, place.yaw, {}}, model);
    case bag_where::on_back:
        return back_bag_frame(figure, game.tuning.camera.walking_eye_height, model);
    case bag_where::on_cart: {
        const auto cart = find_drawn_cart(game, strapped_on(game, place, owner), props);
        if (!cart || cart->second->mounts.count(mount_of(place.seat)) == 0) {
            return std::nullopt;
        }
        return mounted_frame(cart_frame(cart->first), cart->second->mount(mount_of(place.seat)), model);
    }
    }
    return std::nullopt;
}

// Whether `account`'s bag is on its way somewhere, drawn as handled instead.
bool bag_moving(const game_state& game, const std::uint64_t account) {
    if (is_me(game, account)) {
        return game.handling && game.handling->kind == handling_kind::bag;
    }
    const auto avatar = game.remote_avatars.find(account);
    return avatar != game.remote_avatars.end() && avatar->second.handling &&
        avatar->second.handling->kind == handling_kind::bag;
}

std::string club_model(const game_state& game, const std::string& id) {
    const club_definition* club = find_club(game.clubs, id);
    return club != nullptr ? club->model : std::string();
}
}

std::string bag_model_of(const game_state& game, const std::uint64_t account) {
    if (is_me(game, account)) {
        return item_model(game, equipped_loadout(game).bag);
    }
    const std::optional<shown_bag> theirs = remote_bag(game, account);
    return theirs ? theirs->model : std::string();
}

void add_handling(render_data& data, render_remote_avatar& figure, const game_state& game, const bag_handling& handling,
                  const std::uint64_t owner, const std::string& bag_model, const prop_catalog& props) {
    const auto bag = props.find(bag_model);
    if (bag == props.end()) {
        return;
    }
    const prop_model& model = bag->second;
    const float t = handling_progress(handling, game.tuning.player);
    const float eye_height = game.tuning.camera.walking_eye_height;
    if (handling.kind == handling_kind::bag) {
        const std::optional<glm::mat4> from = bag_frame_at(game, handling.from, owner, figure, model, props);
        const std::optional<glm::mat4> to = bag_frame_at(game, handling.to, owner, figure, model, props);
        if (!from || !to) {
            return;
        }
        const render_handled_prop moving{bag_model, gear_paint_of(game, owner), *from, *to, eased(t), bag_arc_height};
        data.handled_props.push_back(moving);
        figure.bag_model.clear();
        figure.reach = glm::vec3(handled_prop_frame(moving) * glm::vec4(model.point("mouth"), 1.0f));
        // Down to the ground and up again; less to a cart's side.
        const bool low = handling.from.where == bag_where::on_ground || handling.to.where == bag_where::on_ground;
        figure.crouch = (low ? 1.0f : 0.4f) * std::sin(glm::pi<float>() * t);
        return;
    }
    const std::optional<glm::mat4> at = bag_frame_at(game, handling.to, owner, figure, model, props);
    if (!at) {
        return;
    }
    // The club in hand down into the bag, then the other drawn out of it.
    const bool putting_back = t < 0.5f;
    const std::string club = club_model(game, putting_back ? handling.club_out : handling.club_in);
    const auto club_prop = props.find(club);
    if (club_prop == props.end()) {
        return;
    }
    const glm::mat4 in_bag = club_in_bag_frame(*at, model);
    const glm::mat4 carried = carried_club_frame(figure, eye_height, club_prop->second);
    const float half = putting_back ? t * 2.0f : (t - 0.5f) * 2.0f;
    data.handled_props.push_back(putting_back ? render_handled_prop{club, std::nullopt, carried, in_bag, eased(half), 0.0f}
                                              : render_handled_prop{club, std::nullopt, in_bag, carried, eased(half), 0.0f});
    figure.carried_club_model.clear();
    figure.reach = glm::vec3(*at * glm::vec4(model.point("mouth"), 1.0f));
    figure.crouch = handling.to.where == bag_where::on_ground ? 0.3f * std::sin(glm::pi<float>() * t) : 0.0f;
}

void add_carts_and_bags(render_data& data, const game_state& game, const prop_catalog& props, render_remote_avatar* me) {
    for (const cart_in_world& cart : carts_in_world(game)) {
        const auto model = props.find(item_model(game, cart.item));
        if (model == props.end()) {
            continue;
        }
        render_cart drawn = drawn_cart(game, cart, model->second);
        drawn.model = model->first;
        if (game.bag.where == bag_where::on_cart && strapped_on(game, game.bag, 0) == cart.owner && !bag_moving(game, 0)) {
            drawn.bags[mount_of(game.bag.seat)] = render_strapped_bag{bag_model_of(game, 0), gear_paint_of(game, 0)};
        }
        if (is_online(game)) {
            for (const auto& [account, bag] : game.online.bags) {
                if (!is_me(game, account) && bag.place.where == bag_where::on_cart &&
                    strapped_on(game, bag.place, account) == cart.owner && !bag_moving(game, account)) {
                    drawn.bags[mount_of(bag.place.seat)] =
                        render_strapped_bag{bag_model_of(game, account), gear_paint_of(game, account)};
                }
            }
        }
        data.carts.push_back(drawn);
    }
    if (game.bag.where == bag_where::on_ground && !bag_moving(game, 0)) {
        data.ground_bags.push_back(
            render_ground_bag{bag_model_of(game, 0), game.bag.position, game.bag.yaw, gear_paint_of(game, 0)});
    }
    if (game.handling) {
        render_remote_avatar standing;
        standing.position = game.player.position;
        standing.yaw = game.player.yaw;
        standing.left_handed = game.hand == handedness::left;
        add_handling(data, me != nullptr ? *me : standing, game, *game.handling, 0, bag_model_of(game, 0), props);
    }
}
