#pragma once
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace tex {
using Json = nlohmann::json;
using Pixel = std::array<float, 4>;
enum class Kind { Scalar, Color, Normal };
struct Memory {
    size_t limit = 1024ull * 1024 * 1024;
    size_t current = 0;
    size_t peak = 0;
};
struct Image {
    int width, height, channels;
    Kind kind;
    std::vector<float> pixels;
    std::shared_ptr<Memory> memory;
    Image(int w, int h, Kind k, std::shared_ptr<Memory> m);
    ~Image();
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Pixel get(int x, int y) const;
    void set(int x, int y, Pixel p);
    Pixel sample(float u, float v, const std::string& edge) const;
};
using ImagePtr = std::shared_ptr<Image>;
float clamp(float x);
float luminance(Pixel p);
float toLinear(float x);
float toSrgb(float x);
Pixel color(const Json& j);
Pixel mix(Pixel a, Pixel b, float t);
Pixel composite(Pixel a, Pixel b, const std::string& mode, float opacity, bool scalar);
ImagePtr readPng(const std::filesystem::path& path, Kind kind, bool srgb, std::shared_ptr<Memory> memory);
void writeImage(const std::filesystem::path& path, const Image& image, const std::string& format, int bits, bool alpha, bool srgb, Pixel background);

class Workers {
public:
    explicit Workers(unsigned count);
    ~Workers();
    void rows(int count, const std::function<void(int)>& fn);
    unsigned count() const { return static_cast<unsigned>(threads_.size()) + 1; }
private:
    void work();
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable ready_, done_;
    std::function<void(int)> fn_;
    std::atomic<int> next_{0};
    int rows_ = 0, remaining_ = 0;
    size_t generation_ = 0;
    bool stop_ = false;
    std::exception_ptr error_;
};

void addAdvancedCatalog(Json& result);
Json expandPresets(Json nodes);
Json presetNames();
Json catalog();
Json normalizedNode(const std::string& id, const Json& node);
std::vector<std::string> dependencies(const Json& node);

struct Options {
    int width = 0, height = 0;
    std::optional<int> seed;
    unsigned threads = 0;
    size_t memoryMb = 1024;
    std::filesystem::path out = ".";
};
struct Output {
    std::string name, node, format;
    int bits = 8;
    bool alpha = false;
    bool srgb = true;
};
struct Context {
    int width, height, seed;
    bool tile;
    std::filesystem::path base;
    Workers& workers;
    std::shared_ptr<Memory> memory;
};
ImagePtr execute(const Json& node, const std::map<std::string, ImagePtr>& inputs, Context& context);

ImagePtr advanced(const Json& node, const std::map<std::string, ImagePtr>& inputs, Context& context);
ImagePtr erode(const Json& node, const ImagePtr& input, const ImagePtr& mask, Context& context);

class Graph {
public:
    Graph(Json document, std::filesystem::path base, Options options = {});
    Json render(const std::function<void(const Output&, const ImagePtr&)>& sink = {});
    Json summary() const;
private:
    std::map<std::string, Json> nodes_;
    std::vector<std::string> order_;
    std::vector<Output> outputs_;
    std::filesystem::path base_;
    Options options_;
    int width_ = 512, height_ = 512, seed_ = 42;
    bool tile_ = false;
    Pixel background_{0, 0, 0, 1};
};
}
