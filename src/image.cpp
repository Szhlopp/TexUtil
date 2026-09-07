#include "texutil/texutil.hpp"
#include <png.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace tex {
float clamp(float x) { return std::max(0.0f, std::min(1.0f, x)); }
float luminance(Pixel p) { return p[0] * 0.2126f + p[1] * 0.7152f + p[2] * 0.0722f; }
float toLinear(float x) { return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); }
float toSrgb(float x) { return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f; }
Pixel color(const Json& j) {
    Pixel p{0, 0, 0, 1};
    if (j.is_number()) p = {j.get<float>(), j.get<float>(), j.get<float>(), 1};
    else if (j.is_array() && (j.size() == 3 || j.size() == 4)) {
        for (size_t i = 0; i < j.size(); ++i) { if (!j[i].is_number()) throw std::runtime_error("color components must be numbers"); p[i] = j[i].get<float>(); }
    } else if (j.is_string()) {
        std::string s = j;
        if ((s.size() != 7 && s.size() != 9) || s[0] != '#' || s.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos) throw std::runtime_error("color must be #RRGGBB or #RRGGBBAA");
        for (size_t i = 0; i < (s.size() - 1) / 2; ++i) p[i] = static_cast<float>(std::stoul(s.substr(1 + 2 * i, 2), nullptr, 16)) / 255;
        for (int i = 0; i < 3; ++i) p[i] = toLinear(p[i]);
    } else throw std::runtime_error("invalid color");
    for (float x : p) if (!std::isfinite(x) || std::abs(x) > 10000) throw std::runtime_error("color must contain finite bounded values");
    if (p[3] < 0 || p[3] > 1) throw std::runtime_error("alpha must be 0..1");
    return p;
}
Pixel mix(Pixel a, Pixel b, float t) { for (int i = 0; i < 4; ++i) a[i] += (b[i] - a[i]) * t; return a; }
Pixel composite(Pixel a, Pixel b, const std::string& mode, float opacity, bool scalar) {
    Pixel blend = b;
    for (int c = 0; c < 3; ++c) {
        float x = a[c], y = b[c];
        if (mode == "multiply") blend[c] = x * y;
        else if (mode == "add") blend[c] = x + y;
        else if (mode == "subtract") blend[c] = x - y;
        else if (mode == "screen") blend[c] = 1 - (1 - x) * (1 - y);
        else if (mode == "overlay") blend[c] = x <= 0.5f ? 2 * x * y : 1 - 2 * (1 - x) * (1 - y);
        else if (mode == "min") blend[c] = std::min(x, y);
        else if (mode == "max") blend[c] = std::max(x, y);
        else if (mode == "difference") blend[c] = std::abs(x - y);
    }
    if (scalar) { auto p = mix(a, blend, opacity); p[3] = 1; return p; }
    if (mode == "mix") {
        for (int c = 0; c < 3; ++c) { a[c] *= a[3]; b[c] *= b[3]; }
        auto p = mix(a, b, opacity);
        for (int c = 0; c < 3; ++c) p[c] = p[3] > 1e-8f ? p[c] / p[3] : 0;
        return p;
    }
    float aa = clamp(a[3]), ab = clamp(b[3]) * opacity, alpha = ab + aa * (1 - ab);
    Pixel p{0, 0, 0, alpha};
    for (int c = 0; c < 3; ++c) p[c] = alpha > 1e-8f ? ((1 - ab) * aa * a[c] + (1 - aa) * ab * b[c] + aa * ab * blend[c]) / alpha : 0;
    return p;
}
Image::Image(int w, int h, Kind k, std::shared_ptr<Memory> m) : width(w), height(h), channels(k == Kind::Scalar ? 1 : 4), kind(k), memory(std::move(m)) {
    size_t bytes = static_cast<size_t>(w) * h * channels * sizeof(float);
    if (bytes > memory->limit || memory->current > memory->limit - bytes) throw std::runtime_error("float-buffer memory budget exceeded; use --memory MB or a smaller --size");
    pixels.resize(bytes / sizeof(float));
    memory->current += bytes; memory->peak = std::max(memory->peak, memory->current);
}
Image::~Image() { memory->current -= pixels.size() * sizeof(float); }
Pixel Image::get(int x, int y) const {
    auto i = (static_cast<size_t>(y) * width + x) * channels;
    if (channels == 1) return {pixels[i], pixels[i], pixels[i], 1};
    return {pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]};
}
void Image::set(int x, int y, Pixel p) {
    auto i = (static_cast<size_t>(y) * width + x) * channels;
    for (int c = 0; c < channels; ++c) pixels[i + c] = p[c];
}
Pixel Image::sample(float u, float v, const std::string& edge) const {
    if (!std::isfinite(u) || !std::isfinite(v)) throw std::runtime_error("non-finite sampling coordinates");
    // Reduce coordinates before float-to-int conversion, including extreme transforms.
    if (edge == "repeat") { u -= std::floor(u); v -= std::floor(v); }
    else if (edge == "clamp") { u = clamp(u); v = clamp(v); }
    else if (u < -1.0f / width || u > 1 + 1.0f / width || v < -1.0f / height || v > 1 + 1.0f / height) return {0, 0, 0, 0};
    float x = u * width - 0.5f, y = v * height - 0.5f;
    int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    auto fetch = [&](int px, int py) {
        if (edge == "repeat") { px = (px % width + width) % width; py = (py % height + height) % height; }
        else if (edge == "transparent" && (px < 0 || py < 0 || px >= width || py >= height)) return Pixel{0, 0, 0, 0};
        else { px = std::clamp(px, 0, width - 1); py = std::clamp(py, 0, height - 1); }
        Pixel p = get(px, py);
        if (kind == Kind::Color) for (int c = 0; c < 3; ++c) p[c] *= p[3];
        return p;
    };
    Pixel p = mix(mix(fetch(ix, iy), fetch(ix + 1, iy), x - ix), mix(fetch(ix, iy + 1), fetch(ix + 1, iy + 1), x - ix), y - iy);
    if (kind == Kind::Color) for (int c = 0; c < 3; ++c) p[c] = p[3] > 1e-8f ? p[c] / p[3] : 0;
    return p;
}

Workers::Workers(unsigned count) {
    count = count ? count : std::min(16u, std::max(1u, std::thread::hardware_concurrency()));
    if (count > 256) throw std::runtime_error("threads must be 1..256");
    try { for (unsigned i = 1; i < count; ++i) threads_.emplace_back([this] { work(); }); }
    catch (...) { { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; } ready_.notify_all(); for (auto& t : threads_) t.join(); throw; }
}
Workers::~Workers() { { std::lock_guard<std::mutex> lock(mutex_); stop_ = true; } ready_.notify_all(); for (auto& t : threads_) t.join(); }
void Workers::work() {
    size_t seen = 0;
    for (;;) {
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [&] { return stop_ || generation_ != seen; });
        if (stop_) return;
        seen = generation_; lock.unlock();
        try { for (int y; (y = next_.fetch_add(1)) < rows_;) fn_(y); }
        catch (...) { std::lock_guard<std::mutex> errorLock(mutex_); if (!error_) error_ = std::current_exception(); }
        lock.lock(); if (--remaining_ == 0) done_.notify_one();
    }
}
void Workers::rows(int count, const std::function<void(int)>& fn) {
    if (threads_.empty() || count < 16) { for (int y = 0; y < count; ++y) fn(y); return; }
    { std::lock_guard<std::mutex> lock(mutex_); fn_ = fn; rows_ = count; next_ = 0; remaining_ = static_cast<int>(threads_.size()); error_ = nullptr; ++generation_; }
    ready_.notify_all();
    try { for (int y; (y = next_.fetch_add(1)) < count;) fn(y); }
    catch (...) { std::lock_guard<std::mutex> lock(mutex_); if (!error_) error_ = std::current_exception(); }
    std::unique_lock<std::mutex> lock(mutex_); done_.wait(lock, [&] { return remaining_ == 0; }); fn_ = {};
    if (error_) std::rethrow_exception(error_);
}

namespace {
struct PngFile {
    FILE* file = nullptr;
    png_structp png = nullptr;
    png_infop info = nullptr;
    bool write = false;
    ~PngFile() { if (png) { if (write) png_destroy_write_struct(&png, &info); else png_destroy_read_struct(&png, &info, nullptr); } if (file) std::fclose(file); }
};
}
ImagePtr readPng(const std::filesystem::path& path, Kind kind, bool srgb, std::shared_ptr<Memory> memory) {
    PngFile f; f.file = std::fopen(path.string().c_str(), "rb");
    if (!f.file) throw std::runtime_error("cannot open PNG '" + path.string() + "'");
    f.png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!f.png) throw std::runtime_error("PNG allocation failed");
    f.info = png_create_info_struct(f.png);
    if (!f.info) throw std::runtime_error("PNG allocation failed");
    std::vector<unsigned char> raw;
    std::vector<png_bytep> rows;
    ImagePtr result;
    if (setjmp(png_jmpbuf(f.png))) throw std::runtime_error("invalid PNG '" + path.string() + "'");
    png_init_io(f.png, f.file); png_read_info(f.png, f.info);
    int w = static_cast<int>(png_get_image_width(f.png, f.info)), h = static_cast<int>(png_get_image_height(f.png, f.info));
    if (w < 1 || h < 1 || w > 16384 || h > 16384) throw std::runtime_error("PNG dimensions must be 1..16384");
    int depth = png_get_bit_depth(f.png, f.info), type = png_get_color_type(f.png, f.info);
    if (type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(f.png);
    if (type == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(f.png);
    bool transparency = png_get_valid(f.png, f.info, PNG_INFO_tRNS);
    if (transparency) png_set_tRNS_to_alpha(f.png);
    if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(f.png);
    if (!(type & PNG_COLOR_MASK_ALPHA) && !transparency) png_set_add_alpha(f.png, depth == 16 ? 65535 : 255, PNG_FILLER_AFTER);
    png_set_interlace_handling(f.png); png_read_update_info(f.png, f.info);
    size_t rowBytes = png_get_rowbytes(f.png, f.info);
    result = std::make_shared<Image>(w, h, kind, memory);
    if (rowBytes * h > memory->limit - memory->current) throw std::runtime_error("PNG decode exceeds memory budget");
    raw.resize(rowBytes * h); rows.resize(h);
    for (int y = 0; y < h; ++y) rows[y] = raw.data() + y * rowBytes;
    png_read_image(f.png, rows.data()); png_read_end(f.png, nullptr);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        Pixel p{};
        for (int c = 0; c < 4; ++c) {
            size_t index = static_cast<size_t>(x * 4 + c) * (depth == 16 ? 2 : 1);
            p[c] = depth == 16 ? (rows[y][index] * 256 + rows[y][index + 1]) / 65535.0f : rows[y][index] / 255.0f;
        }
        if (kind == Kind::Color && srgb) for (int c = 0; c < 3; ++c) p[c] = toLinear(p[c]);
        if (kind == Kind::Scalar) p[0] = luminance(p);
        result->set(x, y, p);
    }
    return result;
}
void writeImage(const std::filesystem::path& path, const Image& image, const std::string& format, int bits, bool alpha, bool srgb, Pixel background) {
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    auto pixel = [&](int x, int y) {
        Pixel p = image.get(x, y);
        if (image.kind == Kind::Color && !alpha) { for (int c = 0; c < 3; ++c) p[c] = p[c] * clamp(p[3]) + background[c] * (1 - clamp(p[3])); p[3] = 1; }
        if (format == "pgm") { float value = luminance(p); p[0] = p[1] = p[2] = value; }
        if (image.kind == Kind::Color && srgb) for (int c = 0; c < 3; ++c) p[c] = toSrgb(p[c]);
        for (float v : p) if (!std::isfinite(v)) throw std::runtime_error("non-finite pixels in output '" + path.string() + "'");
        return p;
    };
    bool gray = (image.kind == Kind::Scalar && format != "ppm") || format == "pgm";
    if (format == "pfm") {
        std::ofstream file(path, std::ios::binary); if (!file) throw std::runtime_error("cannot write '" + path.string() + "'");
        uint16_t probe = 1; bool little = *reinterpret_cast<unsigned char*>(&probe) == 1;
        file << (gray ? "Pf\n" : "PF\n") << image.width << ' ' << image.height << '\n' << (little ? "-1.0\n" : "1.0\n");
        for (int y = image.height - 1; y >= 0; --y) for (int x = 0; x < image.width; ++x) { Pixel p = pixel(x, y); file.write(reinterpret_cast<const char*>(p.data()), (gray ? 1 : 3) * sizeof(float)); }
        file.close(); if (!file) throw std::runtime_error("failed writing '" + path.string() + "'"); return;
    }
    int channels = (gray ? 1 : 3) + (alpha && format == "png" ? 1 : 0);
    if (format == "ppm") channels = 3;
    std::vector<unsigned char> row(static_cast<size_t>(image.width) * channels * (bits / 8));
    auto makeRow = [&](int y) {
        for (int x = 0; x < image.width; ++x) {
            Pixel p = pixel(x, y); if (gray) p[0] = luminance(p);
            for (int c = 0; c < channels; ++c) {
                int source = ((gray && c == 1) || (!gray && c == 3)) ? 3 : c;
                unsigned v = static_cast<unsigned>(std::lround(clamp(p[source]) * (bits == 16 ? 65535.0f : 255.0f)));
                size_t i = static_cast<size_t>(x * channels + c) * (bits / 8);
                if (bits == 16) { row[i] = v >> 8; row[i + 1] = v & 255; } else row[i] = v;
            }
        }
    };
    if (format == "png") {
        PngFile f; f.write = true; f.file = std::fopen(path.string().c_str(), "wb");
        if (!f.file) throw std::runtime_error("cannot write '" + path.string() + "'");
        f.png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
        if (!f.png) throw std::runtime_error("PNG allocation failed");
        f.info = png_create_info_struct(f.png);
        if (!f.info) throw std::runtime_error("PNG allocation failed");
        if (setjmp(png_jmpbuf(f.png))) throw std::runtime_error("PNG write failed '" + path.string() + "'");
        png_init_io(f.png, f.file);
        int type = gray ? (alpha ? PNG_COLOR_TYPE_GRAY_ALPHA : PNG_COLOR_TYPE_GRAY) : (alpha ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB);
        png_set_IHDR(f.png, f.info, image.width, image.height, bits, type, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
        if (image.kind == Kind::Color && srgb) png_set_sRGB(f.png, f.info, PNG_sRGB_INTENT_PERCEPTUAL);
        png_set_compression_level(f.png, 3); png_write_info(f.png, f.info);
        for (int y = 0; y < image.height; ++y) { makeRow(y); png_write_row(f.png, row.data()); }
        png_write_end(f.png, nullptr);
        if (std::fflush(f.file) != 0) throw std::runtime_error("failed flushing '" + path.string() + "'");
    } else {
        std::ofstream file(path, std::ios::binary); if (!file) throw std::runtime_error("cannot write '" + path.string() + "'");
        file << (format == "pgm" ? "P5\n" : "P6\n") << image.width << ' ' << image.height << '\n' << (bits == 16 ? 65535 : 255) << '\n';
        for (int y = 0; y < image.height; ++y) { makeRow(y); file.write(reinterpret_cast<const char*>(row.data()), row.size()); }
        file.close(); if (!file) throw std::runtime_error("failed writing '" + path.string() + "'");
    }
}
}
