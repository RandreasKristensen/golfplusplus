#pragma once

// The scene's light and colours from assets/picture/lighting.json: art
// direction for the renderer, loaded by app like picture.json, so not part
// of game_content. The light is keyed on the sun's elevation, not the clock,
// so dusk lasts as long as it would at the course's latitude; the colours
// are what each thing is, whatever the light.

#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

inline constexpr const char* lighting_path = "picture/lighting.json";

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
    // The mist lying low (lighting.glsl's mist_depth): it takes mist_density
    // of the light per metre on the area's lowest ground (by Koschmieder,
    // a visibility there of 3.912 / mist_density metres), thinning by e every
    // mist_height_meters up.
    float mist_height_meters = 0.0f;
    float mist_density = 0.0f;
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
    // The cloud deck the sky's cover (game/weather_lighting.h) draws over the
    // panorama: noise on a ceiling over the camera, cloud_deck_scale cells
    // per unit of it across (a unit is as far as the ceiling is high),
    // drifting cloud_deck_drift units a second. Cloud wherever the noise is
    // over a threshold from cloud_deck_clear with no cover (none) to
    // cloud_deck_full under full cover, fully cloud cloud_deck_softness
    // past it. Its edges are cloud_deck_top times as bright as the sky
    // overhead, its thick middles cloud_deck_base of that.
    float cloud_deck_scale = 0.0f;
    float cloud_deck_drift = 0.0f;
    float cloud_deck_clear = 0.0f;
    float cloud_deck_full = 0.0f;
    float cloud_deck_softness = 0.0f;
    float cloud_deck_top = 0.0f;
    float cloud_deck_base = 0.0f;
    // A sun shower's rainbow, a ring round the point opposite the sun: the
    // primary rainbow_radius_deg out and rainbow_width_deg wide, red outside;
    // the secondary further out and wider, its colours reversed,
    // rainbow_secondary_share as bright. At its brightest it adds
    // rainbow_brightness of the sun's light; the sky inside the primary is
    // lighter by rainbow_inner_glow of the sun's light, and between the two
    // darker by rainbow_band_darkening (Alexander's band). It needs the sun
    // up: none with it on or below the horizon, full from
    // rainbow_sun_full_deg above it.
    float rainbow_radius_deg = 0.0f;
    float rainbow_width_deg = 0.0f;
    float rainbow_secondary_radius_deg = 0.0f;
    float rainbow_secondary_width_deg = 0.0f;
    float rainbow_secondary_share = 0.0f;
    float rainbow_brightness = 0.0f;
    float rainbow_inner_glow = 0.0f;
    float rainbow_band_darkening = 0.0f;
    float rainbow_sun_full_deg = 0.0f;
};

// What the weather (game/weather.h) does to the light (game/weather_lighting.h)
// and the ground. The sky's cover fades in and out over fade_seconds as
// drawn. Under full cover the sun keeps overcast_sun of its light and
// overcast_shadows of its shadows, overcast_scatter of the light it loses
// coming down from the whole sky; the sky's light, the haze and the sky
// turn overcast_grey of the way to grey, the sky overhead overcast_sky
// times overcast_sky_brightness times the clear sky's brightness there,
// in place of at least overcast_sky_blend of the sky panorama, keeping
// overcast_mottle of the panorama's clouds' contrast; the
// horizon's glow keeps overcast_glow; the haze adds overcast_haze and is full
// by overcast_haze_distance of its distance; the camcorder opens up by
// overcast_exposure and keeps overcast_saturation of its colour. Rain at
// its heaviest keeps rain_light of the light, darkens the clouds by
// rain_mottle where the panorama's are bright, adds rain_haze, shortens the haze to rain_haze_distance and
// adds rain_gain_noise. The thickest fog takes the sun's light like
// fog_cover of cloud, adds fog_haze (in which the sky fades), shortens the haze to
// fog_haze_distance and greys it fog_grey of the way. Soaked ground keeps
// wet_albedo of its colour and shines with wet_gloss, as sharp as
// wet_gloss_power. The air scatters the night lights' light towards the
// eye as thick as its visibility says: clear_visibility_meters in clear
// air, rain_visibility_meters in the heaviest rain, fog_visibility_meters
// in the thickest fog, each keeping on forwards as much as its
// *_forwardness (Henyey-Greenstein's g).
// The haze glows with the sun looking towards it, sun_glow_at_sun of the
// sun's light looking straight at it, spreading round the sky as far as
// sun_glow_spread (1 - Henyey-Greenstein's g: 1 even all round, small held
// close to the sun; make_render_sky_weather turns the two into g and its
// average, so the glare at the sun stays put as the spread changes); and there is always
// some mist: the haze grows with distance from its square (mist 0) towards
// straight (mist 1), the same at its full distance either way. The thickest
// fog adds fog_mist_density and the heaviest rain rain_mist_density to the
// low mist's density on the lowest ground (lighting_key::mist_density).
struct weather_style {
    float fade_seconds = 0.0f;
    float overcast_sun = 0.0f;
    float overcast_shadows = 0.0f;
    float overcast_scatter = 0.0f;
    float overcast_grey = 0.0f;
    glm::vec3 overcast_sky{0.0f};
    float overcast_sky_brightness = 0.0f;
    float overcast_sky_blend = 0.0f;
    float overcast_mottle = 0.0f;
    float overcast_glow = 0.0f;
    float overcast_haze = 0.0f;
    float overcast_haze_distance = 0.0f;
    float overcast_exposure = 0.0f;
    float overcast_saturation = 0.0f;
    float rain_light = 0.0f;
    float rain_mottle = 0.0f;
    float rain_haze = 0.0f;
    float rain_haze_distance = 0.0f;
    float rain_gain_noise = 0.0f;
    float fog_cover = 0.0f;
    float fog_haze = 0.0f;
    float fog_haze_distance = 0.0f;
    float fog_grey = 0.0f;
    float wet_albedo = 0.0f;
    float wet_gloss = 0.0f;
    float wet_gloss_power = 0.0f;
    float clear_visibility_meters = 0.0f;
    float clear_forwardness = 0.0f;
    float rain_visibility_meters = 0.0f;
    float rain_forwardness = 0.0f;
    float fog_visibility_meters = 0.0f;
    float fog_forwardness = 0.0f;
    float sun_glow_at_sun = 0.0f;
    float sun_glow_spread = 0.0f;
    float mist = 0.0f;
    float fog_mist_density = 0.0f;
    float rain_mist_density = 0.0f;
};

// Rain as drawn (renderer/weather_renderer.h): up to max_drops streaks in a
// volume_size box that wraps round the camera, max_drops of them at rain
// amount 1 and amount^drop_power of them at less (heavy rain far denser than
// light), falling from light_fall_speed
// towards heavy_fall_speed at amount 1 and blown by the wind at wind_height metres. A streak is as long as the
// drop falls in streak_seconds, light_alpha to heavy_alpha opaque, in
// `color` lit by the light there is, fading out nearer than near_fade
// metres and towards the box's edge.
struct rain_style {
    int max_drops = 0;
    float drop_power = 0.0f;
    float volume_size = 0.0f;
    float light_fall_speed = 0.0f;
    float heavy_fall_speed = 0.0f;
    float wind_height = 0.0f;
    float streak_seconds = 0.0f;
    float light_alpha = 0.0f;
    float heavy_alpha = 0.0f;
    float near_fade = 0.0f;
    glm::vec3 color{0.0f};
};

// The wind as drawn. Every flag hangs at flag_calm_speed (m/s at its height)
// and below, streams straight out at flag_full_speed and above, flapping
// from flag_calm_hz to flag_full_hz, its waves flag_flap of its length.
// Bits of grass and leaf (`speck_color`) blow past in a speck_volume_size
// box round the camera with the wind at speck_height metres: max_specks of
// them at speck_full_speed and up, speck_calm_share of that in calm, each a
// streak as long as it moves in speck_seconds, tumbling speck_tumble metres
// up and down.
struct wind_style {
    float flag_calm_speed = 0.0f;
    float flag_full_speed = 0.0f;
    float flag_calm_hz = 0.0f;
    float flag_full_hz = 0.0f;
    float flag_flap = 0.0f;
    int max_specks = 0;
    float speck_volume_size = 0.0f;
    float speck_height = 0.0f;
    float speck_full_speed = 0.0f;
    float speck_calm_share = 0.0f;
    float speck_seconds = 0.0f;
    float speck_tumble = 0.0f;
    glm::vec3 speck_color{0.0f};
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
    glm::vec3 cart_path{0.0f};   // a cart path's top (renderer/cart_path_mesh.h)
};

// The ball, tee boxes, cups and flags, aim dots and collectibles: lost balls,
// most in the ball's white, a share of them in one of the paints.
struct marker_colors {
    glm::vec3 ball{0.0f};
    glm::vec3 tee_grout{0.0f};
    glm::vec3 tee_tile{0.0f};
    glm::vec3 tee_mat{0.0f};
    std::vector<glm::vec3> collectible_paints;
    float collectible_paint_share = 0.0f;
    glm::vec3 cup{0.0f};
    glm::vec3 pin_pole{0.0f};
    glm::vec3 flag{0.0f};
};

// Other players: every figure's clothes, a shirt per player and a cap per
// group (picked by id), and their balls, tinted towards their shirt. A
// golfer's bag and cart are painted alike in their colour
// (game/gear_paint.h): the bag in a dark shade of it, the cart in a faded one.
struct figure_colors {
    glm::vec3 trousers{0.0f};
    glm::vec3 skin{0.0f};
    glm::vec3 cap{0.0f};  // out of a group
    glm::vec3 shoes{0.0f};
    std::vector<glm::vec3> shirts;
    std::vector<glm::vec3> group_caps;
    glm::vec3 ball{0.0f};
    float ball_shirt_share = 0.0f;
    std::vector<glm::vec3> bag_paints;   // one per favourite colour
    std::vector<glm::vec3> cart_paints;  // the same colours' carts
};

// The least light some things show, 0..1, so they stay readable when the
// light is gone: the ball, cup, flag and what glows of itself.
struct glow_floors {
    float ball = 0.0f;
    float cup = 0.0f;
    float pin_pole = 0.0f;
    float flag = 0.0f;
    float ember = 0.0f;
    float flight_path = 0.0f;
};

struct scene_colors {
    ground_colors ground;
    marker_colors markers;
    figure_colors figures;
    glow_floors glow;
};

// Shadows: a soft dark blob under the ball cast along the sun
// (core/light_frame.h, blob_shadow.vert), blobs at the feet of players,
// carts and trees, the shadow map of trees, floodlights, players and carts
// (renderer/shadow_map_pass.h) and
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
    // What casts a blob: the drawn ball, its radius times ball_radius_scale,
    // cast along the sun. A player (player_radius) and a cart (cart_radius)
    // only darken the ground at their foot, foot_share of blob_strength dark
    // by day, their shadow the shadow map's; a tree likewise, always that
    // faint, tree_radius_share of its crown's radius wide.
    float ball_radius_scale = 0.0f;
    float player_radius = 0.0f;
    float cart_radius = 0.0f;
    float tree_radius_share = 0.0f;
    float foot_share = 0.0f;
    float pole_radius = 0.0f;  // a floodlight pole's blob at its foot
    // The shadow map: map_resolution texels square over map_extent
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

// The sun's shafts in the haze (renderer/sun_shaft_pass.h): the shadows of
// trees, floodlights, players and carts on the shadow map take the sun's
// glow out of the haze they fall through, each ray stepped `steps` times out
// to reach_meters (the sky's rays and the far ground's end there); the pass
// is skipped when the most it could take is under min_glow. The same pass
// steps the beams of the `lamps` floodlights nearest the camera through
// their own shadow maps of the trees and floodlights (renderer/lamp_shadow_pass.h),
// lamp_map_resolution texels square, seen from lamp_near_meters out so a
// lamp's own head casts nothing.
struct shaft_style {
    int steps = 0;
    float reach_meters = 0.0f;
    float min_glow = 0.0f;
    int lamps = 0;
    int lamp_map_resolution = 0;
    float lamp_near_meters = 0.0f;
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
// face glows in the floodlight's colour, past 1 so the camcorder blooms and
// smears them. A surface takes a light as its reach says; the air (and the
// rain in it) takes it as a real lamp's, by the inverse square of the
// distance, as bright as a surface's at beam_near_share of its reach and
// softened over beam_core_meters, the lamp's housing: bright at the lamp,
// faint down its beam.
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
    float beam_near_share = 0.0f;
    float beam_core_meters = 0.0f;
};

// Lightning in a thunderstorm (core/lightning_frame.h): strikes_per_minute
// at full thunder, at random, each flashing for flash_seconds in a few
// pulses pulse_seconds long. bolt_share of them are bolts down to the
// horizon from a top between top_min_deg and top_max_deg up, the rest flash
// inside the cloud only. A strike is near_meters to far_meters away, its
// thunder heard after the sound covers that at sound_speed m/s: the close
// clap within close_meters, the far rumble beyond. As drawn
// (assets/picture/shaders/backdrop.frag), its bolt is bolt_width_deg wide,
// wandering up to bolt_jitter_deg either way, bolt_brightness bright in
// `color`, lighting the clouds within glow_radius_deg of its top by
// glow_strength.
struct lightning_style {
    float strikes_per_minute = 0.0f;
    float flash_seconds = 0.0f;
    float pulse_seconds = 0.0f;
    float bolt_share = 0.0f;
    float top_min_deg = 0.0f;
    float top_max_deg = 0.0f;
    float near_meters = 0.0f;
    float far_meters = 0.0f;
    float close_meters = 0.0f;
    float sound_speed = 0.0f;
    float bolt_width_deg = 0.0f;
    float bolt_jitter_deg = 0.0f;
    float bolt_brightness = 0.0f;
    float glow_radius_deg = 0.0f;
    float glow_strength = 0.0f;
    glm::vec3 color{0.0f};
};

struct lighting_palette {
    std::vector<lighting_key> keys;  // by rising sun elevation
    sky_style sky;
    scene_colors colors;
    shadow_style shadows;
    shaft_style shafts;
    night_light_style night_lights;
    weather_style weather;
    rain_style rain;
    wind_style wind;
    lightning_style lightning;
};

// nullopt unless every field is present (the sky's clouds, the shadows',
// night lights', weather's, rain's and wind's numbers within the ranges
// lighting_fields.h gives them, map_resolution a power of two), every colour is three numbers in
// 0..1, every glow, sky_blend, horizon_glow_strength, gain_noise,
// saturation and sky share in 0..1, exposure within 0.1..8, the sun's
// brightness within 1..64,
// every sky angle within 0..90 (the stars' sun elevations within -90..0, the
// full one below the appearing one), and the keys rise strictly in sun
// elevation within -90..90.
std::optional<lighting_palette> parse_lighting_from_text(const std::string& text);
std::optional<lighting_palette> load_lighting(const std::string& asset_root);
