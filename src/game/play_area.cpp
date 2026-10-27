#include "game/play_area.h"

#include "physics/det_math.h"
#include "game/area_solids.h"

#include "physics/ground_mesh.h"
#include "physics/land_relief.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
terrain_mesh place_mesh(const terrain_mesh& mesh, const hole_data& hole, const course_world_hole_start& start) {
    terrain_mesh placed = mesh;
    const float radians = glm::radians(start.rotation_degrees);
    for (terrain_vertex& vertex : placed.vertices) {
        vertex.position = place_hole_point(hole, start, vertex.position);
        vertex.normal = rotate_about_y(vertex.normal, radians);
    }
    return build_terrain_mesh_index(std::move(placed));
}

// The zone where the placed hole puts it: every point of its outline placed.
material_zone place_zone(const material_zone& zone, const hole_data& hole, const course_world_hole_start& start) {
    std::vector<glm::vec2> outline;
    for (const glm::vec2& point : zone.outline) {
        const glm::vec3 placed = place_hole_point(hole, start, glm::vec3(point.x, 0.0f, point.y));
        outline.emplace_back(placed.x, placed.z);
    }
    return make_material_zone(zone.type, std::move(outline));
}

play_area with_bounds(play_area area) {
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    for (const terrain_vertex& vertex : area.ground.vertices) {
        low = glm::min(low, vertex.position);
        high = glm::max(high, vertex.position);
    }
    if (low.x > high.x) {
        return area;
    }
    area.center = horizontal((low + high) * 0.5f);
    area.extent = std::max(high.x - low.x, high.z - low.z) * 0.5f;

    glm::vec3 holes_low(std::numeric_limits<float>::max());
    glm::vec3 holes_high(std::numeric_limits<float>::lowest());
    for (const terrain_mesh& hole : area.holes) {
        for (const terrain_vertex& vertex : hole.vertices) {
            holes_low = glm::min(holes_low, vertex.position);
            holes_high = glm::max(holes_high, vertex.position);
        }
    }
    for (const material_zone& zone : area.zones) {
        const glm::vec2 half = zone_half_extent(zone);
        holes_low = glm::min(holes_low, zone.center - glm::vec3(half.x, 0.0f, half.y));
        holes_high = glm::max(holes_high, zone.center + glm::vec3(half.x, 0.0f, half.y));
    }
    if (holes_low.x <= holes_high.x) {
        area.holes_low = horizontal(holes_low);
        area.holes_high = horizontal(holes_high);
    }
    return area;
}

// Full ribbon width (fairway + rough); also how far the ground reaches past
// the holes.
float ribbon_width(const hole_data& hole) {
    return std::max(hole.spline.width, hole.spline.rough_width);
}

ground_settings ground_settings_for(const float margin, const game_tuning& tuning) {
    return ground_settings{tuning.terrain.ground_cell_size, margin, tuning.terrain.ground_blend_distance,
                           tuning.terrain.fairway_cell_size, tuning.terrain.zone_cell_size, tuning.terrain.zones};
}

// Metres per cell of the solids' grid, a few canopies wide. Any size gives
// the same contacts; only how many solids each test looks at depends on it.
constexpr float solid_grid_cell_size = 16.0f;

// The area with its ground built from its holes and zones, and the zone
// shapes draped on that ground for drawing.
play_area with_ground(play_area area, const height_grid* land, const float margin, const game_tuning& tuning) {
    built_ground built = build_ground(area.holes, area.zones, land, ground_settings_for(margin, tuning));
    area.ground = std::move(built.mesh);
    area.water_levels = std::move(built.water_levels);
    area.material_overlay = build_material_overlay_mesh(area.ground, area.zones);
    return area;
}

// The area with a tee box at each hole's tee, over its finished ground. Each
// faces the way its sign does.
play_area with_tee_boxes(play_area area, const std::vector<hole_data>& holes, const game_tuning& tuning) {
    std::vector<tee_box> boxes;
    for (const hole_data& hole : holes) {
        boxes.push_back(place_tee_box(area, hole, down_hole_at_tee(hole, tuning.hole_sign.look_ahead), tuning.tee_box));
    }
    area.tee_boxes = std::move(boxes);
    // Their tops are the surface for what is placed on the ground next;
    // standing_solids makes the whole list once everything stands.
    area.solids = tee_box_solids(area.tee_boxes, tuning);
    area.solids_grid = build_solid_grid(area.solids, solid_grid_cell_size);
    return area;
}

// The area with a sign at each hole's tee, standing on its finished ground.
play_area with_signs(play_area area, const std::vector<hole_data>& holes, const game_tuning& tuning) {
    area.signs = place_hole_signs(area, holes, tuning.hole_sign);
    return area;
}

// The area with `fences` standing on its finished ground.
play_area with_fences(play_area area, const std::vector<course_world_fence>& fences) {
    for (course_world_fence fence : fences) {
        for (glm::vec3& pole : fence.poles) {
            pole.y = terrain_height(area, pole);
        }
        area.fences.push_back(std::move(fence));
    }
    return area;
}

// The area with floodlights along `holes`, standing on its finished ground
// clear of its signs and fences.
play_area with_floodlights(play_area area, const std::vector<hole_data>& holes,
                           const std::vector<course_world_cart_road>& roads, const game_tuning& tuning) {
    area.floodlights = place_floodlights(area, holes, roads, tuning);
    return area;
}

// The hole's ribbon in hole space.
terrain_mesh build_hole_mesh(const hole_data& hole, const game_tuning& tuning) {
    terrain_spline spline;
    spline.control_points = hole.spline.control_points;
    spline.bank = hole.spline.bank;
    spline.width = ribbon_width(hole);
    spline.fairway_width = hole.spline.width;
    spline.paints_fairway = !has_fairway_outline(hole);
    spline.sample_count = tuning.terrain.min_sections;
    return build_terrain_mesh(spline);
}

// A placed hole's ribbon as its lift over the course's land: the hole's own
// heights over its tee (at `tee_height`) across the fairway, easing to none
// at the ribbon's outer edge, so neighbouring holes meet on the land rather
// than on each other's heights.
terrain_mesh lift_over_land(terrain_mesh hole, const float tee_height, const float fairway_width) {
    const float fairway_half = fairway_width * 0.5f;
    const float rough_band = std::max(0.001f, hole.width * 0.5f - fairway_half);
    for (terrain_vertex& vertex : hole.vertices) {
        const float t = clamp01((std::abs(vertex.distance_from_center) - fairway_half) / rough_band);
        vertex.position.y = (vertex.position.y - tee_height) * (1.0f - t * t * (3.0f - 2.0f * t));
    }
    hole.vertices = with_smooth_normals(std::move(hole.vertices), hole.indices);
    return build_terrain_mesh_index(std::move(hole));
}
}

glm::vec3 place_hole_point(const hole_data& hole, const course_world_hole_start& start, const glm::vec3& point) {
    return start.position + rotate_about_y(point - hole.tee_position, glm::radians(start.rotation_degrees));
}

hole_data place_hole(const hole_data& hole, const course_world_hole_start& start) {
    hole_data placed = hole;
    placed.tee_position = place_hole_point(hole, start, hole.tee_position);
    placed.pin_position = place_hole_point(hole, start, hole.pin_position);
    for (glm::vec3& point : placed.spline.control_points) {
        point = place_hole_point(hole, start, point);
    }
    for (material_zone& zone : placed.material_zones) {
        zone = place_zone(zone, hole, start);
    }
    for (tree_instance& tree : placed.trees) {
        tree.position = place_hole_point(hole, start, tree.position);
    }
    return placed;
}

play_area place_course_holes(const std::vector<hole_data>& holes,
                             const course_world_definition& world,
                             const game_tuning& tuning) {
    play_area course;
    const bool on_land = !world.ground.heights.empty();
    for (std::size_t i = 0; i < holes.size() && i < world.hole_starts.size(); ++i) {
        const hole_data placed = place_hole(holes[i], world.hole_starts[i]);
        terrain_mesh mesh = place_mesh(build_hole_mesh(holes[i], tuning), holes[i], world.hole_starts[i]);
        course.holes.push_back(on_land ? lift_over_land(std::move(mesh), placed.tee_position.y, holes[i].spline.width)
                                       : std::move(mesh));
        course.zones.insert(course.zones.end(), placed.material_zones.begin(), placed.material_zones.end());
        course.trees.insert(course.trees.end(), placed.trees.begin(), placed.trees.end());
        if (i == 0) {
            course.ground_y = placed.tee_position.y;
        }
    }
    return course;
}

play_area build_course_area(const std::vector<hole_data>& holes,
                            const course_world_definition& world,
                            const game_tuning& tuning) {
    play_area course = place_course_holes(holes, world, tuning);
    std::vector<hole_data> placed_holes;
    float margin = 0.0f;
    for (std::size_t i = 0; i < course.holes.size(); ++i) {
        placed_holes.push_back(place_hole(holes[i], world.hole_starts[i]));
        margin = std::max(margin, ribbon_width(holes[i]));
    }
    // The holes' own heights (hole-editor touch-ups) lift the relieved land
    // as authored; only the land's relief is scaled.
    const height_grid relieved = relieved_land(world.ground, course.zones, tuning.terrain.relief);
    const height_grid* land = world.ground.heights.empty() ? nullptr : &relieved;
    play_area grounded = with_tee_boxes(with_bounds(with_ground(std::move(course), land, margin, tuning)),
                                        placed_holes, tuning);
    play_area standing = with_floodlights(
        with_fences(with_signs(std::move(grounded), placed_holes, tuning), world.fences), placed_holes, world.cart_roads,
        tuning);
    standing.solids = standing_solids(standing, tuning);
    standing.solids_grid = build_solid_grid(standing.solids, solid_grid_cell_size);
    const float unit = tuning.scale.meters_per_world_unit;
    for (const hole_data& hole : placed_holes) {
        cup made;
        made.centre = anchor_on_terrain(standing, hole.pin_position);
        made.radius = tuning.cup.radius_meters * tuning.cup.leniency / unit;
        made.depth = tuning.cup.depth_meters / unit;
        made.rim_radius = tuning.cup.rim_radius_meters / unit;
        standing.cups.push_back(made);
    }
    return standing;
}

bool under_water(const play_area& area, const glm::vec3& position) {
    const terrain_sample sample = sample_area(area, position);
    return sample.material == terrain_material::water && position.y < sample.water_level;
}

terrain_sample sample_area(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    terrain_sample sample = sample_terrain_mesh(area.ground, position, area.ground_y);
    const std::optional<terrain_sample> on_hole = sample_holes(area.holes, position);
    sample.material = surface_material(area.zones, on_hole ? &*on_hole : nullptr, position);
    if (sample.material == terrain_material::water) {
        if (const std::optional<zone_hit> pond = winning_zone_at(area.zones, position); pond && pond->index < area.water_levels.size()) {
            sample.water_level = area.water_levels[pond->index];
        }
    }
    sample.distance_from_center = on_hole ? on_hole->distance_from_center : 0.0f;
    if (const std::optional<float> top = walkable_top_at(area.solids, area.solids_grid, position);
        top && *top > sample.point.y) {
        sample.point.y = *top;
        sample.normal = world_up;
    }
    record_terrain_sample(profile, sample.triangles_tested);
    return sample;
}

float terrain_height(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    return sample_area(area, position, profile).point.y;
}

glm::vec3 anchor_on_terrain(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    const terrain_sample sample = sample_area(area, position, profile);
    return glm::vec3(position.x, sample.point.y, position.z);
}

glm::vec3 resting_ball_position(const play_area& area, const glm::vec3& position, const float radius) {
    const terrain_sample ground = sample_area(area, position);
    return ground.point + ground_normal(ground.normal) * radius;
}

glm::vec3 tee_stance_position(const play_area& area, const glm::vec3& ball, const glm::vec3& pin, const float stand_off) {
    glm::vec3 stance = ball - yaw_direction(yaw_towards(ball, pin)) * stand_off;
    stance.y = terrain_height(area, stance);
    return stance;
}

namespace {
// Within `margin` past a road's half width of it, and never less than `least`.
bool near_cart_road(const std::vector<course_world_cart_road>& roads, const glm::vec3& position, const float margin,
                    const float least) {
    const glm::vec3 point = horizontal(position);
    for (const course_world_cart_road& road : roads) {
        const float reach = std::max(least, road.width * 0.5f + margin);
        for (std::size_t i = 0; i + 1 < road.polyline.size(); ++i) {
            const glm::vec3 a = horizontal(road.polyline[i]);
            const glm::vec3 ab = horizontal(road.polyline[i + 1]) - a;
            const float length_squared = glm::dot(ab, ab);
            const float t = length_squared <= 0.0001f ? 0.0f : clamp01(glm::dot(point - a, ab) / length_squared);
            if (glm::length(point - (a + ab * t)) <= reach) {
                return true;
            }
        }
    }
    return false;
}

const cart_surface_tuning& cart_surface(const cart_surfaces_tuning& surfaces, const terrain_material material) {
    switch (material) {
    case terrain_material::fairway:
        return surfaces.fairway;
    case terrain_material::green:
        return surfaces.green;
    case terrain_material::bunker:
        return surfaces.bunker;
    case terrain_material::water:
        return surfaces.water;
    case terrain_material::rough:
        break;
    }
    return surfaces.rough;
}
}

bool on_cart_road(const std::vector<course_world_cart_road>& roads, const glm::vec3& position, const cart_tuning& cart) {
    return near_cart_road(roads, position, cart.road_reach_margin, cart.min_road_reach);
}

cart_ground cart_ground_at(const play_area& area, const std::vector<course_world_cart_road>& roads, const game_tuning& tuning,
                           const float wetness, const glm::vec3& position, frame_profile* profile) {
    const terrain_sample sample = sample_area(area, position, profile);
    const cart_surfaces_tuning& surfaces = tuning.surfaces.cart;
    const bool on_road = near_cart_road(roads, position, 0.0f, 0.0f);
    const cart_surface_tuning& surface = on_road ? surfaces.path : cart_surface(surfaces, sample.material);
    const float wet = clamp01(wetness);
    cart_ground ground;
    ground.normal = sample.normal;
    ground.rolling_resistance = surface.rolling_resistance * glm::mix(1.0f, surface.wet_rolling_resistance_scale, wet);
    ground.grip = surface.grip * glm::mix(1.0f, surface.wet_grip_scale, wet);
    // A cart road over water is its bridge or ford.
    if (sample.material == terrain_material::water && !on_road) {
        ground.water_depth = std::max(0.0f, sample.water_level - sample.point.y) * tuning.scale.meters_per_world_unit;
    }
    return ground;
}

std::vector<tree_body> standing_trees(const play_area& area, frame_profile* profile) {
    std::vector<tree_body> trees;
    trees.reserve(area.trees.size());
    for (const tree_instance& tree : area.trees) {
        trees.push_back(tree_body{anchor_on_terrain(area, tree.position, profile), tree.shape});
    }
    return trees;
}
