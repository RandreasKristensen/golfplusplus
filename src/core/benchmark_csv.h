#pragma once

// Raw CPU and delayed GPU data share the originating frame's phase and view.
#include "core/benchmark_scenarios.h"
#include "profiling/profiling.h"
#include <ostream>

void benchmark_csv_header(std::ostream& out);
void benchmark_csv_frame(std::ostream& out, const benchmark_scenario& scenario, const char* phase,
                         const frame_profile& frame);
