#include "game/play_area.h"

#include "physics/det_math.h"
#include "game/area_solids.h"
#include "game/hub_definition.h"

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
#include <glm/vec2.hpp>

namespace {
// Andrew's monotone chain: the convex outline of `points`, counter-clockwise.
std::vector<glm::vec2> convex_hull(std::vector<glm::vec2> points) {
    std::sort(points.begin(), points.end(), [](const glm::vec2 a, const glm::vec2 b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    if (points.size() < 3) {
        return points;
    }
    const auto turn = [](const glm::vec2 o, const glm::vec2 a, const glm::vec2 b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<glm::vec2> hull(points.size() * 2);
    std::size_t count = 0;
    for (const glm::vec2 point : points) {
        while (count >= 2 && turn(hull[count - 2], hull[count - 1], point) <= 0.0f) {
            --count;
        }
        hull[count++] = point;
    }
    const std::size_t lower = count + 1;
    for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
        while (count >= lower && turn(hull[count - 2], hull[count - 1], *it) <= 0.0f) {
            --count;
        }
        hull[count++] = *it;
    }
    hull.resize(count - 1);
    return hull;
}

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
    auto placed = make_material_zone(zone.type, std::move(outline));
    placed.green_stimp_feet = zone.green_stimp_feet;
    return placed;
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

    std::vector<glm::vec2> hole_points;
    for (const terrain_mesh& hole : area.holes) {
        for (const terrain_vertex& vertex : hole.vertices) {
            hole_points.emplace_back(vertex.position.x, vertex.position.z);
        }
    }
    for (const material_zone& zone : area.zones) {
        const glm::vec2 half = zone_half_extent(zone);
        for (const glm::vec2 corner : {glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f)}) {
            hole_points.push_back(glm::vec2(zone.center.x, zone.center.z) + corner * half);
        }
    }
    area.holes_outline.clear();
    for (const glm::vec2 point : convex_hull(std::move(hole_points))) {
        area.holes_outline.emplace_back(point.x, 0.0f, point.y);
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
                           tuning.terrain.fairway_cell_size, tuning.terrain.zone_cell_size, tuning.terrain.zones,
                           tuning.tee_box.blend_distance, tuning.tee_box.sample_spacing};
}

// Metres per cell of the solids' grid, a few canopies wide. Any size gives
// the same contacts; only how many solids each test looks at depends on it.


// The area with its ground built from its holes and zones, a tee box at
// each hole's tee that has one (facing the way its sign does) levelled into
// it, and the zone shapes draped on that ground for drawing.
play_area with_ground(play_area area, const std::vector<hole_data>& holes, const height_grid* land, const float margin,
                      const game_tuning& tuning, const std::optional<ground_pad>& arrival_pad = std::nullopt) {
    std::vector<tee_box> boxes;
    std::vector<ground_pad> pads;
    for (std::size_t i = 0; i < holes.size(); ++i) {
        if (!holes[i].tee_box) {
            continue;
        }
        boxes.push_back(place_tee_box(holes[i], down_hole_at_tee(holes[i], tuning.hole_sign.look_ahead), tuning.tee_box));
        boxes.back().hole = i;
        pads.push_back(tee_box_pad(boxes.back()));
    }
    if (arrival_pad) { pads.push_back(*arrival_pad); }
    built_ground built = build_ground(area.holes, area.zones, pads, land, ground_settings_for(margin, tuning));
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        boxes[i].center.y = built.pad_levels[i];
    }
    area.tee_boxes = std::move(boxes);
    area.ground = std::move(built.mesh);
    area.water_levels = std::move(built.water_levels);
    area.material_overlay = build_material_overlay_mesh(area.ground, area.zones);
    return area;
}

// The area with `roads` laid on its finished ground, and no tree trunk
// within the paths' clearance.
play_area with_cart_paths(play_area area, const std::vector<course_world_cart_road>& roads, const game_tuning& tuning) {
    area.cart_paths = lay_cart_paths(area, roads, tuning.cart_path);
    const auto in_lane = [&](const tree_instance& tree) {
        return distance_past_cart_paths(area.cart_paths, tree.position) - tree.shape.trunk_radius < tuning.cart_path.clearance;
    };
    area.trees.erase(std::remove_if(area.trees.begin(), area.trees.end(), in_lane), area.trees.end());
    return area;
}

// The area with a sign at each hole's tee and one by each green on its cart
// paths, standing on its finished ground.
play_area with_signs(play_area area, const std::vector<hole_data>& holes, const game_tuning& tuning) {
    area.signs = place_hole_signs(area, holes, tuning.hole_sign, tuning.cart_path.clearance);
    area.way_signs = place_way_signs(area, holes, tuning.way_sign, tuning.cart_path.clearance);
    return area;
}

// The area with `fences` standing on its finished ground, open where they
// would stand within its cart paths' clearance: each a run of poles and the
// nets between them clear of it.
play_area with_fences(play_area area, const std::vector<course_world_fence>& fences, const game_tuning& tuning) {
    const float clearance = tuning.cart_path.clearance;
    const auto pole_clear = [&](const glm::vec3& pole) {
        return distance_past_cart_paths(area.cart_paths, pole) - tuning.fence.pole_radius >= clearance;
    };
    // Checked this often along a net.
    constexpr float net_check_step = 0.5f;
    const auto net_clear = [&](const glm::vec3& a, const glm::vec3& b) {
        const int steps = std::max(1, static_cast<int>(std::ceil(horizontal_distance(a, b) / net_check_step)));
        for (int k = 1; k < steps; ++k) {
            if (distance_past_cart_paths(area.cart_paths, glm::mix(a, b, static_cast<float>(k) / static_cast<float>(steps))) <
                clearance) {
                return false;
            }
        }
        return true;
    };
    for (const course_world_fence& fence : fences) {
        course_world_fence run{{}, fence.height};
        const auto close_run = [&]() {
            if (run.poles.size() >= 2) {
                area.fences.push_back(run);
            }
            run.poles.clear();
        };
        for (const glm::vec3& pole : fence.poles) {
            if (!pole_clear(pole)) {
                close_run();
                continue;
            }
            if (!run.poles.empty() && !net_clear(run.poles.back(), pole)) {
                close_run();
            }
            run.poles.push_back(glm::vec3(pole.x, terrain_height(area, pole), pole.z));
        }
        close_run();
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
    spline.paints_fairway = ribbon_paints_fairway(hole);
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
                            const game_tuning& tuning, const std::optional<ground_pad>& arrival_pad) {
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
    play_area grounded = with_bounds(with_ground(std::move(course), placed_holes, land, margin, tuning, arrival_pad));
    play_area standing = with_floodlights(
        with_fences(with_signs(with_cart_paths(std::move(grounded), world.cart_roads, tuning), placed_holes, tuning),
                    world.fences, tuning),
        placed_holes, world.cart_roads,
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
        made.up = sample_area(standing, made.centre).normal;
        standing.cups.push_back(made);
    }
    return standing;
}

play_area build_hub_area(const hub_definition& hub, const course_world_definition& world, const game_tuning& tuning,
                         const std::optional<ground_pad>& arrival_pad) {
    play_area area;
    area.zones = world.zones;
    area.trees = world.trees;
    std::vector<ground_pad> pads;
    for (std::size_t i = 0; i < hub.tee_mats.size(); ++i) {
        const auto& mat = hub.tee_mats[i];
        if (!mat.raised) { continue; }
        const auto along = safe_normalize(horizontal(mat.aim - mat.position), glm::vec3(0,0,1));
        area.tee_boxes.push_back({i, mat.position, along, tuning.tee_box.width * .5f, tuning.tee_box.length * .5f});
        pads.push_back(tee_box_pad(area.tee_boxes.back()));
    }
    if (arrival_pad) { pads.push_back(*arrival_pad); }
    auto ground = build_ground({}, area.zones, pads, &world.ground, ground_settings_for(0, tuning));
    for (std::size_t i = 0; i < area.tee_boxes.size(); ++i) { area.tee_boxes[i].center.y = ground.pad_levels[i]; }
    area.ground = std::move(ground.mesh); area.water_levels = std::move(ground.water_levels);
    area.material_overlay = build_material_overlay_mesh(area.ground, area.zones);
    area = with_bounds(std::move(area));
    area = with_fences(with_cart_paths(std::move(area), world.cart_roads, tuning), world.fences, tuning);
    area.solids = standing_solids(area, tuning);
    std::vector<cup> markers;
    for (const auto& target : hub.targets) { markers.push_back(cup{anchor_on_terrain(area, target.position),0,0,0}); }
    const auto sticks = flagstick_solids(markers, {}, tuning);
    area.solids.insert(area.solids.end(), sticks.begin(), sticks.end());
    area.solids_grid = build_solid_grid(area.solids, solid_grid_cell_size);
    for (const auto& point : hub.practice_cups) {
        const float unit = tuning.scale.meters_per_world_unit;
        const glm::vec3 centre = anchor_on_terrain(area, point);
        area.cups.push_back(cup{centre, tuning.cup.radius_meters * tuning.cup.leniency / unit,
            tuning.cup.depth_meters / unit, tuning.cup.rim_radius_meters / unit, sample_area(area, centre).normal});
    }
    return area;
}

bool under_water(const play_area& area, const glm::vec3& position) {
    const terrain_sample sample = sample_area(area, position);
    return sample.material == terrain_material::water && position.y < sample.water_level;
}

terrain_sample sample_area(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    terrain_sample sample = sample_terrain_mesh(area.ground, position, area.ground_y);
    const std::optional<terrain_sample> on_hole = sample_holes(area.holes, position);
    const auto zone = winning_zone_at(area.zones, position);
    sample.material = zone ? zone->material : on_hole ? on_hole->material : terrain_material::rough;
    if (zone) {
        sample.green_stimp_feet = area.zones[zone->index].green_stimp_feet;
        if (sample.material == terrain_material::water && zone->index < area.water_levels.size()) {
            sample.water_level = area.water_levels[zone->index];
        }
    }
    sample.distance_from_center = on_hole ? on_hole->distance_from_center : 0.0f;
    if (const std::optional<walkable_top> top = walkable_top_at(area.solids, area.solids_grid, position); top) {
        if (top->over_surface) {
            // A cart path's top, over the ground or the pond it bridges, following it.
            const bool pond = sample.material == terrain_material::water;
            sample.point.y = (pond ? std::max(sample.point.y, sample.water_level) : sample.point.y) + top->height;
            // Level across the path, following the ground along it.
            const glm::vec3 normal = pond ? world_up : sample.normal;
            sample.normal = safe_normalize(normal - top->across * glm::dot(normal, top->across), world_up);
            sample.top_solid = static_cast<int>(top->solid);
        } else if (top->height > sample.point.y) {
            sample.point.y = top->height;
            sample.normal = top->normal;
            sample.top_solid = static_cast<int>(top->solid);
        }
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

cart_ground cart_ground_at(const play_area& area, const game_tuning& tuning, const float wetness, const glm::vec3& position,
                           frame_profile* profile) {
    const terrain_sample sample = sample_area(area, position, profile);
    const cart_surfaces_tuning& surfaces = tuning.surfaces.cart;
    const bool on_road =
        sample.top_solid >= 0 && area.solids[static_cast<std::size_t>(sample.top_solid)].kind == solid_kind::path;
    const cart_surface_tuning& surface = on_road ? surfaces.path : cart_surface(surfaces, sample.material);
    const float wet = clamp01(wetness);
    cart_ground ground;
    ground.normal = sample.normal;
    ground.rolling_resistance = surface.rolling_resistance * glm::mix(1.0f, surface.wet_rolling_resistance_scale, wet);
    ground.grip = surface.grip * glm::mix(1.0f, surface.wet_grip_scale, wet);
    // A cart path over water is its bridge.
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
