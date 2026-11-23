// The view grid a tree's images are baked from (renderer/tree_impostor.h),
// in the grown model's unit frame: azimuths round it at each elevation, each
// view an orthographic picture along the way back to its eye, framed by the
// model's box. The bake and the drawn image read the grid through this one file.

uniform int u_azimuths;
uniform int u_elevations;
uniform vec4 u_azimuth_table[16];   // cos, sin
uniform vec4 u_elevation_table[8];  // cos, sin, radians
uniform vec3 u_box_centre;
uniform vec3 u_box_half;

// From the model towards the view's eye.
vec3 tree_grid_direction(int azimuth, int elevation) {
    vec4 a = u_azimuth_table[azimuth];
    vec4 e = u_elevation_table[elevation];
    return vec3(e.x * a.x, e.y, e.x * a.y);
}

void tree_view_axes(vec3 toward_eye, out vec3 right, out vec3 up) {
    vec3 across = cross(vec3(0.0, 1.0, 0.0), toward_eye);
    float length_across = length(across);
    right = length_across > 1.0e-4 ? across / length_across : vec3(1.0, 0.0, 0.0);
    up = cross(toward_eye, right);
}

// Half the model's box across, up and along the view: its picture's frame.
vec3 tree_view_reach(vec3 toward_eye, vec3 right, vec3 up) {
    return vec3(dot(abs(right), u_box_half), dot(abs(up), u_box_half), dot(abs(toward_eye), u_box_half)) * 1.002 + 1.0e-4;
}
