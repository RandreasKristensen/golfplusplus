#pragma once

// GOLFPP_SCREENSHOT (core/startup_options.h): saves what the window shows
// as a 24-bit BMP, for reviewing the picture.

#include <SDL.h>

#include <filesystem>

// Reads the window's back buffer, so call it after the frame is drawn and
// before the swap. False when the file cannot be written.
bool save_screenshot(SDL_Window* window, const std::filesystem::path& path);
