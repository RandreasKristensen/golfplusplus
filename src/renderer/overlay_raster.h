#pragma once

// Paints an overlay batch into an image on the CPU, for pictures drawn once
// and then used as a texture (the hole signs' faces). The batch is laid out
// on its grid like any overlay, so pixel text lands on whole texels. GL-free.

#include <glm/vec3.hpp>

#include "renderer/bmp_image.h"
#include "renderer/overlay_batch.h"

// An opaque image the size of `batch.grid`, filled with `background`, with
// every triangle blended over it in order (source alpha over, like the
// overlay pass). A triangle paints the texels whose centres it covers, with
// the top-left rule on its edges, so two triangles sharing an edge never
// both paint a texel. Row 0 is the bottom (clip y = -1), as in rgba_image.
rgba_image rasterize_overlay_batch(const overlay_batch& batch, glm::vec3 background);
