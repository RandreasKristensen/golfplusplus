// A tree's light, for its geometry (tree.frag) and its baked images
// (tree_image.frag): one path, so a far tree is lit as a near one is.
// Needs lighting.glsl.

// `color` is the surface's unlit colour, `kind` 1 on a leaf, 0.5 on a fine
// twig, 0 on bark, `depth` how deep in the crown a leaf or twig is (inner
// ones are shaded), `sun` the shadow map's share of the sun. Sun behind a
// leaf shines through it; twigs are lit as bark.
vec3 tree_lit(vec3 color, vec3 n, float kind, float depth, vec3 world, float sun, float shine_through) {
    float ao = kind > 0.25 ? mix(1.0, 0.45, depth) : 1.0;
    vec3 shine = vec3(0.0);
    if (kind > 0.75) {
        vec3 to_eye = normalize(haze_eye.xyz - world);
        float behind = max(dot(-to_eye, sun_direction.xyz), 0.0);
        shine = sun_color.rgb * color * (shine_through * behind * behind * sun * (1.0 - depth));
    }
    n = dot(n, sun_direction.xyz) < 0.0 && kind > 0.75 ? mix(n, -n, 0.3) : n;
    vec3 light = surface_light(n, ao, sun) + local_light(world, n);
    return hazed(color * light * wet.x + shine, world);
}
