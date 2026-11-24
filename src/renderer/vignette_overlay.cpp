#include "renderer/vignette_overlay.h"

namespace {
void push(overlay_batch& batch, const glm::vec2 position, const glm::vec4 colour) {
    batch.vertices.push_back(overlay_vertex{position, colour});
}

// A strip from the outer edge a-b to the inner edge c-d, clear at the inner.
void strip(overlay_batch& batch, const glm::vec2 a, const glm::vec2 b, const glm::vec2 c, const glm::vec2 d,
           const glm::vec3 colour, const float alpha) {
    const glm::vec4 edge(colour, alpha);
    const glm::vec4 clear(colour, 0.0f);
    push(batch, a, edge);
    push(batch, b, edge);
    push(batch, c, clear);
    push(batch, b, edge);
    push(batch, d, clear);
    push(batch, c, clear);
}
}

void draw_vignette(overlay_batch& batch, const glm::vec3 colour, const float strength) {
    if (!(strength > 0.0f)) {
        return;
    }
    const float alpha = strength * vignette_edge_alpha;
    const float in = 1.0f - vignette_inset;
    const glm::vec2 outer_bl(-1.0f, -1.0f), outer_br(1.0f, -1.0f), outer_tr(1.0f, 1.0f), outer_tl(-1.0f, 1.0f);
    const glm::vec2 inner_bl(-in, -in), inner_br(in, -in), inner_tr(in, in), inner_tl(-in, in);
    strip(batch, outer_bl, outer_br, inner_bl, inner_br, colour, alpha);  // bottom
    strip(batch, outer_br, outer_tr, inner_br, inner_tr, colour, alpha);  // right
    strip(batch, outer_tr, outer_tl, inner_tr, inner_tl, colour, alpha);  // top
    strip(batch, outer_tl, outer_bl, inner_tl, inner_bl, colour, alpha);  // left
}
