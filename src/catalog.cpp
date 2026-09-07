#include "texutil/texutil.hpp"
#include <cmath>
#include <set>
#include <stdexcept>

namespace tex {
namespace {
Json field(std::string type, Json value, std::string description) { return {{"type", type}, {"default", value}, {"description", description}}; }
Json number(double value, double min, double max, std::string description) { auto f = field("number", value, description); f["min"] = min; f["max"] = max; return f; }
Json integer(int value, int min, int max, std::string description) { auto f = number(value, min, max, description); f["type"] = "integer"; return f; }
Json choice(std::string value, Json choices, std::string description) { auto f = field("string", value, description); f["enum"] = choices; return f; }
Json required(std::string type, std::string description) { return {{"type", type}, {"required", true}, {"description", description}}; }
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
}
Json catalog() {
    Json result = Json::object();
    auto add = [&](std::string name, std::string description, Json fields) { result[name] = {{"description", description}, {"parameters", fields}}; };
    auto input = required("reference", "Source node name.");
    auto scale = field("positive_pair", 1.0, "Positive scalar or [x,y] scale.");
    auto center = field("pair", Json::array({0.5, 0.5}), "Center in normalized image coordinates.");
    auto edge = choice("repeat", {"repeat", "clamp", "transparent"}, "Sampling outside the source.");
    auto mode = choice("over", {"over", "mix", "multiply", "add", "subtract", "screen", "overlay", "min", "max", "difference"}, "Overlap/blend operation.");
    auto opacity = number(1, 0, 1, "Blend strength, 0 to 1.");
    Json noise = {
        {"scale", number(8, 0.001, 4096, "Noise frequency across a unit image; larger gives smaller features.")},
        {"seed", field("seed", nullptr, "Override document seed; omitted uses document seed.")},
        {"tile", field("boolean", nullptr, "Override document tile setting; uses 4D torus sampling.")},
        {"fractal", choice("none", {"none", "fbm", "ridged"}, "Fractal noise combination.")},
        {"octaves", integer(5, 1, 12, "Number of fractal octaves.")},
        {"gain", number(0.5, 0, 1, "Amplitude multiplier per octave.")},
        {"lacunarity", number(2, 1, 4, "Frequency multiplier per octave.")}
    };
    for (auto name : {"simplex", "perlin", "value", "white", "clouds", "voronoi"}) add(name, std::string("Scalar ") + name + " noise, backed by FastNoise2.", noise);
    result["clouds"]["parameters"]["fractal"]["default"] = "fbm";
    auto& vor = result["voronoi"]["parameters"];
    vor["distance"] = choice("euclidean", {"euclidean", "squared", "manhattan", "hybrid"}, "Cell distance metric.");
    vor["feature"] = choice("distance", {"distance", "cells", "edges"}, "Nearest distance, random cell values, or F2-F1 edges (bright cell interiors).");
    vor["jitter"] = number(1, 0, 1, "Cell point jitter.");
    add("constant", "Solid scalar or linear color. Hex strings are decoded from sRGB.", {{"value", field("color", 0.0, "Number, #RRGGBB[AA], or linear [r,g,b,a].")}});
    add("gradient", "Linear, radial or angular scalar gradient.", {
        {"type", choice("linear", {"linear", "radial", "angular"}, "Gradient geometry.")}, {"center", center},
        {"angle", number(0, -36000, 36000, "Clockwise degrees; 0 runs left to right.")},
        {"radius", number(0.5, 0.0001, 16, "Radial range in UV units.")}, {"reverse", field("boolean", false, "Invert gradient.")}
    });
    add("shape", "Antialiased scalar mask or transparent colored stamp.", {
        {"type", choice("circle", {"circle", "box", "diamond", "ring"}, "Shape geometry.")}, {"center", center},
        {"size", field("positive_pair", 0.8, "Full width/height in UV units.")},
        {"angle", number(0, -36000, 36000, "Clockwise rotation in degrees.")},
        {"softness", number(0, 0, 1, "Inward edge feather, relative to half-size.")},
        {"thickness", number(0.2, 0.001, 1, "Ring thickness, relative to radius.")},
        {"color", field("color", nullptr, "Optional color; omission produces a scalar mask.")}
    });
    add("checker", "Alternating scalar squares.", {{"count", field("count_pair", Json::array({8, 8}), "Cell counts [columns,rows].")}});
    add("stripes", "Antialiased periodic scalar lines; angle 0 gives vertical grain.", {
        {"count", number(32, 0.001, 4096, "Cycles per unit UV along the gradient direction.")},
        {"angle", number(0, -36000, 36000, "Clockwise sampling direction in degrees; lines are perpendicular.")},
        {"phase", number(0, -10000, 10000, "Offset in cycles; 1 is a full period.")},
        {"wave", choice("sine", {"sine", "triangle", "saw", "square"}, "Periodic profile, box-filtered across the projected pixel footprint.")},
        {"duty", number(0.5, 0.001, 0.999, "White fraction of a square wave; ignored by other profiles.")}
    });
    add("image", "Import a PNG and resize to document dimensions.", {
        {"path", required("string", "PNG path relative to the JSON file.")},
        {"kind", choice("color", {"color", "scalar", "normal"}, "Interpretation of pixel values.")},
        {"srgb", field("boolean", true, "Decode sRGB for color inputs; ignored for data.")}
    });
    add("blend", "Combine two fields, optionally through a scalar mask.", {{"a", input}, {"b", input}, {"mask", field("reference", nullptr, "Optional mask node; clamped luminance multiplies opacity.")}, {"mode", mode}, {"opacity", opacity}});
    add("invert", "Invert RGB/scalar values, preserving alpha.", {{"input", input}});
    add("grayscale", "Linear Rec.709 luminance; discards alpha.", {{"input", input}});
    add("levels", "Clamp input range, apply gamma, map to output range.", {
        {"input", input}, {"in", field("pair", Json::array({0, 1}), "Strictly increasing input range.")},
        {"out", field("pair", Json::array({0, 1}), "Output range; may be reversed.")}, {"gamma", number(1, 0.01, 100, "Exponent uses 1/gamma.")}
    });
    add("threshold", "Threshold luminance with optional smooth transition.", {{"input", input}, {"value", number(0.5, -1000, 1000, "Threshold level.")}, {"softness", number(0, 0, 10, "Transition width.")}});
    add("blur", "Separable box blur, optionally repeated to approximate Gaussian blur.", {{"input", input}, {"radius", integer(3, 0, 2048, "Box radius in pixels.")}, {"passes", integer(3, 1, 6, "Repeated box passes.")}, {"edge", edge}});
    add("transform", "Inverse-mapped translation, scaling and rotation.", {{"input", input}, {"scale", scale}, {"offset", field("pair", Json::array({0, 0}), "UV translation.")}, {"angle", number(0, -36000, 36000, "Clockwise rotation in degrees.")}, {"edge", edge}});
    add("warp", "Displace sampling coordinates using independent scalar maps centered at 0.5.", {{"input", input}, {"x", input}, {"y", input}, {"strength", number(0.05, -10, 10, "Maximum UV displacement; map 0/1 maps to -/+strength.")}, {"edge", edge}});
    add("directional_warp", "Displace the source along one direction, optionally controlled by a grayscale image or channel.", {
        {"input", input}, {"intensity", field("reference", nullptr, "Optional control node; omission supplies white (1) everywhere.")},
        {"channel", choice("luminance", {"luminance", "r", "g", "b", "a"}, "Channel of intensity; scalar fields work directly. Values clamp to 0..1.")},
        {"angle", number(0, -36000, 36000, "Clockwise sampling direction: 0 right, 90 down. Visible content moves oppositely.")},
        {"strength", number(0.05, -10, 10, "UV displacement = strength * (intensity - midlevel).")},
        {"midlevel", number(0, 0, 1, "Neutral intensity; 0 makes black stationary, 0.5 gives signed displacement.")},
        {"edge", edge}
    });
    add("ramp", "Map scalar luminance to a color gradient with ordered stops.", {
        {"input", input}, {"stops", required("array", "At least two [position,color] stops, strictly increasing.")},
        {"interpolation", choice("linear", {"linear", "smooth", "constant"}, "Interpolation between stops in linear RGB.")}
    });
    add("normal", "Convert scalar height to a tangent-space normal using central differences.", {{"input", input}, {"strength", number(1, 0, 1000, "Height scale per unit UV; resolution independent.")}, {"convention", choice("directx", {"directx", "opengl"}, "DirectX uses +height derivative in image Y for green; OpenGL flips green.")}, {"edge", edge}});
    add("pack", "Pack scalar node luminances into RGBA, tagged as linear data.", {{"r", input}, {"g", input}, {"b", input}, {"a", field("reference", nullptr, "Optional alpha source; defaults to 1.")}});
    Json placement = {{"input", input}, {"size", field("positive_pair", 0.1, "Stamp full width/height in UV units.")}, {"angle", number(0, -36000, 36000, "Stamp clockwise angle.")}, {"opacity", opacity}, {"mode", mode}, {"wrap", field("boolean", false, "Wrap stamps across output boundaries; max stamp size 1 UV.")}};
    placement["mode"]["default"] = "max";
    auto stamps = placement;
    stamps["points"] = required("array", "Placements: [x,y] or {position:[x,y],size,angle,opacity}; up to 10000.");
    add("stamp", "Place a source image at explicit positions on an empty field.", stamps);
    auto array = placement;
    array["count"] = field("count_pair", Json::array({4, 4}), "Grid [columns,rows], maximum 10000 total.");
    array["jitter"] = number(0, 0, 1, "Position jitter as a fraction of each grid cell.");
    array["rotation_jitter"] = number(0, 0, 360, "Random +/- angle in degrees.");
    array["seed"] = field("seed", nullptr, "Override document seed.");
    add("array", "Grid of source stamps, centered in each cell.", array);
    auto radial = placement;
    radial["count"] = integer(12, 1, 10000, "Number of stamps around the circle.");
    radial["center"] = center;
    radial["radius"] = number(0.35, 0, 16, "Distance of stamp centers from array center in UV.");
    radial["start"] = number(-90, -36000, 36000, "Starting angle; -90 is top.");
    radial["sweep"] = number(360, -36000, 36000, "Angular sweep; excludes its endpoint.");
    radial["orient"] = field("boolean", true, "Rotate stamps with their radial placement angle.");
    add("radial", "Radial array of source stamps.", radial);
    addAdvancedCatalog(result);
    return result;
}

Json normalizedNode(const std::string& id, const Json& node) {
    const std::string prefix = "node '" + id + "': ";
    require(node.is_object(), prefix + "must be an object");
    require(node.contains("op") && node["op"].is_string(), prefix + "missing string 'op'");
    static const Json specs = catalog();
    std::string op = node["op"];
    require(specs.contains(op), prefix + "unknown operation '" + op + "'");
    const Json& params = specs[op]["parameters"];
    for (auto it = node.begin(); it != node.end(); ++it) require(it.key() == "op" || params.contains(it.key()), prefix + "unknown parameter '" + it.key() + "'");
    Json out = node;
    for (auto it = params.begin(); it != params.end(); ++it) {
        auto key = it.key(); const auto& f = it.value();
        if (!out.contains(key)) {
            require(!f.value("required", false), prefix + "missing '" + key + "'");
            if (!f["default"].is_null()) out[key] = f["default"];
            continue;
        }
        const auto& v = out[key]; const auto t = f["type"].get<std::string>();
        auto error = prefix + "invalid '" + key + "' (expected " + t + ")";
        if (t == "number" || t == "integer" || t == "seed") {
            require(v.is_number() && std::isfinite(v.get<double>()), error);
            if (t != "number") require(v.is_number_integer() && v.get<double>() >= -2147483648.0 && v.get<double>() <= 2147483647.0, error);
            if (f.contains("min")) require(v.get<double>() >= f["min"].get<double>() && v.get<double>() <= f["max"].get<double>(), error + ": out of range");
        } else if (t == "boolean") require(v.is_boolean(), error);
        else if (t == "reference" || t == "string") require(v.is_string() && !v.get<std::string>().empty(), error);
        else if (t == "array") require(v.is_array(), error);
        else if (t == "object") require(v.is_object(), error);
        else if (t == "references") { require(v.is_array() && v.size() <= 64, error); for (const auto& r : v) require(r.is_string() && !r.get<std::string>().empty(), error); }
        else if (t == "color") { try { color(v); } catch (...) { throw std::runtime_error(error); } }
        else if (t == "pair" || t == "positive_pair" || t == "count_pair") {
            Json pair = v;
            if (t == "positive_pair" && v.is_number()) pair = Json::array({v, v});
            require(pair.is_array() && pair.size() == 2, error);
            for (const auto& x : pair) {
                require(x.is_number() && std::isfinite(x.get<double>()) && std::abs(x.get<double>()) <= 10000, error);
                if (t == "positive_pair") require(x.get<double>() >= 0.0001 && x.get<double>() <= 32, error);
                if (t == "count_pair") require(x.is_number_integer() && x.get<int>() >= 1 && x.get<int>() <= 10000, error);
            }
            if (t == "count_pair") require(pair[0].get<int>() * pair[1].get<int>() <= 10000, error + ": maximum 10000 cells");
            out[key] = pair;
        }
        if (f.contains("enum")) require(std::find(f["enum"].begin(), f["enum"].end(), v) != f["enum"].end(), error + ": unsupported value");
    }
    // Normalize defaults that accept either a scalar or a pair.
    for (auto it = params.begin(); it != params.end(); ++it) if (it.value()["type"] == "positive_pair" && out.contains(it.key()) && out[it.key()].is_number()) out[it.key()] = Json::array({out[it.key()], out[it.key()]});
    if ((op == "math" || op == "auto_levels" || op == "range_mask") && out.contains("range")) require(out["range"][0].get<float>() <= out["range"][1].get<float>(), prefix + "range must increase");
    if (out.contains("value_range")) require(out["value_range"][0].get<float>() >= 0 && out["value_range"][0].get<float>() <= out["value_range"][1].get<float>() && out["value_range"][1].get<float>() <= 1, prefix + "value_range must increase within 0..1");
    if (op == "swirl" && out["wrap"].get<bool>()) require(out["radius"].get<float>() <= 0.5f && out["edge"] == "repeat", prefix + "wrapped swirl requires radius <= 0.5 and edge:repeat");
    if (op == "levels") require(out["in"][0].get<float>() < out["in"][1].get<float>(), prefix + "input range must increase at float precision");
    if (out.contains("fractal")) {
        double frequency = out["scale"].get<double>() / std::min(out["stretch"][0].get<double>(), out["stretch"][1].get<double>());
        if (out["fractal"] != "none") frequency *= std::pow(out["lacunarity"].get<double>(), out["octaves"].get<int>() - 1);
        require(frequency <= 10000000, prefix + "combined octave frequency including stretch exceeds 10000000");
    }
    if (op == "ramp") {
        require(out["stops"].size() >= 2 && out["stops"].size() <= 1024, prefix + "requires 2..1024 stops");
        float previous = -INFINITY;
        for (const auto& stop : out["stops"]) {
            require(stop.is_array() && stop.size() == 2 && stop[0].is_number(), prefix + "stop must be [position,color]");
            float position = stop[0].get<float>();
            require(std::isfinite(position) && position > previous, prefix + "stop positions must strictly increase");
            color(stop[1]); previous = position;
        }
    }
    if (op == "stamp") {
        require(out["points"].size() <= 10000, prefix + "maximum 10000 stamps");
        for (auto& p : out["points"]) {
            if (p.is_array()) p = {{"position", p}};
            require(p.is_object() && p.contains("position"), prefix + "stamp needs position");
            for (auto it = p.begin(); it != p.end(); ++it) require(it.key() == "position" || it.key() == "size" || it.key() == "angle" || it.key() == "opacity", prefix + "unknown stamp field '" + it.key() + "'");
            // Reuse transform and blend validators for the nested placement fields.
            auto v = normalizedNode(id + ".point", {{"op", "transform"}, {"input", "_"}, {"offset", p["position"]}, {"scale", p.value("size", out["size"])}, {"angle", p.value("angle", out["angle"])}});
            auto b = normalizedNode(id + ".point", {{"op", "blend"}, {"a", "_"}, {"b", "_"}, {"opacity", p.value("opacity", out["opacity"])}});
            p = {{"position", v["offset"]}, {"size", v["scale"]}, {"angle", v["angle"]}, {"opacity", b["opacity"]}};
        }
    }
    if ((op == "stamp" || op == "array" || op == "radial" || op == "scatter") && out["wrap"].get<bool>()) {
        auto check = [&](const Json& size) { require(size[0].get<float>() * (1 + out.value("size_jitter", 0.0f)) <= 1 && size[1].get<float>() * (1 + out.value("size_jitter", 0.0f)) <= 1, prefix + "wrapped stamps require size <= 1"); };
        check(out["size"]);
        if (op == "stamp") for (auto& p : out["points"]) check(p["size"]);
    }
    return out;
}
std::vector<std::string> dependencies(const Json& node) {
    static const Json specs = catalog();
    std::set<std::string> unique;
    for (auto it = specs[node["op"].get<std::string>()]["parameters"].begin(); it != specs[node["op"].get<std::string>()]["parameters"].end(); ++it) if (it.value()["type"] == "reference" && node.contains(it.key())) unique.insert(node[it.key()].get<std::string>());
    if (node.contains("sources")) for (const auto& ref : node["sources"]) unique.insert(ref.get<std::string>());
    return {unique.begin(), unique.end()};
}
}
