#pragma once

// The scene's light and colours from assets/visuals/lighting.json: art
// direction for the renderer, loaded by app like picture.json, so not part
// of game_content. The light is keyed on the sun's elevation, not the clock,
// so dusk lasts as long as it would at the course's latitude; the colours
// are what each thing is, whatever the light.

#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

inline constexpr const char* lighting_path = "visuals/lighting.json";

// The light at one sun elevation. A surface takes the sky's light from
// above and the ground's from below (blended by how it faces), plus the sun
// as much as it faces it; what lights an unlit thing is the sky's plus the
// sun's, so the noon key's two add up to 1.
struct lighting_key {
    float sun_elevation_deg = 0.0f;
    glm::vec3 sun_color{0.0f};       // the light along the sun
    glm::vec3 ambient_sky{0.0f};     // the light from the sky above
    glm::vec3 ambient_ground{0.0f};  // the light bounced up from the ground
    glm::vec3 sky_zenith{0.0f};      // the sky overhead
    // How far the course's sky panorama gives way to a sky shading from the
    // haze at the horizon to sky_zenith (0 the panorama as drawn).
    float sky_blend = 0.0f;
    glm::vec3 haze_tint{0.0f};  // multiplies the course's haze colour
    // The sky's glow along the horizon towards the sun, and how strongly it
    // shows (0..1).
    glm::vec3 horizon_glow{0.0f};
    float horizon_glow_strength = 0.0f;
    // The camcorder (renderer/camcorder_pass.h): what its iris lets in on top
    // of metering, the white it is set to (multiplies the picture), how far
    // it has raised gain (0..1, gain-up noise) and how much colour it keeps.
    float exposure = 0.0f;
    glm::vec3 white_balance{0.0f};
    float gain_noise = 0.0f;
    float saturation = 0.0f;
};

// What the sky shows whatever the light (backdrop.frag): the sun, the moon
// and the stars, sized in degrees of sky so they look the same at any
// resolution, aspect and field of view.
struct sky_style {
    float sun_radius_deg = 0.0f;
    float sun_brightness = 0.0f;  // the disc, far past white for the camcorder to clip, bloom and smear
    float sun_halo_radius_deg = 0.0f;  // the sun's glare fades out this far from its centre
    float sun_halo_strength = 0.0f;    // 0..1
    float moon_radius_deg = 0.0f;
    glm::vec3 moon_color{0.0f};
    float moon_earthshine = 0.0f;   // how much of moon_color its dark side shows, 0..1
    float glow_width_deg = 0.0f;    // the horizon glow fades out this far round from the sun
    float glow_height_deg = 0.0f;   // and this far up from the horizon
    float star_spacing_deg = 0.0f;  // the sky is cut into cells this wide, each holding at most one star
    float star_density = 0.0f;      // the share of cells with a star, 0..1
    float star_radius_deg = 0.0f;   // never drawn smaller than a pixel
    float star_brightness = 0.0f;   // the brightest star, 0..1
    float star_twinkle = 0.0f;      // how much a star dims as it twinkles, 0..1
    float star_twinkle_hz = 0.0f;
    // The stars fade in as the sun sinks from stars_appear_deg to
    // stars_full_deg, and out towards the horizon below star_horizon_fade_deg.
    float stars_appear_deg = 0.0f;
    float stars_full_deg = 0.0f;
    float star_horizon_fade_deg = 0.0f;
    // The panorama's clouds (brighter than its mean by cloud_threshold, fully
    // by cloud_threshold + cloud_softness) catch the low sun as far as the
    // horizon glow shows: tinted towards the glow's colour, cloud_sun_gain
    // bright, within cloud_sun_width_deg of the sun, and multiplied by
    // cloud_shade away from it.
    float cloud_threshold = 0.0f;
    float cloud_softness = 0.0f;
    float cloud_sun_gain = 0.0f;
    float cloud_sun_width_deg = 0.0f;
    glm::vec3 cloud_shade{0.0f};
};

// The ground by surface: the terrain, ponds, and every map of a hole.
struct ground_colors {
    glm::vec3 fairway{0.0f};
    glm::vec3 fairway_edge{0.0f};  // the fairway shades towards this at its edge
    glm::vec3 rough{0.0f};
    glm::vec3 green{0.0f};
    glm::vec3 bunker{0.0f};
    glm::vec3 water{0.0f};
    glm::vec3 backdrop{0.0f};    // the plain under and around the course
    glm::vec3 underwater{0.0f};  // over the picture seen from under a pond
};

struct tree_colors {
    glm::vec3 trunk{0.0f};
    glm::vec3 leaves{0.0f};
};

// The ball, tee boxes, cups and flags, aim dots, collectibles and the club
// at address.
struct marker_colors {
    glm::vec3 ball{0.0f};
    glm::vec3 tee_grout{0.0f};
    glm::vec3 tee_tile{0.0f};
    glm::vec3 tee_mat{0.0f};
    glm::vec3 collectible{0.0f};
    glm::vec3 cup{0.0f};
    glm::vec3 pin_pole{0.0f};
    glm::vec3 flag{0.0f};
    glm::vec3 aim_dot{0.0f};
    glm::vec3 club_shaft{0.0f};
    glm::vec3 club_face{0.0f};
    glm::vec3 club_back{0.0f};
    glm::vec3 club_sole{0.0f};
};

struct cart_colors {
    glm::vec3 body{0.0f};
    glm::vec3 trim{0.0f};
    glm::vec3 cream{0.0f};
    glm::vec3 canopy{0.0f};
    glm::vec3 rubber{0.0f};
    glm::vec3 hub_cap{0.0f};
    glm::vec3 seat{0.0f};
    glm::vec3 bag{0.0f};
};

struct emote_colors {
    glm::vec3 cigarette{0.0f};
    glm::vec3 ember_dim{0.0f};  // the lit tip glows between these
    glm::vec3 ember_bright{0.0f};
    glm::vec3 smoke{0.0f};
    glm::vec3 can{0.0f};
    glm::vec3 can_label{0.0f};
    glm::vec3 can_lid{0.0f};
};

// Other players: every figure's clothes, a shirt per player and a cap per
// group (picked by id), and their balls, tinted towards their shirt.
struct figure_colors {
    glm::vec3 trousers{0.0f};
    glm::vec3 skin{0.0f};
    glm::vec3 cap{0.0f};  // out of a group
    glm::vec3 shoes{0.0f};
    std::vector<glm::vec3> shirts;
    std::vector<glm::vec3> group_caps;
    glm::vec3 ball{0.0f};
    float ball_shirt_share = 0.0f;
};

// Hole signs and fences.
struct fixture_colors {
    glm::vec3 sign_board{0.0f};
    glm::vec3 sign_post{0.0f};
    glm::vec3 fence_post{0.0f};
    glm::vec3 floodlight_pole{0.0f};
    glm::vec3 floodlight_head{0.0f};  // its housing; the lamp's face glows (night_light_style::lamp_glow)
};

// The least light some things show, 0..1, so they stay readable when the
// light is gone: the ball, cup, flag and aim dots, and what glows of itself.
struct glow_floors {
    float ball = 0.0f;
    float cup = 0.0f;
    float pin_pole = 0.0f;
    float flag = 0.0f;
    float aim_dot = 0.0f;
    float ember = 0.0f;
    float flight_path = 0.0f;
};

struct scene_colors {
    ground_colors ground;
    tree_colors trees;
    marker_colors markers;
    cart_colors cart;
    emote_colors emotes;
    figure_colors figures;
    fixture_colors fixtures;
    glow_floors glow;
};

// Shadows: soft dark blobs on the ground under the ball, players and carts,
// cast along the sun (core/light_frame.h, blob_shadow.vert), a faint one at
// every tree's foot, the trees' shadow map (renderer/shadow_map_pass.h) and
// the land's shadow behind hills at low sun (lighting.glsl's sun_shadow).
struct shadow_style {
    float blob_strength = 0.0f;  // how dark a blob is at its middle, 0..1
    // A blob's share of its strength with no sun: the faint dark under
    // things at night, 0..1.
    float night_share = 0.0f;
    // The sun's shadows fade in as it rises from sun_fade_start_deg to sun_fade_full_deg.
    float sun_fade_start_deg = 0.0f;
    float sun_fade_full_deg = 0.0f;
    float max_stretch = 0.0f;    // a blob is never cast longer than this many times its width, 1..16
    float contact_reach = 0.0f;  // metres: the night blob fades out as a thing rises this far off the ground
    float horizon_softness_deg = 0.0f;  // degrees of sun over which the land's shadow edge fades
    // What casts: the drawn ball's radius times ball_radius_scale; a player
    // as a blob of player_radius cast from player_height; a cart likewise. A
    // tree only darkens the ground at its foot, tree_radius_share of its
    // crown's radius wide and tree_contact_share of blob_strength dark: its
    // shadow is the shadow map's.
    float ball_radius_scale = 0.0f;
    float player_radius = 0.0f;
    float player_height = 0.0f;
    float cart_radius = 0.0f;
    float cart_height = 0.0f;
    float tree_radius_share = 0.0f;
    float tree_contact_share = 0.0f;
    float pole_radius = 0.0f;  // a floodlight pole's blob at its foot
    // The trees' shadow map: map_resolution texels square over map_extent
    // metres of ground, its middle map_ahead of the extent in front of the
    // camera; map_strength of the sun the shadow takes away (faded in like
    // the blobs), its edge blurred over map_softness texels, and faded out
    // over the outer map_edge_fade of the map.
    int map_resolution = 0;
    float map_extent = 0.0f;
    float map_ahead = 0.0f;
    float map_strength = 0.0f;
    float map_softness = 0.0f;
    float map_edge_fade = 0.0f;
};

// A light after dark: its colour times its intensity (past 1 blooms), and how
// far it reaches in metres.
struct night_light {
    glm::vec3 color{0.0f};
    float intensity = 0.0f;
    float radius = 0.0f;
};

// The lights that come on after dark (core/light_frame.h): round the ball, a
// cart's headlight and the floodlights' lamps (game/floodlights.h); the glow
// floors the ball and the cigarette's ember rise to, and how bright a lamp's
// face glows in the floodlight's colour, past 1 so the camcorder blooms them.
struct night_light_style {
    // The lights come on as the sun sinks from on_deg to full_deg.
    float on_deg = 0.0f;
    float full_deg = 0.0f;
    float ball_glow = 0.0f;
    float ember_glow = 0.0f;
    night_light ball;
    night_light headlight;
    float headlight_cone_deg = 0.0f;  // half the cone's width, to its edge
    float headlight_soft_deg = 0.0f;  // how far in from its edge it is full
    float headlight_pitch_deg = 0.0f;  // tipped down from level
    float headlight_forward = 0.0f;    // metres ahead of the driver
    float headlight_height = 0.0f;     // metres off the ground
    night_light floodlight;
    float floodlight_cone_deg = 0.0f;  // half the cone's width, to its edge
    float floodlight_soft_deg = 0.0f;  // how far in from its edge it is full
    float lamp_glow = 0.0f;
};

struct lighting_palette {
    std::vector<lighting_key> keys;  // by rising sun elevation
    sky_style sky;
    scene_colors colors;
    shadow_style shadows;
    night_light_style night_lights;
};

// nullopt unless every field is present (the sky's clouds, the shadows' and
// night lights' numbers within the ranges lighting_fields.h gives them,
// map_resolution a power of two), every colour is three numbers in
// 0..1, every glow, sky_blend, horizon_glow_strength, gain_noise,
// saturation and sky share in 0..1, exposure within 0.1..8, the sun's
// brightness within 1..64,
// every sky angle within 0..90 (the stars' sun elevations within -90..0, the
// full one below the appearing one), and the keys rise strictly in sun
// elevation within -90..90.
std::optional<lighting_palette> parse_lighting_from_text(const std::string& text);
std::optional<lighting_palette> load_lighting(const std::string& asset_root);
