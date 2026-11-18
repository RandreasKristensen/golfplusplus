// The visible leaves and their shadow maps use the same cutouts. Needs noise.glsl.
bool tree_leaf_present(vec2 uv, float tint, float foliage) {
    vec2 grid = uv * 5.0;
    vec2 cell = floor(grid) + tint * 37.0;
    if (hash12(cell) >= foliage) {
        return false;
    }
    float angle = 6.2831853 * hash12(cell + 3.1);
    vec2 f = fract(grid) - 0.5;
    vec2 r = vec2(cos(angle) * f.x + sin(angle) * f.y, -sin(angle) * f.x + cos(angle) * f.y) / vec2(0.5, 0.36);
    if (dot(r, r) > 1.0) {
        return false;
    }
    return true;
}
