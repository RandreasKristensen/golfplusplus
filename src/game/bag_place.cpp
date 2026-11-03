#include "game/bag_place.h"

bool same_place(const bag_place& a, const bag_place& b) {
    switch (a.where) {
    case bag_where::on_back:
        return b.where == bag_where::on_back;
    case bag_where::on_cart:
        return b.where == bag_where::on_cart && a.seat == b.seat && a.cart_owner == b.cart_owner;
    case bag_where::on_ground:
        return b.where == bag_where::on_ground && a.position == b.position && a.yaw == b.yaw;
    }
    return false;
}

glm::vec3 bag_at(const bag_place& bag, const cart_pose& cart, const glm::vec3& golfer, const cart_tuning& tuning) {
    switch (bag.where) {
    case bag_where::on_cart:
        return bag_spot(cart, bag.seat, tuning);
    case bag_where::on_back:
        return golfer;
    case bag_where::on_ground:
        return bag.position;
    }
    return golfer;
}
