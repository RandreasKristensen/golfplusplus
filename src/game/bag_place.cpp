#include "game/bag_place.h"

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
