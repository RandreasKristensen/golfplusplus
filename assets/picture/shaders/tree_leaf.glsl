// A clump card's texel (renderer/tree_model.h): a leaf, a withered leaf, a
// twig of the card's spray, or nothing. The picture, the depth-first pass, the
// bake and both shadow maps cut the cards by this one function, so a crown's
// shadow is the crown drawn. Needs noise.glsl.

const int card_empty = 0;
const int card_leaf = 1;
const int card_withered = 2;
const int card_twig = 3;

// Twigs begin this share of a card's length from its foot (tree_model.cpp's card_foot).
const float card_foot = 0.2;
// Round each twig, the countless finer ones no stroke draws: twigs.w of the
// texels within haze_reach card widths of it, thinning outward.
const float haze_reach = 0.06;
// The thinnest a twig is drawn, in pixels; finer ones are thinned in proportion instead.
const float twig_min_pixels = 1.5;

// A card's point in card widths: across its middle, and along it from its foot.
vec2 tree_card_point(vec2 uv, float stretch) {
    return vec2(uv.x - 0.5, uv.y * stretch);
}

// How many card widths a pixel spans here. Taken before any branch, so the
// derivatives are defined.
float tree_card_pixel(vec2 uv, float stretch) {
    vec2 q = tree_card_point(uv, stretch);
    return max(length(dFdx(q)), length(dFdy(q)));
}

// The size of a threshold's cell, in pixels: coarser cells change less as the eye moves.
const float threshold_pixels = 2.0;

// A 0..1 threshold fixed on the card at about threshold_pixels' size, blended between
// the two nearest power-of-two scales so it neither crawls nor pops as the
// eye moves, and still uniform (Wyman and McGuire 2017, hashed alpha testing).
float tree_hashed_threshold(vec2 q, float pixel, float seed) {
    float level = log2(1.0 / max(threshold_pixels * pixel, 1.0e-6));
    vec2 scales = vec2(exp2(floor(level)), exp2(floor(level) + 1.0));
    vec2 offset = vec2(seed * 61.0, seed * 17.0);
    float x = mix(hash12(floor(q * scales.x) + offset), hash12(floor(q * scales.y) + offset), fract(level));
    float f = fract(level);
    float a = max(min(f, 1.0 - f), 1.0e-4);
    float t = x < 1.0 - a ? (x < a ? x * x / (2.0 * a * (1.0 - a)) : (x - 0.5 * a) / (1.0 - a))
                          : 1.0 - (1.0 - x) * (1.0 - x) / (2.0 * a * (1.0 - a));
    return clamp(t, 1.0e-4, 1.0);
}

// The nearest stroke so far: x its squared distance over its drawn half
// width (inside at most 1), y the share of that drawn width it really covers,
// z the squared distance to the nearest stroke's middle in card widths.
void tree_twig_stroke(vec2 q, vec2 a, vec2 b, float wa, float wb, float floor_width, inout vec3 nearest) {
    vec2 ab = b - a;
    float t = clamp(dot(q - a, ab) / max(dot(ab, ab), 1.0e-8), 0.0, 1.0);
    float half_width = mix(wa, wb, t);
    float drawn = max(half_width, floor_width);
    vec2 off = q - a - ab * t;
    float away = dot(off, off);
    float inside = away / (drawn * drawn);
    nearest.xy = inside < nearest.x ? vec2(inside, half_width / drawn) : nearest.xy;
    nearest.z = min(nearest.z, away);
}

// A twig from a to b, kinked once at its middle by `kink` to one `side`.
void tree_twig(vec2 q, vec2 a, vec2 b, float wa, float wb, float kink, float side, float floor_width, inout vec3 nearest) {
    vec2 ab = b - a;
    vec2 middle = (a + b) * 0.5 + vec2(-ab.y, ab.x) * (0.12 * kink * side);
    float wm = 0.5 * (wa + wb);
    tree_twig_stroke(q, a, middle, wa, wm, floor_width, nearest);
    tree_twig_stroke(q, middle, b, wm, wb, floor_width, nearest);
}

// `v` turned by the angle whose cosine and sine are `turn`, to `side` (1 or -1).
vec2 tree_turned(vec2 v, vec2 turn, float side) {
    return vec2(turn.x * v.x - side * turn.y * v.y, side * turn.y * v.x + turn.x * v.y);
}

// The card's spray of fine twigs: a leading twig from the branch's end along
// the card, four side twigs alternating along it, each with two of its own,
// thinner and shorter outward. `twigs`: x how much they zig-zag (oak, beech),
// y how much they hang as long whips close to the leader (birch, willow), z the
// card's length over its width, w the haze of the finest twigs round them. True where a twig covers the texel.
bool tree_twig_present(vec2 q, float seed, vec4 twigs, float pixel) {
    vec2 foot = vec2(0.0, card_foot * twigs.z);
    float reach = (0.95 - card_foot) * twigs.z;
    // Nothing behind the branch's end or beyond the spray's reach: most of a bare card, at once.
    if (q.y < foot.y - haze_reach || length(q - foot - vec2(0.0, 0.5 * reach)) > 0.75 * reach + haze_reach) {
        return false;
    }
    float floor_width = 0.5 * twig_min_pixels * pixel;
    // The card's own numbers, all from one hash.
    float h = hash12(vec2(seed * 97.0, 1.3));
    vec2 tip = foot + vec2((h - 0.5) * 0.15, reach);
    float flip = fract(h * 7.13) < 0.5 ? 1.0 : -1.0;
    vec3 nearest = vec3(2.0, 0.0, 1.0e3);
    tree_twig(q, foot, tip, 0.012, 0.004, twigs.x, flip, floor_width, nearest);
    vec2 leader = normalize(tip - foot);
    float spread = mix(0.8, 0.22, twigs.y);
    vec2 turn = vec2(cos(spread), sin(spread));
    vec2 sub_turn = vec2(cos(0.8 * spread), sin(0.8 * spread));
    for (int i = 0; i < 4; ++i) {
        float side = (i % 2 == 0 ? 1.0 : -1.0) * flip;
        float at = 0.15 + 0.19 * float(i) + 0.06 * fract(h * (13.7 + 5.3 * float(i)));
        vec2 base = mix(foot, tip, at);
        float length_out = (1.0 - at) * reach * mix(0.6, 0.95, twigs.y);
        vec2 way = tree_turned(leader, turn, side);
        vec2 end = base + way * length_out;
        // The side twig with its own two reach no further than this from its middle.
        if (length(q - 0.5 * (base + end)) > 0.6 * length_out + haze_reach + 0.02) {
            continue;
        }
        tree_twig(q, base, end, 0.007, 0.003, twigs.x, -side, floor_width, nearest);
        for (int j = 0; j < 2; ++j) {
            vec2 from = mix(base, end, 0.35 + 0.3 * float(j));
            float sub = (j == 0 ? -1.0 : 1.0) * side;
            vec2 to = from + tree_turned(way, sub_turn, sub) * (length_out * (0.45 - 0.1 * float(j)));
            tree_twig_stroke(q, from, to, 0.004, 0.002, floor_width, nearest);
        }
    }
    // The stroke's share of the texel, and the finer twigs' haze in the rest of it: the
    // same expected coverage at any pixel size.
    float stroke = nearest.x <= 1.0 ? nearest.y : 0.0;
    float cover = stroke + (1.0 - stroke) * twigs.w * max(1.0 - sqrt(nearest.z) / haze_reach, 0.0);
    return cover > 0.0 && cover >= tree_hashed_threshold(q, pixel, seed);
}

// The leaves are a 5 x 5 grid of ellipses at their own angles: a cell is in
// leaf below season.x (the crown's share in leaf) and withered up to that plus
// season.y (the crown's withered share) times twice the card's `hold`. Between
// and under them, the twigs. `shade` is the leaf's own 0..1 for its colour.
int tree_card_texel(vec2 uv, float tint, float seed, float hold, vec2 season, vec4 twigs, float pixel, out float shade) {
    vec2 grid = uv * vec2(5.0);
    vec2 cell = floor(grid) + tint * 37.0 + seed * 53.0;
    float h = hash12(cell);
    shade = hash12(cell + 7.7);
    if (h < season.x + season.y * 2.0 * hold) {
        float angle = 6.2831853 * hash12(cell + 3.1);
        vec2 f = fract(grid) - 0.5;
        vec2 r = vec2(cos(angle) * f.x + sin(angle) * f.y, -sin(angle) * f.x + cos(angle) * f.y) / vec2(0.5, 0.36);
        if (dot(r, r) <= 1.0) {
            return h < season.x ? card_leaf : card_withered;
        }
    }
    shade = 0.5;
    return tree_twig_present(tree_card_point(uv, twigs.z), seed, twigs, pixel) ? card_twig : card_empty;
}
