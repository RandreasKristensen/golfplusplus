// The one tree pose in the scene and both shadow maps: world-fixed yaw
// and wind from the frame's clock, never the eye or the pass.
vec3 tree_world_position(vec3 local, vec3 scale, vec3 offset, float yaw, float seconds, vec2 wind) {
    float c=cos(yaw),s=sin(yaw);
    vec3 p=local*scale;
    vec3 world=offset+vec3(c*p.x-s*p.z,p.y,s*p.x+c*p.z);
    float gust=0.6+0.4*sin(seconds*1.3+offset.x*0.21+offset.z*0.17);
    world.xz+=wind*(0.004*scale.y*local.y*local.y*gust);
    return world;
}
