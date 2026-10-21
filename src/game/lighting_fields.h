#pragma once

// The named fields of assets/picture/lighting.json and the ranges each must
// be within: what lighting_loader.cpp reads, and the lighting key's fields
// sun_lighting.cpp blends between keys.

#include "game/lighting_loader.h"

#include <glm/vec3.hpp>

template <typename T>
struct color_field {
    const char* key;
    glm::vec3 T::*member;
};

template <typename T>
struct share_field {
    const char* key;
    float T::*member;
};

inline constexpr color_field<lighting_key> key_fields[] = {
    {"sun_color", &lighting_key::sun_color},
    {"ambient_sky", &lighting_key::ambient_sky},
    {"ambient_ground", &lighting_key::ambient_ground},
    {"sky_zenith", &lighting_key::sky_zenith},
    {"haze_tint", &lighting_key::haze_tint},
    {"horizon_glow", &lighting_key::horizon_glow},
    {"white_balance", &lighting_key::white_balance},
};

// Each 0..1.
inline constexpr share_field<lighting_key> key_share_fields[] = {
    {"sky_blend", &lighting_key::sky_blend},
    {"horizon_glow_strength", &lighting_key::horizon_glow_strength},
    {"gain_noise", &lighting_key::gain_noise},
    {"saturation", &lighting_key::saturation},
};

inline constexpr float min_exposure = 0.1f;
inline constexpr float max_exposure = 8.0f;

// A number of a section, within [low, high].
template <typename T>
struct bounded_field {
    const char* key;
    float T::*member;
    float low;
    float high;
};

inline constexpr float max_frequency_hz = 100.0f;

inline constexpr bounded_field<sky_style> sky_fields[] = {
    {"sun_radius_deg", &sky_style::sun_radius_deg, 0.0f, 90.0f},
    {"sun_brightness", &sky_style::sun_brightness, 1.0f, 64.0f},
    {"sun_halo_radius_deg", &sky_style::sun_halo_radius_deg, 0.0f, 90.0f},
    {"sun_halo_strength", &sky_style::sun_halo_strength, 0.0f, 1.0f},
    {"moon_radius_deg", &sky_style::moon_radius_deg, 0.0f, 90.0f},
    {"moon_earthshine", &sky_style::moon_earthshine, 0.0f, 1.0f},
    {"glow_width_deg", &sky_style::glow_width_deg, 0.0f, 180.0f},
    {"glow_height_deg", &sky_style::glow_height_deg, 0.0f, 90.0f},
    {"star_spacing_deg", &sky_style::star_spacing_deg, 0.0f, 90.0f},
    {"star_density", &sky_style::star_density, 0.0f, 1.0f},
    {"star_radius_deg", &sky_style::star_radius_deg, 0.0f, 90.0f},
    {"star_brightness", &sky_style::star_brightness, 0.0f, 1.0f},
    {"star_twinkle", &sky_style::star_twinkle, 0.0f, 1.0f},
    {"star_twinkle_hz", &sky_style::star_twinkle_hz, 0.0f, max_frequency_hz},
    {"stars_appear_deg", &sky_style::stars_appear_deg, -90.0f, 0.0f},
    {"stars_full_deg", &sky_style::stars_full_deg, -90.0f, 0.0f},
    {"star_horizon_fade_deg", &sky_style::star_horizon_fade_deg, 0.0f, 90.0f},
    {"cloud_threshold", &sky_style::cloud_threshold, 0.0f, 4.0f},
    {"cloud_softness", &sky_style::cloud_softness, 0.01f, 4.0f},
    {"cloud_sun_gain", &sky_style::cloud_sun_gain, 0.0f, 4.0f},
    {"cloud_sun_width_deg", &sky_style::cloud_sun_width_deg, 1.0f, 180.0f},
    {"cloud_deck_scale", &sky_style::cloud_deck_scale, 0.01f, 64.0f},
    {"cloud_deck_drift", &sky_style::cloud_deck_drift, 0.0f, 1.0f},
    {"cloud_deck_clear", &sky_style::cloud_deck_clear, 0.0f, 2.0f},
    {"cloud_deck_full", &sky_style::cloud_deck_full, 0.0f, 2.0f},
    {"cloud_deck_softness", &sky_style::cloud_deck_softness, 0.01f, 1.0f},
    {"cloud_deck_top", &sky_style::cloud_deck_top, 0.0f, 4.0f},
    {"cloud_deck_base", &sky_style::cloud_deck_base, 0.0f, 1.0f},
    {"rainbow_radius_deg", &sky_style::rainbow_radius_deg, 1.0f, 89.0f},
    {"rainbow_width_deg", &sky_style::rainbow_width_deg, 0.1f, 20.0f},
    {"rainbow_secondary_radius_deg", &sky_style::rainbow_secondary_radius_deg, 1.0f, 89.0f},
    {"rainbow_secondary_width_deg", &sky_style::rainbow_secondary_width_deg, 0.1f, 20.0f},
    {"rainbow_secondary_share", &sky_style::rainbow_secondary_share, 0.0f, 1.0f},
    {"rainbow_brightness", &sky_style::rainbow_brightness, 0.0f, 4.0f},
    {"rainbow_inner_glow", &sky_style::rainbow_inner_glow, 0.0f, 1.0f},
    {"rainbow_band_darkening", &sky_style::rainbow_band_darkening, 0.0f, 1.0f},
};

inline constexpr float max_caster_metres = 10.0f;
inline constexpr float max_light_intensity = 16.0f;
inline constexpr float max_light_radius = 200.0f;
inline constexpr float max_glow = 16.0f;

inline constexpr bounded_field<shadow_style> shadow_fields[] = {
    {"blob_strength", &shadow_style::blob_strength, 0.0f, 1.0f},
    {"night_share", &shadow_style::night_share, 0.0f, 1.0f},
    {"sun_fade_start_deg", &shadow_style::sun_fade_start_deg, -90.0f, 90.0f},
    {"sun_fade_full_deg", &shadow_style::sun_fade_full_deg, -90.0f, 90.0f},
    {"max_stretch", &shadow_style::max_stretch, 1.0f, 16.0f},
    {"contact_reach", &shadow_style::contact_reach, 0.01f, max_caster_metres},
    {"horizon_softness_deg", &shadow_style::horizon_softness_deg, 0.01f, 45.0f},
    {"ball_radius_scale", &shadow_style::ball_radius_scale, 0.0f, max_caster_metres},
    {"player_radius", &shadow_style::player_radius, 0.0f, max_caster_metres},
    {"cart_radius", &shadow_style::cart_radius, 0.0f, max_caster_metres},
    {"tree_radius_share", &shadow_style::tree_radius_share, 0.0f, 4.0f},
    {"foot_share", &shadow_style::foot_share, 0.0f, 1.0f},
    {"pole_radius", &shadow_style::pole_radius, 0.0f, max_caster_metres},
    {"map_extent", &shadow_style::map_extent, 10.0f, 2000.0f},
    {"map_ahead", &shadow_style::map_ahead, 0.0f, 0.5f},
    {"map_strength", &shadow_style::map_strength, 0.0f, 1.0f},
    {"map_softness", &shadow_style::map_softness, 0.0f, 8.0f},
    {"map_edge_fade", &shadow_style::map_edge_fade, 0.01f, 1.0f},
};

inline constexpr int min_shadow_map_resolution = 64;
inline constexpr int max_shadow_map_resolution = 4096;

inline constexpr bounded_field<night_light_style> night_light_fields[] = {
    {"on_deg", &night_light_style::on_deg, -90.0f, 90.0f},
    {"full_deg", &night_light_style::full_deg, -90.0f, 90.0f},
    {"ball_glow", &night_light_style::ball_glow, 0.0f, max_glow},
    {"ember_glow", &night_light_style::ember_glow, 0.0f, max_glow},
    {"headlight_cone_deg", &night_light_style::headlight_cone_deg, 1.0f, 89.0f},
    {"headlight_soft_deg", &night_light_style::headlight_soft_deg, 0.0f, 89.0f},
    {"headlight_pitch_deg", &night_light_style::headlight_pitch_deg, -89.0f, 89.0f},
    {"headlight_forward", &night_light_style::headlight_forward, 0.0f, max_caster_metres},
    {"headlight_height", &night_light_style::headlight_height, 0.0f, max_caster_metres},
    {"floodlight_cone_deg", &night_light_style::floodlight_cone_deg, 1.0f, 89.0f},
    {"floodlight_soft_deg", &night_light_style::floodlight_soft_deg, 0.0f, 89.0f},
    {"lamp_glow", &night_light_style::lamp_glow, 0.0f, max_glow},
};

inline constexpr float max_fade_seconds = 120.0f;
inline constexpr float max_gloss_power = 256.0f;

inline constexpr bounded_field<weather_style> weather_fields[] = {
    {"fade_seconds", &weather_style::fade_seconds, 0.0f, max_fade_seconds},
    {"overcast_sun", &weather_style::overcast_sun, 0.0f, 1.0f},
    {"overcast_shadows", &weather_style::overcast_shadows, 0.0f, 1.0f},
    {"overcast_scatter", &weather_style::overcast_scatter, 0.0f, 1.0f},
    {"overcast_grey", &weather_style::overcast_grey, 0.0f, 1.0f},
    {"overcast_sky_brightness", &weather_style::overcast_sky_brightness, 0.0f, 4.0f},
    {"overcast_sky_blend", &weather_style::overcast_sky_blend, 0.0f, 1.0f},
    {"overcast_mottle", &weather_style::overcast_mottle, 0.0f, 1.0f},
    {"overcast_glow", &weather_style::overcast_glow, 0.0f, 1.0f},
    {"overcast_haze", &weather_style::overcast_haze, 0.0f, 1.0f},
    {"overcast_haze_distance", &weather_style::overcast_haze_distance, 0.05f, 1.0f},
    {"overcast_exposure", &weather_style::overcast_exposure, 0.25f, 4.0f},
    {"overcast_saturation", &weather_style::overcast_saturation, 0.0f, 1.0f},
    {"rain_light", &weather_style::rain_light, 0.0f, 1.0f},
    {"rain_mottle", &weather_style::rain_mottle, 0.0f, 1.0f},
    {"rain_haze", &weather_style::rain_haze, 0.0f, 1.0f},
    {"rain_haze_distance", &weather_style::rain_haze_distance, 0.05f, 1.0f},
    {"rain_gain_noise", &weather_style::rain_gain_noise, 0.0f, 1.0f},
    {"fog_cover", &weather_style::fog_cover, 0.0f, 1.0f},
    {"fog_haze", &weather_style::fog_haze, 0.0f, 1.0f},
    {"fog_haze_distance", &weather_style::fog_haze_distance, 0.01f, 1.0f},
    {"fog_grey", &weather_style::fog_grey, 0.0f, 1.0f},
    {"wet_albedo", &weather_style::wet_albedo, 0.0f, 1.0f},
    {"wet_gloss", &weather_style::wet_gloss, 0.0f, 1.0f},
    {"wet_gloss_power", &weather_style::wet_gloss_power, 1.0f, max_gloss_power},
};

inline constexpr int max_weather_streaks = 20000;
inline constexpr float max_volume_size = 200.0f;

inline constexpr bounded_field<lightning_style> lightning_fields[] = {
    {"strikes_per_minute", &lightning_style::strikes_per_minute, 0.0f, 60.0f},
    {"flash_seconds", &lightning_style::flash_seconds, 0.05f, 3.0f},
    {"pulse_seconds", &lightning_style::pulse_seconds, 0.01f, 1.0f},
    {"bolt_share", &lightning_style::bolt_share, 0.0f, 1.0f},
    {"top_min_deg", &lightning_style::top_min_deg, 0.0f, 80.0f},
    {"top_max_deg", &lightning_style::top_max_deg, 0.0f, 80.0f},
    {"near_meters", &lightning_style::near_meters, 1.0f, 50000.0f},
    {"far_meters", &lightning_style::far_meters, 1.0f, 50000.0f},
    {"close_meters", &lightning_style::close_meters, 0.0f, 50000.0f},
    {"sound_speed", &lightning_style::sound_speed, 1.0f, 1000.0f},
    {"bolt_width_deg", &lightning_style::bolt_width_deg, 0.01f, 5.0f},
    {"bolt_jitter_deg", &lightning_style::bolt_jitter_deg, 0.0f, 20.0f},
    {"bolt_brightness", &lightning_style::bolt_brightness, 0.0f, 32.0f},
    {"glow_radius_deg", &lightning_style::glow_radius_deg, 0.1f, 180.0f},
    {"glow_strength", &lightning_style::glow_strength, 0.0f, 8.0f},
};
inline constexpr float max_fall_speed = 30.0f;

inline constexpr bounded_field<rain_style> rain_fields[] = {
    {"drop_power", &rain_style::drop_power, 0.25f, 4.0f},
    {"volume_size", &rain_style::volume_size, 1.0f, max_volume_size},
    {"light_fall_speed", &rain_style::light_fall_speed, 0.1f, max_fall_speed},
    {"heavy_fall_speed", &rain_style::heavy_fall_speed, 0.1f, max_fall_speed},
    {"wind_height", &rain_style::wind_height, 0.0f, 100.0f},
    {"streak_seconds", &rain_style::streak_seconds, 0.0f, 1.0f},
    {"light_alpha", &rain_style::light_alpha, 0.0f, 1.0f},
    {"heavy_alpha", &rain_style::heavy_alpha, 0.0f, 1.0f},
    {"near_fade", &rain_style::near_fade, 0.0f, max_volume_size},
};

inline constexpr float max_wind_speed = 60.0f;

inline constexpr bounded_field<wind_style> wind_fields[] = {
    {"flag_calm_speed", &wind_style::flag_calm_speed, 0.0f, max_wind_speed},
    {"flag_full_speed", &wind_style::flag_full_speed, 0.0f, max_wind_speed},
    {"flag_calm_hz", &wind_style::flag_calm_hz, 0.0f, max_frequency_hz},
    {"flag_full_hz", &wind_style::flag_full_hz, 0.0f, max_frequency_hz},
    {"flag_flap", &wind_style::flag_flap, 0.0f, 1.0f},
    {"speck_volume_size", &wind_style::speck_volume_size, 1.0f, max_volume_size},
    {"speck_height", &wind_style::speck_height, 0.0f, 100.0f},
    {"speck_full_speed", &wind_style::speck_full_speed, 0.1f, max_wind_speed},
    {"speck_calm_share", &wind_style::speck_calm_share, 0.0f, 1.0f},
    {"speck_seconds", &wind_style::speck_seconds, 0.0f, 1.0f},
    {"speck_tumble", &wind_style::speck_tumble, 0.0f, max_caster_metres},
};

inline constexpr bounded_field<night_light> light_fields[] = {
    {"intensity", &night_light::intensity, 0.0f, max_light_intensity},
    {"radius", &night_light::radius, 0.0f, max_light_radius},
};

inline constexpr share_field<glow_floors> glow_fields[] = {
    {"ball", &glow_floors::ball},
    {"cup", &glow_floors::cup},
    {"pin_pole", &glow_floors::pin_pole},
    {"flag", &glow_floors::flag},
    {"aim_dot", &glow_floors::aim_dot},
    {"ember", &glow_floors::ember},
    {"flight_path", &glow_floors::flight_path},
};

inline constexpr color_field<ground_colors> ground_fields[] = {
    {"fairway", &ground_colors::fairway},
    {"fairway_edge", &ground_colors::fairway_edge},
    {"rough", &ground_colors::rough},
    {"green", &ground_colors::green},
    {"bunker", &ground_colors::bunker},
    {"water", &ground_colors::water},
    {"backdrop", &ground_colors::backdrop},
    {"underwater", &ground_colors::underwater},
};

inline constexpr color_field<marker_colors> marker_fields[] = {
    {"ball", &marker_colors::ball},
    {"tee_grout", &marker_colors::tee_grout},
    {"tee_tile", &marker_colors::tee_tile},
    {"tee_mat", &marker_colors::tee_mat},
    {"collectible", &marker_colors::collectible},
    {"cup", &marker_colors::cup},
    {"pin_pole", &marker_colors::pin_pole},
    {"flag", &marker_colors::flag},
    {"aim_dot", &marker_colors::aim_dot},
    {"club_shaft", &marker_colors::club_shaft},
    {"club_face", &marker_colors::club_face},
    {"club_back", &marker_colors::club_back},
    {"club_sole", &marker_colors::club_sole},
};

inline constexpr color_field<figure_colors> figure_fields[] = {
    {"trousers", &figure_colors::trousers},
    {"skin", &figure_colors::skin},
    {"cap", &figure_colors::cap},
    {"shoes", &figure_colors::shoes},
    {"ball", &figure_colors::ball},
};

