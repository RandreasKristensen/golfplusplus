#include "doctest.h"

#include "game/model_kinds.h"
#include "game/prop_loader.h"
#include "renderer/camera_local.h"
#include "renderer/prop_batch.h"
#include "renderer/prop_pieces.h"

#include "test_support.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

namespace {
// A model of the trinket kind (it needs only its hang point) with `rest`.
std::optional<prop_model> parse(const std::string& rest) {
    return parse_prop_from_text(R"({"kind": "trinket", "colors": {"paint": [0.5, 0.25, 1]}, "points": {"hang": [0, 0, 0]})" +
                                rest + "}");
}

// The catalog's own required models (tree, fence, floodlight, hole sign),
// plus `files`.
std::map<std::string, std::string> with_required(std::map<std::string, std::string> files) {
    files["tree"] = R"({"kind": "tree", "colors": {"trunk": [0, 0, 0], "leaves": [0, 1, 0]}})";
    files["fence"] = R"({"kind": "fence", "colors": {"post": [0, 0, 0]}})";
    files["floodlight"] = R"({"kind": "floodlight", "colors": {"pole": [0, 0, 0], "head": [0, 0, 0]}})";
    files["hole_sign"] = R"({"kind": "hole_sign", "colors": {"board": [0, 0, 0], "post": [0, 0, 0]}})";
    return files;
}

const std::string cigarette = R"({"kind": "cigarette",
    "colors": {"paper": [1, 1, 1], "ember_dim": [1, 0, 0], "ember_bright": [1, 0.5, 0], "smoke": [0.8, 0.8, 0.8]},
    "parts": [{"shape": "cylinder", "from": [0, 0, 0], "to": [0, 0, 1], "radius": 0.05, "color": "paper"},
              {"shape": "sphere", "at": [0, 0, 1], "radius": 0.06, "color": "ember_dim", "tag": "ember"}],
    "points": {"smoke": [0, 0, 1]}})";
}

TEST_CASE("every shipped model loads by its file name, of its kind; a missing required one fails, naming its file") {
    const prop_catalog_load_result shipped = load_props(asset_root());
    REQUIRE(shipped.props.has_value());
    CHECK(shipped.error.empty());
    for (const required_model& required : required_models()) {
        REQUIRE(shipped.props->count(required.id) == 1U);
        CHECK(shipped.props->at(required.id).kind == required.kind);
    }
    CHECK(!shipped.props->at("cigarette").parts.empty());
    CHECK(shipped.props->at("golf_cart").kind == "cart");

    const prop_catalog_load_result missing = load_props(fixture_root());
    CHECK(!missing.props.has_value());
    CHECK(missing.error.find(props_path) != std::string::npos);

    std::map<std::string, std::string> no_tree = with_required({});
    no_tree.erase("tree");
    const prop_catalog_load_result without = parse_prop_catalog(no_tree);
    CHECK(!without.props.has_value());
    CHECK(without.error.find("tree") != std::string::npos);
}

TEST_CASE("a model's kind says what it must have: a cigarette without an ember is refused") {
    CHECK(parse_prop_from_text(cigarette).has_value());
    std::string no_ember = cigarette;
    no_ember.replace(no_ember.find(R"(, "tag": "ember")"), std::string(R"(, "tag": "ember")").size(), "");
    CHECK(!parse_prop_from_text(no_ember).has_value());
    std::string no_smoke = cigarette;
    no_smoke.replace(no_smoke.find(R"("points": {"smoke": [0, 0, 1]})"), std::string(R"("points": {"smoke": [0, 0, 1]})").size(),
                     R"("points": {})");
    CHECK(!parse_prop_from_text(no_smoke).has_value());
    // A cart needs its wheels' and steering wheel's points and its bag mounts.
    CHECK(!parse_prop_from_text(R"({"kind": "cart", "colors": {"trim": [0, 0, 0], "rubber": [0, 0, 0], "hub_cap": [0, 0, 0]}})")
               .has_value());
    // An unknown kind, or none.
    CHECK(!parse_prop_from_text(R"({"kind": "spaceship", "colors": {}})").has_value());
    CHECK(!parse_prop_from_text(R"({"colors": {"paint": [0, 0, 0]}})").has_value());
}

TEST_CASE("a model file names its colours, and its parts use them") {
    const std::optional<prop_model> model = parse(R"(, "parts": [
        {"shape": "cylinder", "from": [0, 0, 0], "to": [0, 1, 0], "radius": 0.5, "color": "paint"},
        {"shape": "sphere", "at": [0, 2, 0], "radius": 0.25, "color": "paint", "glow": "ember", "tag": "ember"},
        {"shape": "box", "at": [1, 0, 0], "size": [1, 2, 3], "turn": [0, 90, 0], "color": "paint"},
        {"shape": "panel", "at": [0, 0, -1], "size": [0.5, 0.25], "color": "paint"}])");
    REQUIRE(model.has_value());
    REQUIRE(model->parts.size() == 4U);
    CHECK(model->parts[0].color == glm::vec3(0.5f, 0.25f, 1.0f));
    CHECK(model->parts[1].glow == "ember");
    CHECK(model->parts[1].tag == "ember");
    CHECK(model->parts[2].turn_degrees == glm::vec3(0.0f, 90.0f, 0.0f));
    CHECK(model->parts[3].size == glm::vec3(0.5f, 0.25f, 0.0f));
    CHECK(model->point("hang") == glm::vec3(0.0f));

    // Colours only, besides what the kind needs.
    CHECK(parse("").has_value());
    // Colours are three numbers in 0..1.
    CHECK(!parse_prop_from_text(R"({"kind": "fence", "colors": {"post": [1.5, 0, 0]}})").has_value());
    CHECK(!parse_prop_from_text(R"({"kind": "fence", "parts": []})").has_value());
    // A part's shape, colour, sizes and glow are checked.
    CHECK(!parse(R"(, "parts": {"shape": "sphere"})").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "cone", "at": [0, 0, 0], "radius": 1, "color": "paint"}]})").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "sphere", "at": [0, 0, 0], "radius": 1, "color": "rust"}])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "sphere", "at": [0, 0, 0], "radius": 0, "color": "paint"}])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "sphere", "at": [0, 0, 0], "radius": 1, "color": "paint", "glow": "neon"}])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "cylinder", "from": [0, 0, 0], "to": [0, 0, 0], "radius": 1, "color": "paint"}])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "box", "at": [0, 0, 0], "size": [1, 0, 1], "color": "paint"}])").has_value());
    CHECK(!parse(R"(, "parts": [{"shape": "panel", "at": [0, 0, 0], "size": [1, 1, 1], "color": "paint"}])").has_value());
    // A mount's sides are one long and square to each other.
    CHECK(parse(R"(, "mounts": {"top": {"at": [0, 1, 0], "forward": [0, 0, 1], "up": [0, 1, 0]}})").has_value());
    CHECK(!parse(R"(, "mounts": {"top": {"at": [0, 1, 0], "forward": [0, 0, 2], "up": [0, 1, 0]}})").has_value());
    CHECK(!parse(R"(, "mounts": {"top": {"at": [0, 1, 0], "forward": [0, 1, 0], "up": [0, 1, 0]}})").has_value());
    // A file alone never names a base: only the catalog resolves one.
    CHECK(!parse(R"(, "base": "other")").has_value());
}

TEST_CASE("a variant overrides only what it names of its base, through as many levels as named") {
    const prop_catalog_load_result catalog = parse_prop_catalog(with_required({
        {"cigarette", cigarette},
        {"gold", R"({"base": "cigarette", "colors": {"paper": [0.9, 0.7, 0.2]}})"},
        {"gold_long", R"({"base": "gold", "points": {"smoke": [0, 0, 1.4]},
            "parts": [{"shape": "cylinder", "from": [0, 0, 0], "to": [0, 0, 1.4], "radius": 0.05, "color": "paper"},
                      {"shape": "sphere", "at": [0, 0, 1.4], "radius": 0.06, "color": "ember_dim", "tag": "ember"}]})"},
    }));
    REQUIRE(catalog.props.has_value());
    const prop_model& base = catalog.props->at("cigarette");
    const prop_model& gold = catalog.props->at("gold");
    CHECK(gold.kind == "cigarette");
    // The colour it names, and the parts that use it; all else as the base.
    CHECK(gold.color("paper") == glm::vec3(0.9f, 0.7f, 0.2f));
    CHECK(gold.parts[0].color == glm::vec3(0.9f, 0.7f, 0.2f));
    CHECK(gold.color("smoke") == base.color("smoke"));
    CHECK(gold.parts[1].color == base.parts[1].color);
    CHECK(gold.point("smoke") == base.point("smoke"));
    // Two levels down: its own parts and point, its base's colours.
    const prop_model& long_gold = catalog.props->at("gold_long");
    CHECK(long_gold.point("smoke") == glm::vec3(0.0f, 0.0f, 1.4f));
    CHECK(long_gold.parts[0].to == glm::vec3(0.0f, 0.0f, 1.4f));
    CHECK(long_gold.color("paper") == gold.color("paper"));

    // A cycle, a missing base and a base of another kind are refused.
    CHECK(!parse_prop_catalog(with_required({{"a", R"({"base": "b"})"}, {"b", R"({"base": "a"})"}})).props.has_value());
    CHECK(!parse_prop_catalog(with_required({{"a", R"({"base": "nothing"})"}})).props.has_value());
    CHECK(!parse_prop_catalog(with_required({{"cigarette", cigarette}, {"a", R"({"base": "cigarette", "kind": "can"})"}}))
               .props.has_value());
}

TEST_CASE("a model hangs at another's mount by its hang point, its sides along the mount's") {
    const std::optional<prop_model> holder =
        parse(R"(, "mounts": {"side": {"at": [1, 2, 0], "forward": [1, 0, 0], "up": [0, 1, 0]}})");
    const std::optional<prop_model> hung = parse_prop_from_text(
        R"({"kind": "trinket", "colors": {"paint": [1, 1, 1]}, "points": {"hang": [0, 0.5, 0], "tip": [0, 0.5, 1]}})");
    REQUIRE(holder.has_value());
    REQUIRE(hung.has_value());
    const glm::mat4 frame = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    const glm::mat4 placed = mounted_frame(frame, holder->mount("side"), *hung);
    // Its hang point is at the mount, and its forward point along the mount's forward.
    CHECK(near(glm::vec3(placed * glm::vec4(hung->point("hang"), 1.0f)), glm::vec3(11.0f, 2.0f, 0.0f)));
    CHECK(near(glm::vec3(placed * glm::vec4(hung->point("tip"), 1.0f)), glm::vec3(12.0f, 2.0f, 0.0f)));
}

TEST_CASE("a model's parts become unit meshes in its frame, a box its six faces") {
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

    // Drawn, every vertex carries a normal pointing out of its piece.
    prop_batch batch;
    append_prop_model(batch, *model, frame, glow_floors{});
    CHECK(batch.opaque().size() == prop_model_vertex_count(*model));
    CHECK(batch.blended().empty());
    std::size_t side = 0;
    for (std::size_t i = 0; i < prop_mesh_vertex_count(prop_mesh::cylinder); ++i) {
        const prop_vertex& vertex = batch.opaque()[i];
        const glm::vec3 from_axis(vertex.position.x - 10.0f, 0.0f, vertex.position.z);
        if (std::abs(vertex.normal.y) > 0.99f) {
            // A cap: facing out of its end.
            CHECK(vertex.normal.y * (vertex.position.y - 2.0f) > 0.0f);
        } else {
            ++side;
            CHECK(glm::dot(vertex.normal, glm::normalize(from_axis)) > 0.9f);
        }
    }
    CHECK(side > 0U);
}
