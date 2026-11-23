#include "game/area_surface.h"

#include "physics/ground_mesh.h"

area_surface surface_at(const play_area& area, const glm::vec3& position) {
    area_surface surface;
    surface.on_hole = sample_holes(area.holes, area.surface, position);
    surface.zone = winning_zone_at(area.zones, area.surface, position);
    surface.material = surface_material(surface.zone ? std::optional<terrain_material>(surface.zone->material) : std::nullopt,
                                        surface.on_hole ? &*surface.on_hole : nullptr);
    return surface;
}

bool ground_at(const play_area& area, const glm::vec3& position) {
    return sample_terrain_inside(area.ground, position).has_value();
}

bool hole_owns_surface(const play_area& area, const area_surface& surface, const std::size_t hole, const glm::vec3& position) {
    if (surface.zone) {
        const bool hazard = surface.material == terrain_material::bunker || surface.material == terrain_material::water;
        if (hazard || surface.zone->index >= area.zone_holes.size() || area.zone_holes[surface.zone->index] == hole) {
            return true;
        }
    }
    // Whatever lies on its ribbon, another hole's fairway among it.
    return hole < area.holes.size() && sample_terrain_inside(area.holes[hole], position).has_value();
}
