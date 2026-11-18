#pragma once

// Grass roots inspected through ground-level views along real fairways.
// Ring colours are diagnostic; root positions are checked against the world.
#include "grass_check.h"
grass_check_result check_grass_ground_views(const check_course& course,const lighting_palette& palette,
                                             const std::string& root,const std::filesystem::path& out);
