#include "texutil/texutil.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace tex;
namespace {
void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
void near(float actual, float expected, float epsilon = 1e-5f) { check(std::abs(actual - expected) <= epsilon, "expected " + std::to_string(expected) + ", got " + std::to_string(actual)); }
Json document(Json nodes, std::string output = "result", int size = 32) { return {{"size", size}, {"nodes", nodes}, {"outputs", {{"result.png", output}}}}; }
ImagePtr run(Json doc, unsigned threads = 1) {
    Options options; options.threads = threads; ImagePtr result;
    Graph(doc, ".", options).render([&](const Output&, const ImagePtr& image) { result = image; }); return result;
}
ImagePtr node(Json n, int size = 32) { return run(document({{"result", n}}, "result", size)); }
void fails(Json doc, const std::string& contains) {
    try { Graph(doc, "."); } catch (const std::exception& e) { check(std::string(e.what()).find(contains) != std::string::npos, std::string("wrong error: ") + e.what()); return; }
    throw std::runtime_error("expected validation error: " + contains);
}
void testGraph() {
    auto doc = document({{"a", {{"op", "constant"}, {"value", 0.5}}}, {"b", {{"op", "constant"}, {"value", 0.25}}}, {"result", {{"op", "blend"}, {"a", "a"}, {"b", "b"}, {"mode", "multiply"}, {"opacity", 0.8}}}});
    near(run(doc)->get(0, 0)[0], 0.2f);
    doc["nodes"]["mask"] = {{"op", "constant"}, {"value", 0.5}};
    doc["nodes"]["result"]["mask"] = "mask";
    near(run(doc)->get(0, 0)[0], 0.35f);
    doc["outputs"]["a.png"] = "a"; doc["outputs"]["again.png"] = "result";
    doc["nodes"]["unused"] = {{"op", "constant"}};
    auto stats = Graph(doc, ".").render([](const Output&, const ImagePtr&) {});
    check(stats["timings"].size() == 4, "shared nodes computed once, unused skipped");
    auto bad = doc; bad["nodes"]["unused"] = {{"op", "invert"}, {"input", "unused"}}; fails(bad, "cycle");
    bad = doc; bad["nodes"]["result"]["b"] = "missing"; fails(bad, "unknown node");
    bad = doc; bad["nodes"]["a"]["vlaue"] = 1; fails(bad, "unknown parameter");
    bad = doc; bad["nodes"]["a"]["value"] = "#xxxxxx"; fails(bad, "invalid 'value'");
    bad = doc; bad["size"] = 0; fails(bad, "range");
    bad = doc; bad["size"] = 2.5; fails(bad, "integer");
    bad = doc; bad["nodes"]["result"]["opacity"] = 3; fails(bad, "range");
    bad = doc; bad["outputs"] = {{"../outside.png", "a"}}; fails(bad, "cannot contain");
    bad = doc; bad["outputs"] = {{"x", "a"}, {"x.png", "a"}}; fails(bad, "duplicate output");
    bad = doc; bad["outputs"] = {{"x.png", {{"node", "a"}, {"bits", 32}}}}; fails(bad, "require 8 or 16");
    bad = doc; bad["outputs"] = {{"x.ppm", {{"node", "a"}, {"alpha", true}}}}; fails(bad, "alpha output");
    bad = doc; bad["nodes"]["a"] = {{"op", "levels"}, {"input", "b"}, {"in", {1, 1}}}; fails(bad, "must increase");
    bad = doc; bad["nodes"]["a"] = {{"op", "ramp"}, {"input", "b"}, {"stops", {{0.5, "#000000"}, {0.4, "#ffffff"}}}}; fails(bad, "strictly increase");
    bad = doc; bad["nodes"]["a"] = {{"op", "array"}, {"input", "b"}, {"count", {10000, 2}}}; fails(bad, "10000");
    bad = doc; bad["nodes"]["a"] = {{"op", "stamp"}, {"input", "b"}, {"points", {{{"position", {0.5, 0.5}}, {"angle", "wrong"}}}}}; fails(bad, "angle");
    // Liveness: a 256x256 scalar image is .25 MiB. A long chain fits 1 MiB.
    Json chain = { {"a", {{"op", "constant"}, {"value", 0.25}}} }; std::string previous = "a";
    for (int i = 0; i < 30; ++i) { std::string id = "n" + std::to_string(i); chain[id] = {{"op", "invert"}, {"input", previous}}; previous = id; }
    Options options; options.memoryMb = 1; options.threads = 1;
    auto memoryStats = Graph(document(chain, previous, 256), ".", options).render([](const Output&, const ImagePtr& image) { near(image->get(0, 0)[0], 0.25); });
    check(memoryStats["peak_buffer_mb"].get<double>() <= 0.5, "intermediates released promptly");
    options.memoryMb = 1;
    bool exhausted = false;
    try { Graph(document({{"result", {{"op", "constant"}}}}, "result", 1024), ".", options).render([](const Output&, const ImagePtr&) {}); } catch (const std::exception& e) { exhausted = std::string(e.what()).find("memory budget") != std::string::npos; }
    check(exhausted, "memory limit enforced");
}
void testNormalsAndRamps() {
    auto doc = document({{"height", {{"op", "constant"}, {"value", 0.6}}}, {"result", {{"op", "normal"}, {"input", "height"}}}});
    auto flat = run(doc); auto p = flat->get(12, 12); near(p[0], 0.5); near(p[1], 0.5); near(p[2], 1);
    doc["nodes"]["height"] = {{"op", "gradient"}, {"angle", 90}};
    auto dx = run(doc); doc["nodes"]["result"]["convention"] = "opengl"; auto gl = run(doc);
    p = dx->get(12, 12); auto q = gl->get(12, 12);
    near(p[0], q[0]); near(p[2], q[2]); near(p[1], 1 - q[1]); near(p[1], 0.5f + std::sqrt(0.5f) * 0.5f);
    doc["size"] = 64; auto larger = run(doc); near(larger->get(24, 24)[1], q[1]);
    doc["nodes"]["height"] = {{"op", "constant"}, {"value", 0.5}};
    doc["nodes"]["result"] = {{"op", "ramp"}, {"input", "height"}, {"stops", {{0, "#ff0000"}, {1, "#0000ff"}}}};
    p = run(doc)->get(0, 0); near(p[0], 0.5); near(p[1], 0); near(p[2], 0.5);
    doc["nodes"]["result"]["interpolation"] = "constant"; p = run(doc)->get(0, 0); near(p[0], 1); near(p[2], 0);
    doc["nodes"]["height"]["value"] = 2; p = run(doc)->get(0, 0); near(p[2], 1);
    near(color("#808080")[0], 0.2158605f);
}
void testNoise() {
    for (int size : {1, 2, 3, 17, 33}) for (bool tile : {true, false}) {
        auto tiny = node({{"op", "simplex"}, {"tile", tile}}, size);
        check(tiny->pixels.size() == static_cast<size_t>(size * size), "SIMD tails and tiny sizes");
    }
    for (std::string op : {"simplex", "perlin", "value", "white", "clouds", "voronoi"}) for (bool tile : {false, true}) {
        auto doc = document({{"result", {{"op", op}, {"tile", tile}, {"seed", 77}}}}, "result", 64);
        auto single = run(doc, 1), multi = run(doc, 4);
        check(single->pixels == multi->pixels, op + " deterministic across thread counts");
        auto [lo, hi] = std::minmax_element(single->pixels.begin(), single->pixels.end());
        check(*lo >= 0 && *hi <= 1 && *hi - *lo > 0.02f, op + " usable range");
        for (float value : single->pixels) check(std::isfinite(value), op + " finite");
        doc["nodes"]["result"]["seed"] = 78; check(run(doc)->pixels != single->pixels, op + " seed variation");
    }
    for (std::string feature : {"distance", "cells", "edges"}) for (std::string distance : {"euclidean", "squared", "manhattan", "hybrid"}) {
        auto image = node({{"op", "voronoi"}, {"feature", feature}, {"distance", distance}, {"tile", true}}, 64);
        for (float value : image->pixels) check(std::isfinite(value), "cellular finite");
    }
    for (std::string fractal : {"fbm", "ridged"}) {
        auto image = node({{"op", "perlin"}, {"fractal", fractal}, {"octaves", 6}, {"gain", 1}, {"tile", true}});
        for (float value : image->pixels) check(value >= 0 && value <= 1, "fractal normalized");
    }
    auto image = node({{"op", "simplex"}, {"scale", 3}, {"tile", true}}, 256);
    double seam = 0, interior = 0;
    for (int y = 0; y < 256; ++y) { seam += std::abs(image->get(0, y)[0] - image->get(255, y)[0]); for (int x = 1; x < 256; ++x) interior += std::abs(image->get(x, y)[0] - image->get(x - 1, y)[0]); }
    check(seam < interior / 255 * 2.5, "periodic noise has no discontinuous X seam");
}
void testSpatial() {
    auto doc = document({{"source", {{"op", "gradient"}}}, {"result", {{"op", "transform"}, {"input", "source"}}}}, "result", 64);
    auto identity = run(doc), source = node({{"op", "gradient"}}, 64); check(identity->pixels == source->pixels, "identity transform");
    doc["nodes"]["result"]["offset"] = {1, 0}; check(run(doc)->pixels == source->pixels, "repeat translation");
    doc["nodes"]["result"]["edge"] = "transparent"; auto outside = run(doc); near(outside->get(20, 20)[0], 0);
    doc["nodes"]["result"]["offset"] = {10000, 10000}; doc["nodes"]["result"]["scale"] = 0.0001; near(run(doc)->get(0, 0)[0], 0);
    doc["nodes"]["neutral"] = {{"op", "constant"}, {"value", 0.5}};
    doc["nodes"]["result"] = {{"op", "warp"}, {"input", "source"}, {"x", "neutral"}, {"y", "neutral"}};
    check(run(doc)->pixels == source->pixels, "neutral warp");
    doc["nodes"]["result"] = {{"op", "blur"}, {"input", "neutral"}, {"radius", 100}, {"passes", 3}};
    near(run(doc)->get(0, 0)[0], 0.5);
    doc["nodes"]["source"] = {{"op", "shape"}, {"type", "circle"}, {"color", "#ff0000"}};
    doc["nodes"]["result"] = {{"op", "blur"}, {"input", "source"}, {"radius", 3}, {"edge", "transparent"}};
    auto blurred = run(doc);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) { auto p = blurred->get(x, y); if (p[3] > 1e-5f) near(p[0], 1); }
    // Grid and radial placement put stamps at analytically known centers.
    doc["nodes"]["source"] = {{"op", "shape"}, {"size", 0.8}};
    doc["nodes"]["result"] = {{"op", "array"}, {"input", "source"}, {"count", {2, 2}}, {"size", 0.2}};
    auto grid = run(doc); near(grid->get(16, 16)[0], 1); near(grid->get(32, 32)[0], 0);
    doc["nodes"]["result"] = {{"op", "radial"}, {"input", "source"}, {"count", 4}, {"radius", 0.25}, {"size", 0.2}, {"start", 0}};
    auto ring = run(doc); near(ring->get(48, 32)[0], 1); near(ring->get(32, 16)[0], 1); near(ring->get(32, 32)[0], 0);
    doc["nodes"]["result"] = {{"op", "stamp"}, {"input", "source"}, {"size", 0.3}, {"points", {{0, 0.5}}}, {"wrap", true}};
    auto wrapped = run(doc); near(wrapped->get(0, 32)[0], 1); near(wrapped->get(63, 32)[0], 1);
    doc["nodes"]["result"] = {{"op", "array"}, {"input", "source"}, {"count", {8, 8}}, {"size", 0.1}, {"jitter", 0.8}, {"rotation_jitter", 45}};
    check(run(doc, 1)->pixels == run(doc, 4)->pixels, "deterministic jitter and overlap");
    // Scalar overlap and alpha are independent: half-transparent red over blue.
    auto p = composite({0, 0, 1, 1}, {1, 0, 0, 0.5f}, "over", 1, false); near(p[0], 0.5); near(p[2], 0.5); near(p[3], 1);
}
void testFormats() {
    auto dir = std::filesystem::temp_directory_path() / ("texutil-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    auto memory = std::make_shared<Memory>(); Image scalarImage(3, 1, Kind::Scalar, memory);
    scalarImage.set(0, 0, {0.12345f, 0, 0, 1}); scalarImage.set(1, 0, {0.5f, 0, 0, 1}); scalarImage.set(2, 0, {1, 0, 0, 1});
    writeImage(dir / "height.png", scalarImage, "png", 16, false, true, {0, 0, 0, 1});
    auto decoded = readPng(dir / "height.png", Kind::Scalar, false, memory);
    near(decoded->get(0, 0)[0], 0.12345f, 1.0f / 65535); near(decoded->get(1, 0)[0], 0.5f, 1.0f / 65535);
    // Exercise import resize and ensure document-relative paths resolve independently of cwd.
    auto importDoc = document({{"result", {{"op", "image"}, {"path", "height.png"}, {"kind", "scalar"}}}}, "result", 3);
    Graph(importDoc, dir).render([&](const Output&, const ImagePtr& image) { near(image->get(0, 2)[0], 0.12345f, 1.0f / 65535); });
    Options collision; collision.out = dir; importDoc["outputs"] = {{"height.png", "result"}};
    bool protectedInput = false; try { Graph(importDoc, dir, collision); } catch (...) { protectedInput = true; } check(protectedInput, "input overwrite rejected");
    Image rgba(1, 1, Kind::Color, memory); rgba.set(0, 0, {0.2f, 0.4f, 0.6f, 0.25f});
    writeImage(dir / "rgba.png", rgba, "png", 16, true, true, {0, 0, 0, 1});
    auto roundtrip = readPng(dir / "rgba.png", Kind::Color, true, memory);
    for (int c = 0; c < 4; ++c) near(roundtrip->get(0, 0)[c], rgba.get(0, 0)[c], 3e-5);
    writeImage(dir / "flat.png", rgba, "png", 16, false, false, {1, 1, 1, 1});
    auto flattened = readPng(dir / "flat.png", Kind::Color, false, memory); near(flattened->get(0, 0)[0], 0.8f, 2e-5);
    for (std::string format : {"ppm", "pgm", "pfm"}) {
        writeImage(dir / ("height." + format), scalarImage, format, format == "pfm" ? 32 : 16, false, false, {0, 0, 0, 1});
        std::ifstream file(dir / ("height." + format), std::ios::binary); std::string magic; int w, h; double maximum; file >> magic >> w >> h >> maximum; file.get();
        check(w == 3 && h == 1, "export dimensions");
        if (format == "ppm") { unsigned char bytes[6]; file.read(reinterpret_cast<char*>(bytes), 6); check(bytes[0] == bytes[2] && bytes[0] == bytes[4] && bytes[1] == bytes[3] && bytes[1] == bytes[5], "scalar PPM RGB promotion"); }
        if (format == "pfm") { float value; file.read(reinterpret_cast<char*>(&value), 4); near(value, 0.12345f); }
    }
    // Independent malformed PNG rejection.
    { std::ofstream invalid(dir / "invalid.png"); invalid << "not a PNG"; }
    bool rejected = false; try { readPng(dir / "invalid.png", Kind::Color, true, memory); } catch (...) { rejected = true; } check(rejected, "invalid PNG rejected");
    std::filesystem::remove_all(dir);
}
void testRemainingNodes() {
    auto doc = document({{"source", {{"op", "constant"}, {"value", {1, 0, 0}}}}, {"result", {{"op", "grayscale"}, {"input", "source"}}}});
    near(run(doc)->get(0, 0)[0], 0.2126f);
    doc["nodes"]["source"] = {{"op", "constant"}, {"value", 0.5}};
    doc["nodes"]["result"] = {{"op", "threshold"}, {"input", "source"}, {"softness", 0.2}};
    near(run(doc)->get(0, 0)[0], 0.5);
    doc["nodes"]["result"]["softness"] = 0; near(run(doc)->get(0, 0)[0], 1);
    doc["nodes"]["result"] = {{"op", "levels"}, {"input", "source"}, {"in", {0, 1}}, {"out", {0, 2}}, {"gamma", 2}};
    near(run(doc)->get(0, 0)[0], std::sqrt(0.5f) * 2);
    doc["nodes"]["r"] = {{"op", "constant"}, {"value", 0.2}};
    doc["nodes"]["g"] = {{"op", "constant"}, {"value", 0.7}};
    doc["nodes"]["result"] = {{"op", "pack"}, {"r", "r"}, {"g", "g"}, {"b", "source"}, {"a", "g"}};
    auto packed = run(doc); auto p = packed->get(0, 0); near(p[0], 0.2); near(p[1], 0.7); near(p[2], 0.5); near(p[3], 0.7); check(packed->kind == Kind::Normal, "pack is data");
    for (auto type : {"circle", "box", "diamond", "ring"}) {
        auto shape = node({{"op", "shape"}, {"type", type}}); near(shape->get(0, 0)[0], 0); near(shape->get(16, 16)[0], std::string(type) == "ring" ? 0 : 1);
    }
    for (auto type : {"linear", "radial", "angular"}) { auto g = node({{"op", "gradient"}, {"type", type}}); for (float v : g->pixels) check(v >= 0 && v <= 1, "gradient range"); }
    std::map<std::string, float> expected{{"over", 0.75f}, {"mix", 0.75f}, {"multiply", 0.1875f}, {"add", 1}, {"subtract", -0.5f}, {"screen", 0.8125f}, {"overlay", 0.375f}, {"min", 0.25f}, {"max", 0.75f}, {"difference", 0.5f}};
    for (const auto& [mode, value] : expected) near(composite({0.25f,0.25f,0.25f,1}, {0.75f,0.75f,0.75f,1}, mode, 1, true)[0], value);
}
void testDirectionalWarpAndStripes() {
    auto doc = document({{"source", {{"op", "gradient"}}}, {"control", {{"op", "constant"}, {"value", 0}}}, {"result", {{"op", "directional_warp"}, {"input", "source"}, {"intensity", "control"}, {"strength", 0.125}, {"edge", "clamp"}}}}, "result", 64);
    auto original = node({{"op", "gradient"}}, 64);
    check(run(doc)->pixels == original->pixels, "black control is identity");
    doc["nodes"]["control"]["value"] = 1;
    auto white = run(doc); near(white->get(20, 20)[0], original->get(28, 20)[0]);
    doc["nodes"]["result"].erase("intensity");
    check(run(doc)->pixels == white->pixels, "omitted intensity is uniform white");
    doc["nodes"]["result"]["intensity"] = "control";
    doc["nodes"]["control"]["value"] = 0.5;
    doc["nodes"]["result"]["midlevel"] = 0.5;
    check(run(doc)->pixels == original->pixels, "midgray can be neutral");
    doc["nodes"]["control"]["value"] = 0;
    near(run(doc)->get(20, 20)[0], original->get(16, 20)[0]);
    doc["nodes"]["result"]["strength"] = -0.125;
    near(run(doc)->get(20, 20)[0], original->get(24, 20)[0]);
    doc["nodes"]["result"]["midlevel"] = 0;
    doc["nodes"]["result"]["strength"] = 0.125;
    doc["nodes"]["control"]["value"] = {0, 0.25, 1, 0.5};
    for (const auto& [channel, amount] : std::map<std::string, float>{{"r", 0}, {"g", 0.25}, {"b", 1}, {"a", 0.5}, {"luminance", 0.25f * 0.7152f + 0.0722f}}) {
        doc["nodes"]["result"]["channel"] = channel;
        near(run(doc)->get(20, 20)[0], original->get(20, 20)[0] + amount * 0.125f);
    }
    doc["nodes"]["result"]["channel"] = "b";
    doc["nodes"]["source"]["angle"] = 90;
    doc["nodes"]["result"]["angle"] = 90;
    near(run(doc)->get(20, 20)[0], 28.5f / 64);
    doc["nodes"]["result"]["angle"] = 270;
    near(run(doc)->get(20, 20)[0], 12.5f / 64);
    doc["nodes"]["source"] = {{"op", "shape"}, {"color", "#ff880080"}};
    auto warped = run(doc, 1); check(warped->kind == Kind::Color && warped->pixels == run(doc, 4)->pixels, "color/alpha and thread determinism");
    auto bad = doc; bad["nodes"]["result"]["intensity"] = "missing"; fails(bad, "unknown node");
    bad = doc; bad["nodes"]["result"]["channel"] = "typo"; fails(bad, "unsupported value");
    bad = doc; bad["nodes"]["result"]["midlevel"] = -1; fails(bad, "range");
    for (std::string wave : {"sine", "triangle", "saw", "square"}) {
        auto pattern = node({{"op", "stripes"}, {"count", 4}, {"wave", wave}}, 64);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 48; ++x) near(pattern->get(x, y)[0], pattern->get(x + 16, y)[0]);
        auto averaged = node({{"op", "stripes"}, {"count", 4096}, {"wave", wave}, {"duty", 0.25}}, 1);
        near(averaged->get(0, 0)[0], wave == "square" ? 0.25f : 0.5f);
        auto phased = node({{"op", "stripes"}, {"count", 4}, {"wave", wave}, {"phase", -1}}, 64);
        for (size_t i = 0; i < pattern->pixels.size(); ++i) near(phased->pixels[i], pattern->pixels[i]);
    }
    auto saw = node({{"op", "stripes"}, {"count", 1}, {"wave", "saw"}}, 64);
    near(saw->get(20, 10)[0], 20.5f / 64);
    auto horizontal = node({{"op", "stripes"}, {"count", 1}, {"wave", "saw"}, {"angle", 90}}, 64);
    near(horizontal->get(20, 10)[0], 10.5f / 64);
    fails(document({{"result", {{"op", "stripes"}, {"count", 0}}}}), "range");
    // A grayscale PNG can directly control the warp, without color-space conversion.
    auto dir = std::filesystem::temp_directory_path() / ("texutil-warp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto control = node({{"op", "constant"}, {"value", 0.75}}, 64);
    writeImage(dir / "control.png", *control, "png", 16, false, false, {0,0,0,1});
    doc["nodes"]["source"] = {{"op", "gradient"}};
    doc["nodes"]["control"] = {{"op", "image"}, {"path", (dir / "control.png").string()}, {"kind", "scalar"}};
    doc["nodes"]["result"]["angle"] = 0;
    doc["nodes"]["result"]["channel"] = "luminance";
    near(run(doc)->get(20, 20)[0], 20.5f / 64 + 0.75f * 0.125f, 1e-5f);
    std::filesystem::remove_all(dir);
}
}
int main() {
    int failed = 0;
    for (const auto& test : std::vector<std::pair<std::string, std::function<void()>>>{{"graph validation and liveness", testGraph}, {"normals and ramps", testNormalsAndRamps}, {"noise determinism and tiling", testNoise}, {"transforms and placement", testSpatial}, {"format precision and alpha", testFormats}, {"remaining nodes and blend modes", testRemainingNodes}, {"directional warp and antialiased stripes", testDirectionalWarpAndStripes}}) {
        std::cout << "RUN " << test.first << std::endl;
        try { test.second(); std::cout << "PASS " << test.first << std::endl; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << test.first << ": " << e.what() << '\n'; }
    }
    return failed ? 1 : 0;
}
