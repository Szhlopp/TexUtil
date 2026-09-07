#include "texutil/texutil.hpp"
#include <cmath>
#include <stdexcept>

namespace tex {
Json presetNames() { return {"bnw_spots", "gaussian_spots", "dirt", "grunge", "grunge_rust", "grunge_leaks", "scratches", "fibers", "fur", "shavings", "brick", "weave", "ornament", "creased", "crystal", "plasma", "fluid", "liquid"}; }
Json expandPresets(Json nodes) {
    Json original = nodes; size_t serial = 0;
    for (auto it = original.begin(); it != original.end(); ++it) {
        if (!it.value().is_object() || it.value().value("op", Json()) != "preset") continue;
        auto n = normalizedNode(it.key(), it.value()); const auto& config = n["config"]; std::string name = n["name"], prefix = "preset '" + it.key() + "': ";
        for (auto p = config.begin(); p != config.end(); ++p) {
            std::string key = p.key(); double lo = 0, hi = 0;
            if (key == "scale") { lo = 0.25; hi = 4; } else if (key == "detail") { lo = 0; hi = 1; } else if (key == "angle") { lo = -36000; hi = 36000; } else if (key == "seed") { lo = -2147483648.0; hi = 2147483647.0; } else throw std::runtime_error(prefix + "unknown config key '" + key + "'");
            if (!p.value().is_number() || !std::isfinite(p.value().get<double>()) || p.value().get<double>() < lo || p.value().get<double>() > hi || (key == "seed" && !p.value().is_number_integer())) throw std::runtime_error(prefix + "invalid config '" + key + "'");
        }
        float scale = config.value("scale", 1.0f), detail = config.value("detail", 0.5f), angle = config.value("angle", 0.0f);
        auto add = [&](Json node) { std::string id; do { id = "__preset_" + std::to_string(serial++); } while (nodes.contains(id)); if (config.contains("seed") && (node["op"] == "clouds" || node["op"] == "perlin" || node["op"] == "voronoi" || node["op"] == "scatter" || node["op"] == "array" || node["op"] == "white")) node["seed"] = config["seed"]; nodes[id] = node; return id; };
        auto noise = [&](float frequency, Json stretch = 1) { return add({{"op", "clouds"}, {"scale", frequency * scale}, {"stretch", stretch}, {"angle", angle}, {"octaves", 4}}); };
        auto blend = [&](std::string a, std::string b, std::string mode, float opacity) { return add({{"op", "blend"}, {"a", a}, {"b", b}, {"mode", mode}, {"opacity", opacity}}); };
        std::string result;
        if (name == "bnw_spots" || name == "gaussian_spots" || name == "dirt" || name == "grunge_rust") {
            auto shape = add({{"op", "shape"}, {"type", name == "gaussian_spots" ? "gaussian" : "circle"}, {"size", 0.95}, {"softness", name == "bnw_spots" ? 0.02 : 0.25}});
            auto spots = add({{"op", "scatter"}, {"input", shape}, {"count", int((100 + detail * 700) * scale)}, {"size", 0.055 / scale}, {"size_jitter", 0.75}, {"value_range", {0.2, 1}}, {"wrap", true}});
            if (name == "bnw_spots" || name == "gaussian_spots") result = spots;
            else { auto large = noise(5), fine = noise(60); auto dirty = blend(large, fine, "multiply", 0.45f); result = blend(dirty, spots, "max", 0.7f); if (name == "grunge_rust") result = add({{"op", "range_mask"}, {"input", result}, {"range", {0.3, 0.58}}, {"softness", 0.14}}); }
        } else if (name == "scratches" || name == "fibers" || name == "fur" || name == "shavings") {
            if (name == "fibers" || name == "fur") {
                auto stretched = noise(28, {0.7, 16});
                auto contrasted = add({{"op", "levels"}, {"input", stretched}, {"in", {0.34, 0.66}}});
                auto control = noise(5); result = add({{"op", "directional_warp"}, {"input", contrasted}, {"intensity", control}, {"angle", angle}, {"midlevel", 0.5}, {"strength", name == "fur" ? 0.06f + detail * 0.18f : 0.015f + detail * 0.06f}});
            } else {
                auto line = add({{"op", "shape"}, {"type", name == "shavings" ? "ring" : "capsule"}, {"size", {0.95, 0.95}}, {"thickness", 0.09}, {"softness", 0.025}});
                result = add({{"op", "scatter"}, {"input", line}, {"count", int((80 + 400 * detail) * scale)}, {"size", {std::min(0.6f, 0.22f / scale), (name == "shavings" ? 0.04 : 0.006) / scale}}, {"size_jitter", 0.6}, {"angle", angle}, {"rotation_jitter", name == "shavings" ? 180 : 12}, {"value_range", {0.15, 1}}, {"wrap", true}});
            }
        } else if (name == "brick") {
            int columns = std::max(1, int(6 * scale)), rows = std::max(1, int(10 * scale));
            auto box = add({{"op", "shape"}, {"type", "box"}, {"size", 0.96}, {"softness", 0.07}});
            auto layout = add({{"op", "array"}, {"input", box}, {"count", {columns, rows}}, {"size", {0.97 / columns, 0.94 / rows}}, {"row_offset", 0.5}, {"value_range", {0.72, 1}}, {"wrap", true}});
            result = blend(layout, noise(36), "multiply", detail * 0.45f);
        } else if (name == "weave") {
            int cells = std::max(2, int(16 * scale)); if (cells % 2) ++cells;
            auto a = add({{"op", "stripes"}, {"count", cells}, {"angle", 0}}), b = add({{"op", "stripes"}, {"count", cells}, {"angle", 90}}), mask = add({{"op", "checker"}, {"count", {cells, cells}}});
            auto weave = add({{"op", "blend"}, {"a", a}, {"b", b}, {"mask", mask}, {"mode", "mix"}}); result = blend(weave, noise(100), "multiply", detail * 0.4f);
        } else if (name == "ornament") {
            auto petal = add({{"op", "shape"}, {"type", "capsule"}, {"size", {0.95, 0.6}}, {"softness", 0.12}});
            auto flower = add({{"op", "radial"}, {"input", petal}, {"count", 8}, {"radius", 0.22}, {"size", {0.48, 0.23}}, {"orient", true}});
            int cells = std::max(1, int(4 * scale)); result = add({{"op", "array"}, {"input", flower}, {"count", {cells, cells}}, {"size", 0.95 / cells}, {"wrap", true}, {"angle", angle}, {"value_range", {1 - detail * 0.3f, 1}}});
        } else if (name == "crystal") {
            auto cells = add({{"op", "voronoi"}, {"scale", 12 * scale}, {"feature", "cells"}}), facets = add({{"op", "voronoi"}, {"scale", 12 * scale}, {"feature", "edges"}}); result = blend(cells, facets, "multiply", 0.3f + detail * 0.5f);
        } else if (name == "creased") {
            auto ridges = add({{"op", "clouds"}, {"scale", 7 * scale}, {"fractal", "ridged"}, {"angle", angle}, {"octaves", 4}});
            result = add({{"op", "math"}, {"input", ridges}, {"mode", "pow"}, {"value", 1 + detail * 3}});
        } else if (name == "plasma" || name == "fluid" || name == "liquid") {
            auto base = noise(name == "plasma" ? 5 : 10); auto x = noise(3), y = noise(4.7f);
            auto warped = add({{"op", "warp"}, {"input", base}, {"x", x}, {"y", y}, {"strength", 0.1f + detail * 0.35f}});
            if (name == "liquid") result = add({{"op", "range_mask"}, {"input", warped}, {"range", {0.47, 0.53}}, {"softness", 0.09}});
            else if (name == "fluid") result = add({{"op", "slope_blur"}, {"input", warped}, {"slope", x}, {"strength", 12}, {"samples", 12}});
            else result = warped;
        } else if (name == "grunge_leaks") {
            auto streaks = noise(12, {0.6, 12}); auto patch = noise(4); result = blend(streaks, patch, "multiply", 0.7f); result = add({{"op", "levels"}, {"input", result}, {"in", {0.12, 0.48}}, {"gamma", 0.7 + detail}});
        } else { auto base = noise(7), fine = noise(70); result = blend(base, fine, "multiply", 0.2f + detail * 0.65f); }
        // Rotate grid-based patterns after composition. Noise recipes rotate their domains.
        if (angle != 0 && (name == "brick" || name == "weave" || name == "crystal" || name == "bnw_spots" || name == "gaussian_spots")) result = add({{"op", "transform"}, {"input", result}, {"angle", angle}});
        nodes[it.key()] = nodes[result]; nodes.erase(result);
        if (nodes.size() > 4096) throw std::runtime_error("maximum 4096 nodes after preset expansion");
    }
    return nodes;
}
}
