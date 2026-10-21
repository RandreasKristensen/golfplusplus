#include "doctest.h"

#include "game/prop_loader.h"
#include "renderer/prop_pieces.h"

#include "test_support.h"

#include <optional>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

namespace {
const std::string colors = R"("colors": {"paint": [0.5, 0.25, 1]})";

std::optional<prop_model> parse(const std::string& rest, const std::vector<std::string>& needed_colors = {},
                                const std::vector<std::string>& needed_points = {}) {
    return parse_prop_from_text("{" + colors + rest + "}", needed_colors, needed_points);
}
}

TEST_CASE("every shipped prop loads; a missing one fails, naming its file") {
    const prop_set_load_result shipped = load_props(asset_root());
    REQUIRE(shipped.props.has_value());
    CHECK(shipped.error.empty());
    CHECK(!shipped.props->cigarette.parts.empty());
    CHECK(!shipped.props->beer_can.parts.empty());
    CHECK(!shipped.props->golf_cart.parts.empty());

    const prop_set_load_result missing = load_props(fixture_root());
    CHECK(!missing.props.has_value());
    CHECK(missing.error.find(props_path) != std::string::npos);
}

TEST_CASE("a prop file names its colours, and its parts use them") {
    const std::optional<prop_model> model = parse(R"(, "parts": [
        {"shape": "cylinder", "from": [0, 0, 0], "to": [0, 1, 0], "radius": 0.5, "color": "paint"},
        {"shape": "sphere", "at": [0, 2, 0], "radius": 0.25, "color": "paint", "glow": "ember", "tag": "ember"},
        {"shape": "box", "at": [1, 0, 0], "size": [1, 2, 3], "turn": [0, 90, 0], "color": "paint"},
        {"shape": "panel", "at": [0, 0, -1], "size": [0.5, 0.25], "color": "paint"}],
        "points": {"smoke": [0, 3, 0]})",
                                                  {"paint"}, {"smoke"});
    REQUIRE(model.has_value());
    REQUIRE(model->parts.size() == 4U);
    CHECK(model->parts[0].color == glm::vec3(0.5f, 0.25f, 1.0f));
    CHECK(model->parts[1].glow == "ember");
    CHECK(model->parts[1].tag == "ember");
    CHECK(model->parts[2].turn_degrees == glm::vec3(0.0f, 90.0f, 0.0f));
    CHECK(model->parts[3].size == glm::vec3(0.5f, 0.25f, 0.0f));
    CHECK(model->point("smoke") == glm::vec3(0.0f, 3.0f, 0.0f));

    // Colours only (a prop the code fits to the course).
    CHECK(parse("").has_value());
    // What the code asks for must be there.
    CHECK(!parse("", {"trim"}).has_value());
    CHECK(!parse("", {}, {"smoke"}).has_value());
    // Colours are three numbers in 0..1.
    CHECK(!parse_prop_from_text(R"({"colors": {"paint": [1.5, 0, 0]}})", {}, {}).has_value());
    CHECK(!parse_prop_from_text(R"({"parts": []})", {}, {}).has_value());
    // A part's shape, colour, sizes and glow are checked.
    CHECK(!parse(R"(, "parts": {"shape": "sphere"})").has_value());
    CHECK(!parse(R"(, "points": [])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "cone", "at": [0, 0, 0], "radius": 1, "color": "paint"}]})").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "sphere", "at": [0, 0, 0], "radius": 1, "color": "rust"}])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "sphere", "at": [0, 0, 0], "radius": 0, "color": "paint"}])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "sphere", "at": [0, 0, 0], "radius": 1, "color": "paint", "glow": "neon"}])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "cylinder", "from": [0, 0, 0], "to": [0, 0, 0], "radius": 1, "color": "paint"}])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "box", "at": [0, 0, 0], "size": [1, 0, 1], "color": "paint"}])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "panel", "at": [0, 0, 0], "size": [1, 1, 1], "color": "paint"}])").has_value());
}

TEST_CASE("a prop's parts become unit meshes in its frame, a box its six faces") {
    const std::optional<prop_model> model = parse(R"(, "parts": [
        {"shape": "cylinder", "from": [0, 1, 0], "to": [0, 3, 0], "radius": 0.5, "color": "paint"},
        {"shape": "box", "at": [0, 0, 0], "size": [2, 4, 6], "color": "paint"},
        {"shape": "sphere", "at": [0, 0, 2], "radius": 0.25, "color": "paint", "tag": "ember"}])");
    REQUIRE(model.has_value());
    const glm::mat4 frame = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    const std::vector<prop_piece> pieces = prop_pieces(*model, frame);
    REQUIRE(pieces.size() == 8U);
    CHECK(prop_piece_count(*model, prop_mesh::cylinder) == 1U);
    CHECK(prop_piece_count(*model, prop_mesh::quad) == 6U);
    CHECK(prop_piece_count(*model, prop_mesh::sphere) == 1U);

    // The cylinder runs from its start to its end in the frame.
    CHECK(pieces[0].mesh == prop_mesh::cylinder);
    CHECK(near(glm::vec3(pieces[0].model * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)), glm::vec3(10.0f, 1.0f, 0.0f)));
    CHECK(near(glm::vec3(pieces[0].model * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f)), glm::vec3(10.0f, 3.0f, 0.0f)));
    // Each face of the box sits on its side, half its size out.
    glm::vec3 reach(0.0f);
    for (std::size_t i = 1; i <= 6; ++i) {
        CHECK(pieces[i].mesh == prop_mesh::quad);
        reach = glm::max(reach, glm::abs(glm::vec3(pieces[i].model[3]) - glm::vec3(10.0f, 0.0f, 0.0f)));
    }
    CHECK(near(reach, glm::vec3(1.0f, 2.0f, 3.0f)));
    CHECK(pieces[7].part->tag == "ember");
    CHECK(near(glm::vec3(pieces[7].model[3]), glm::vec3(10.0f, 0.0f, 2.0f)));
}
