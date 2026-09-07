#include "texutil/texutil.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>

namespace tex {
namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
void fields(const Json& value, const std::set<std::string>& allowed, const std::string& label) {
    require(value.is_object(), label + " must be an object");
    for (auto it = value.begin(); it != value.end(); ++it) require(allowed.count(it.key()) != 0, label + ": unknown field '" + it.key() + "'");
}
int boundedInteger(const Json& value, int min, int max, const std::string& label) {
    require(value.is_number_integer(), label + " must be an integer");
    double v = value.get<double>(); require(v >= min && v <= max, label + " is out of range"); return value.get<int>();
}
bool boolean(const Json& value, const std::string& label) { require(value.is_boolean(), label + " must be boolean"); return value.get<bool>(); }
std::string string(const Json& value, const std::string& label) { require(value.is_string(), label + " must be a string"); return value.get<std::string>(); }
}
Graph::Graph(Json doc, std::filesystem::path base, Options options) : base_(std::move(base)), options_(std::move(options)) {
    fields(doc, {"version", "size", "seed", "tile", "background", "alpha", "format", "bits", "srgb", "nodes", "outputs"}, "document");
    if (doc.contains("version")) require(boundedInteger(doc["version"], 1, 1, "version") == 1, "unsupported version");
    if (doc.contains("size")) {
        auto s = doc["size"];
        if (s.is_array()) { require(s.size() == 2, "size must be a number or [width,height]"); width_ = boundedInteger(s[0], 1, 16384, "width"); height_ = boundedInteger(s[1], 1, 16384, "height"); }
        else width_ = height_ = boundedInteger(s, 1, 16384, "size");
    }
    if (doc.contains("seed")) seed_ = boundedInteger(doc["seed"], -2147483647 - 1, 2147483647, "seed");
    if (options_.width) { require(options_.width > 0 && options_.width <= 16384 && options_.height > 0 && options_.height <= 16384, "override size must be 1..16384"); width_ = options_.width; height_ = options_.height; }
    if (options_.seed) seed_ = *options_.seed;
    require(options_.threads <= 256, "threads must be 1..256");
    require(options_.memoryMb >= 1 && options_.memoryMb <= 1048576, "memory budget must be 1..1048576 MB");
    tile_ = boolean(doc.value("tile", Json(false)), "tile");
    if (doc.contains("background")) background_ = color(doc["background"]);
    require(background_[3] == 1, "background must be opaque; use alpha:true for transparency");
    bool alpha = boolean(doc.value("alpha", Json(false)), "alpha"), srgb = boolean(doc.value("srgb", Json(true)), "srgb");
    std::string defaultFormat = string(doc.value("format", Json("png")), "format");
    require(defaultFormat == "png" || defaultFormat == "ppm" || defaultFormat == "pgm" || defaultFormat == "pfm", "format must be png, ppm, pgm or pfm");
    int defaultBits = boundedInteger(doc.value("bits", Json(defaultFormat == "pfm" ? 32 : 8)), 8, 32, "bits");
    require(defaultBits == 8 || defaultBits == 16 || defaultBits == 32, "bits must be 8, 16 or 32");
    require(doc.contains("nodes") && doc["nodes"].is_object() && !doc["nodes"].empty(), "nodes must be a nonempty object");
    require(doc["nodes"].size() <= 4096, "maximum 4096 nodes");
    for (auto it = doc["nodes"].begin(); it != doc["nodes"].end(); ++it) {
        require(!it.key().empty(), "node names cannot be empty");
        nodes_[it.key()] = normalizedNode(it.key(), it.value());
        if (nodes_[it.key()]["op"] == "image") require(std::filesystem::is_regular_file(base_ / nodes_[it.key()]["path"].get<std::string>()), "node '" + it.key() + "': image file does not exist");
    }
    // Validate the entire graph, including unreachable nodes, before writing anything.
    std::map<std::string, int> state;
    std::function<void(const std::string&)> visit = [&](const std::string& id) {
        require(nodes_.count(id) != 0, "unknown node reference '" + id + "'");
        require(state[id] != 1, "cycle detected at node '" + id + "'");
        if (state[id] == 2) return;
        state[id] = 1;
        for (const auto& dependency : dependencies(nodes_.at(id))) visit(dependency);
        state[id] = 2;
    };
    for (const auto& [id, node] : nodes_) visit(id);
    require(doc.contains("outputs") && doc["outputs"].is_object() && !doc["outputs"].empty(), "outputs must be a nonempty object mapping filenames to nodes");
    require(doc["outputs"].size() <= 1024, "maximum 1024 outputs");
    std::set<std::filesystem::path> destinations;
    for (auto it = doc["outputs"].begin(); it != doc["outputs"].end(); ++it) {
        Json o = it.value(); if (o.is_string()) o = {{"node", o}};
        fields(o, {"node", "format", "bits", "alpha", "srgb"}, "output '" + it.key() + "'");
        require(o.contains("node"), "output '" + it.key() + "' needs node");
        Output out; out.node = string(o["node"], "output node"); require(nodes_.count(out.node), "output references unknown node '" + out.node + "'");
        auto path = std::filesystem::path(it.key());
        require(!it.key().empty() && !path.is_absolute() && path.has_filename(), "output filename must be relative to --out");
        for (const auto& part : path) require(part != "..", "output filenames cannot contain '..'");
        std::string extension = path.extension().string(); if (!extension.empty()) extension.erase(0, 1);
        out.format = string(o.value("format", Json(extension.empty() ? defaultFormat : extension)), "output format");
        require(out.format == "png" || out.format == "ppm" || out.format == "pgm" || out.format == "pfm", "unsupported output format '" + out.format + "'");
        require(extension.empty() || extension == out.format, "output extension must match format");
        if (extension.empty()) path += "." + out.format;
        require(destinations.insert(path.lexically_normal()).second, "duplicate output path '" + path.string() + "'");
        const auto destination = std::filesystem::weakly_canonical(options_.out / path);
        for (const auto& [id, node] : nodes_) if (node["op"] == "image") require(destination != std::filesystem::weakly_canonical(base_ / node["path"].get<std::string>()), "output would overwrite input image for node '" + id + "'");
        out.name = path.string();
        out.bits = boundedInteger(o.value("bits", Json(out.format == "pfm" ? 32 : defaultBits)), 8, 32, "output bits");
        require(out.format == "pfm" ? out.bits == 32 : (out.bits == 8 || out.bits == 16), "PFM requires 32 bits; PNG/PPM/PGM require 8 or 16");
        out.alpha = boolean(o.value("alpha", Json(alpha)), "output alpha");
        require(!out.alpha || out.format == "png", "alpha output requires PNG");
        out.srgb = boolean(o.value("srgb", Json(out.format == "pfm" ? false : srgb)), "output srgb");
        require(out.format != "pfm" || !out.srgb, "PFM outputs must be linear (srgb:false)");
        outputs_.push_back(out);
    }
    state.clear();
    std::function<void(const std::string&)> schedule = [&](const std::string& id) {
        if (state[id]) return;
        state[id] = 1;
        for (const auto& dependency : dependencies(nodes_.at(id))) schedule(dependency);
        order_.push_back(id);
    };
    for (const auto& output : outputs_) schedule(output.node);
}
Json Graph::summary() const { return {{"size", {width_, height_}}, {"seed", seed_}, {"nodes", nodes_.size()}, {"reachable", order_.size()}, {"outputs", outputs_.size()}, {"order", order_}}; }
Json Graph::render(const std::function<void(const Output&, const ImagePtr&)>& sink) {
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now();
    auto memory = std::make_shared<Memory>(); memory->limit = options_.memoryMb * 1024ull * 1024;
    Workers workers(options_.threads); Context context{width_, height_, seed_, tile_, base_, workers, memory};
    std::map<std::string, size_t> uses;
    for (const auto& id : order_) for (const auto& dep : dependencies(nodes_.at(id))) ++uses[dep];
    std::map<std::string, ImagePtr> cache;
    Json stats = summary(); stats["timings"] = Json::array(); stats["files"] = Json::array(); stats["threads"] = workers.count();
    double exportMs = 0;
    for (const auto& id : order_) {
        auto begin = Clock::now();
        ImagePtr result;
        try { result = execute(nodes_.at(id), cache, context); }
        catch (const std::exception& error) { throw std::runtime_error("node '" + id + "' (" + nodes_[id]["op"].get<std::string>() + "): " + error.what()); }
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        stats["timings"].push_back({{"node", id}, {"op", nodes_.at(id)["op"]}, {"ms", ms}});
        cache[id] = result;
        for (const auto& output : outputs_) if (output.node == id) {
            auto exportStart = Clock::now();
            if (sink) sink(output, result);
            else writeImage(options_.out / output.name, *result, output.format, output.bits, output.alpha, output.srgb, background_);
            exportMs += std::chrono::duration<double, std::milli>(Clock::now() - exportStart).count();
            stats["files"].push_back((options_.out / output.name).string());
        }
        for (const auto& dep : dependencies(nodes_.at(id))) if (--uses[dep] == 0) cache.erase(dep);
        if (uses[id] == 0) cache.erase(id);
    }
    stats["peak_buffer_mb"] = memory->peak / (1024.0 * 1024.0);
    stats["export_ms"] = exportMs;
    stats["total_ms"] = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return stats;
}
}
