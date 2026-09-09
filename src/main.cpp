#include "texutil/texutil.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void help() {
    std::cout << R"(TexUtil 0.1.0: JSON texture graphs

  texutil material.json [--out DIR] [--size N|WxH] [--seed N]
                       [--threads N] [--memory MB] [--stats]
  texutil render material.json [same options]
  texutil validate material.json [--size N|WxH] [--seed N] [--json]
  texutil nodes [--json]
  texutil presets [--json]
  texutil materialx [--json]       # Standard Surface input types and defaults
  texutil describe NODE

A file of '-' reads JSON from stdin; image/import paths then use the current directory.
Render --json prints machine-readable statistics. --stats prints node timings.
Defaults: output in current directory, up to 16 CPU threads, 1024 MB float buffers.
PNG (8/16-bit), PPM, PGM, PFM outputs. Native labeled sheets: output type "sheet".
MaterialX materials: output type "materialx". See docs/MATERIALX.md.
Reuse JSON graphs with named "imports" and alias.node references. See docs/IMPORTS.md.
See README.md, docs/NODES.md and docs/SHEETS.md.
)";
}
long long integer(const std::string& text, long long min, long long max, const std::string& name) {
    size_t end = 0; long long value;
    try { value = std::stoll(text, &end); } catch (...) { throw std::runtime_error(name + " requires an integer"); }
    if (end != text.size() || value < min || value > max) throw std::runtime_error(name + " is out of range");
    return value;
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") { help(); return 0; }
        if (std::string(argv[1]) == "--version") { std::cout << "texutil 0.1.0\n"; return 0; }
        std::string command = "render", filename, node;
        int position = 1; std::string first = argv[position];
        if (first == "render" || first == "validate" || first == "nodes" || first == "describe" || first == "presets" || first == "materialx") { command = first; ++position; }
        if (command == "render" || command == "validate") { if (position >= argc) throw std::runtime_error("missing JSON file"); filename = argv[position++]; }
        if (command == "describe") { if (position >= argc) throw std::runtime_error("missing node operation"); node = argv[position++]; }
        tex::Options options; bool json = false, stats = false;
        for (; position < argc; ++position) {
            std::string arg = argv[position];
            if (arg == "--json") { json = true; continue; }
            if (arg == "--stats" && command == "render") { stats = true; continue; }
            bool valued = arg == "--out" || arg == "--size" || arg == "--seed" || arg == "--threads" || arg == "--memory";
            if (!valued || (command != "render" && command != "validate")) throw std::runtime_error("unknown option '" + arg + "'");
            if (++position >= argc) throw std::runtime_error("missing value for " + arg);
            std::string value = argv[position];
            if (arg == "--out") options.out = value;
            if (arg == "--seed") options.seed = static_cast<int>(integer(value, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), arg));
            if (arg == "--threads") options.threads = static_cast<unsigned>(integer(value, 1, 256, arg));
            if (arg == "--memory") options.memoryMb = static_cast<size_t>(integer(value, 1, 1048576, arg));
            if (arg == "--size") {
                auto split = value.find('x');
                options.width = static_cast<int>(integer(value.substr(0, split), 1, 16384, arg));
                options.height = split == std::string::npos ? options.width : static_cast<int>(integer(value.substr(split + 1), 1, 16384, arg));
            }
        }
        if (command == "materialx") { std::cout << tex::materialXInputs().dump(2) << '\n'; return 0; }
        if (command == "presets") {
            auto names = tex::presetNames(); if (json) std::cout << names.dump(2) << '\n'; else for (const auto& name : names) std::cout << name.get<std::string>() << '\n'; return 0;
        }
        if (command == "nodes") {
            auto catalog = tex::catalog();
            if (json) std::cout << catalog.dump(2) << '\n';
            else for (auto it = catalog.begin(); it != catalog.end(); ++it) std::cout << it.key() << "  " << it.value()["description"].get<std::string>() << '\n';
            return 0;
        }
        if (command == "describe") {
            auto catalog = tex::catalog(); if (!catalog.contains(node)) throw std::runtime_error("unknown node '" + node + "'");
            std::cout << catalog[node].dump(2) << '\n'; return 0;
        }
        tex::Json doc; std::filesystem::path base = std::filesystem::current_path();
        if (filename == "-") doc = tex::Json::parse(std::cin);
        else { std::ifstream file(filename); if (!file) throw std::runtime_error("cannot open '" + filename + "'"); doc = tex::Json::parse(file); base = std::filesystem::absolute(filename).parent_path(); }
        tex::Graph graph(doc, base, options);
        if (command == "validate") {
            auto result = graph.summary();
            if (json) std::cout << result.dump(2) << '\n';
            else std::cout << "Valid: " << result["reachable"] << '/' << result["nodes"] << " nodes reachable, " << result["outputs"] << " outputs\n";
            return 0;
        }
        auto result = graph.render();
        if (json) std::cout << result.dump(2) << '\n';
        else {
            for (const auto& file : result["files"]) std::cout << file.get<std::string>() << '\n';
            std::cerr << "Rendered " << result["size"][0] << 'x' << result["size"][1] << " in " << result["total_ms"] << " ms; peak float buffers " << result["peak_buffer_mb"] << " MB\n";
            if (stats) for (const auto& timing : result["timings"]) std::cerr << timing["node"].get<std::string>() << " (" << timing["op"].get<std::string>() << "): " << timing["ms"] << " ms\n";
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << "texutil: " << error.what() << '\n'; return 1; }
}
