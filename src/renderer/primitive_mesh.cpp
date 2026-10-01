#include "renderer/primitive_mesh.h"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstddef>

namespace {

void append_mesh_vertex(std::vector<float>& vertices, const glm::vec3 position, const glm::vec3 normal) {
    vertices.insert(vertices.end(), {
        position.x, position.y, position.z,
        normal.x, normal.y, normal.z
    });
}

void append_sphere_vertex(std::vector<float>& vertices, const glm::vec3 normal) {
    constexpr float radius = 1.0f;
    append_mesh_vertex(vertices, normal * radius, normal);
}
}

std::vector<float> make_sphere_vertices(const int latitude_segments, const int longitude_segments) {
    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(latitude_segments) *
                     static_cast<std::size_t>(longitude_segments) * 36);

    for (int lat = 0; lat < latitude_segments; ++lat) {
        const float theta0 = glm::pi<float>() * static_cast<float>(lat) / static_cast<float>(latitude_segments);
        const float theta1 = glm::pi<float>() * static_cast<float>(lat + 1) / static_cast<float>(latitude_segments);

        for (int lon = 0; lon < longitude_segments; ++lon) {
            const float phi0 = 2.0f * glm::pi<float>() * static_cast<float>(lon) / static_cast<float>(longitude_segments);
            const float phi1 = 2.0f * glm::pi<float>() * static_cast<float>(lon + 1) / static_cast<float>(longitude_segments);

            const glm::vec3 p00(std::sin(theta0) * std::cos(phi0), std::cos(theta0), std::sin(theta0) * std::sin(phi0));
            const glm::vec3 p01(std::sin(theta0) * std::cos(phi1), std::cos(theta0), std::sin(theta0) * std::sin(phi1));
            const glm::vec3 p10(std::sin(theta1) * std::cos(phi0), std::cos(theta1), std::sin(theta1) * std::sin(phi0));
            const glm::vec3 p11(std::sin(theta1) * std::cos(phi1), std::cos(theta1), std::sin(theta1) * std::sin(phi1));

            append_sphere_vertex(vertices, p00);
            append_sphere_vertex(vertices, p10);
            append_sphere_vertex(vertices, p11);

            append_sphere_vertex(vertices, p00);
            append_sphere_vertex(vertices, p11);
            append_sphere_vertex(vertices, p01);
        }
    }

    return vertices;
}

std::vector<float> make_cylinder_vertices(const int segments) {
    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(segments) * 72);

    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * glm::pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * glm::pi<float>() * static_cast<float>(i + 1) / static_cast<float>(segments);
        const glm::vec3 n0(std::cos(a0), 0.0f, std::sin(a0));
        const glm::vec3 n1(std::cos(a1), 0.0f, std::sin(a1));
        const glm::vec3 p00(n0.x, 0.0f, n0.z);
        const glm::vec3 p01(n1.x, 0.0f, n1.z);
        const glm::vec3 p10(n0.x, 1.0f, n0.z);
        const glm::vec3 p11(n1.x, 1.0f, n1.z);

        append_mesh_vertex(vertices, p00, n0);
        append_mesh_vertex(vertices, p01, n1);
        append_mesh_vertex(vertices, p11, n1);
        append_mesh_vertex(vertices, p00, n0);
        append_mesh_vertex(vertices, p11, n1);
        append_mesh_vertex(vertices, p10, n0);

        append_mesh_vertex(vertices, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
        append_mesh_vertex(vertices, p01, glm::vec3(0.0f, -1.0f, 0.0f));
        append_mesh_vertex(vertices, p00, glm::vec3(0.0f, -1.0f, 0.0f));

        append_mesh_vertex(vertices, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        append_mesh_vertex(vertices, p10, glm::vec3(0.0f, 1.0f, 0.0f));
        append_mesh_vertex(vertices, p11, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    return vertices;
}

std::vector<float> make_cone_vertices(const int segments) {
    std::vector<float> vertices;
    vertices.reserve(static_cast<std::size_t>(segments) * 54);

    const glm::vec3 tip(0.0f, 1.0f, 0.0f);
    for (int i = 0; i < segments; ++i) {
        const float a0 = 2.0f * glm::pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = 2.0f * glm::pi<float>() * static_cast<float>(i + 1) / static_cast<float>(segments);
        const glm::vec3 p0(std::cos(a0), 0.0f, std::sin(a0));
        const glm::vec3 p1(std::cos(a1), 0.0f, std::sin(a1));
        const glm::vec3 face_normal = glm::normalize(glm::cross(p1 - p0, tip - p0));

        append_mesh_vertex(vertices, p0, face_normal);
        append_mesh_vertex(vertices, p1, face_normal);
        append_mesh_vertex(vertices, tip, face_normal);

        append_mesh_vertex(vertices, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
        append_mesh_vertex(vertices, p0, glm::vec3(0.0f, -1.0f, 0.0f));
        append_mesh_vertex(vertices, p1, glm::vec3(0.0f, -1.0f, 0.0f));
    }

    return vertices;
}

std::vector<float> make_xy_quad_vertices() {
    std::vector<float> vertices;
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);
    for (const glm::vec3& corner : {glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
                                    glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 1.0f, 0.0f)}) {
        append_mesh_vertex(vertices, corner, normal);
    }
    return vertices;
}

std::vector<float> make_xz_quad_vertices() {
    std::vector<float> vertices;
    const glm::vec3 normal(0.0f, 1.0f, 0.0f);
    for (const glm::vec3& corner : {glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(-1.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f),
                                    glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, -1.0f)}) {
        append_mesh_vertex(vertices, corner, normal);
    }
    return vertices;
}

std::vector<glm::vec3> mesh_positions_of(const std::vector<float>& interleaved) {
    std::vector<glm::vec3> positions;
    positions.reserve(interleaved.size() / 6);
    for (std::size_t i = 0; i + 5 < interleaved.size(); i += 6) {
        positions.emplace_back(interleaved[i], interleaved[i + 1], interleaved[i + 2]);
    }
    return positions;
}

std::vector<glm::vec3> make_cylinder_positions(const int segments) {
    return mesh_positions_of(make_cylinder_vertices(segments));
}

std::vector<glm::vec3> make_sphere_positions(const int latitude_segments, const int longitude_segments) {
    return mesh_positions_of(make_sphere_vertices(latitude_segments, longitude_segments));
}
