// A canonical mesh (a unit cylinder, sphere or quad) placed by one instance
// (renderer/mesh_instance.h, renderer/instance_meshes.h): the matrix's columns,
// colour and alpha, and the glow floor. Every program that draws instances
// includes this and reads the same attribute slots.

layout(location = 3) in vec3 a_axis_x;
layout(location = 4) in vec3 a_axis_y;
layout(location = 5) in vec3 a_axis_z;
layout(location = 6) in vec3 a_origin;
layout(location = 7) in vec4 a_tint;
layout(location = 8) in float a_glow;

// A mesh position in the world.
vec3 instance_world(vec3 local) {
    return a_axis_x * local.x + a_axis_y * local.y + a_axis_z * local.z + a_origin;
}

// A mesh normal in the world: turned by the matrix's inverse transpose (its
// columns' cross products over the determinant, whose sign keeps a mirrored
// matrix facing outward), normalized; up for a matrix with none.
vec3 instance_normal(vec3 local) {
    vec3 yz = cross(a_axis_y, a_axis_z);
    vec3 turned = local.x * yz + local.y * cross(a_axis_z, a_axis_x) + local.z * cross(a_axis_x, a_axis_y);
    turned *= dot(a_axis_x, yz) < 0.0 ? -1.0 : 1.0;
    float length_of = length(turned);
    return length_of > 0.0 ? turned / length_of : vec3(0.0, 1.0, 0.0);
}
