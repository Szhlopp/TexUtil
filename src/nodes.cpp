#include "texutil/texutil.hpp"
#include <FastNoise/FastNoise.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tex {
namespace {
constexpr float pi = 3.14159265358979323846f;
using Pair = std::array<float, 2>;
Pair pair(const Json& j) { return {j[0].get<float>(), j[1].get<float>()}; }
float radians(float degrees) { return degrees * pi / 180; }
float smooth(float x) { x = clamp(x); return x * x * (3 - 2 * x); }
Pixel scalar(float x) { return {x, x, x, 1}; }
ImagePtr create(Context& c, Kind kind) { return std::make_shared<Image>(c.width, c.height, kind, c.memory); }

template<class T> FastNoise::SmartNode<> source(float scale) { auto n = FastNoise::New<T>(); n->SetScale(1 / scale); return n; }
FastNoise::SmartNode<> noiseSource(const Json& n) {
    std::string op = n["op"]; float scale = n["scale"];
    if (op == "simplex" || op == "clouds") return source<FastNoise::Simplex>(scale);
    if (op == "perlin") return source<FastNoise::Perlin>(scale);
    if (op == "value") return source<FastNoise::Value>(scale);
    if (op == "white") { auto n = FastNoise::New<FastNoise::DomainScale>(); n->SetSource(FastNoise::New<FastNoise::White>()); n->SetScaling(scale); return n; }
    FastNoise::DistanceFunction distance = FastNoise::DistanceFunction::Euclidean;
    std::string metric = n["distance"];
    if (metric == "squared") distance = FastNoise::DistanceFunction::EuclideanSquared;
    if (metric == "manhattan") distance = FastNoise::DistanceFunction::Manhattan;
    if (metric == "hybrid") distance = FastNoise::DistanceFunction::Hybrid;
    if (n["feature"] == "cells") {
        auto v = FastNoise::New<FastNoise::CellularValue>(); v->SetScale(1 / scale); v->SetDistanceFunction(distance); v->SetGridJitter(n["jitter"].get<float>()); return v;
    }
    auto v = FastNoise::New<FastNoise::CellularDistance>(); v->SetScale(1 / scale); v->SetDistanceFunction(distance); v->SetGridJitter(n["jitter"].get<float>());
    if (n["feature"] == "edges") { v->SetDistanceIndex0(1); v->SetDistanceIndex1(0); v->SetReturnType(FastNoise::CellularDistance::ReturnType::Index0Sub1); }
    return v;
}
ImagePtr noise(const Json& n, Context& c) {
    auto gen = noiseSource(n); std::string fractal = n["fractal"];
    float amplitude = 1;
    if (fractal != "none") {
        FastNoise::SmartNode<FastNoise::Fractal<>> f;
        if (fractal == "fbm") f = FastNoise::New<FastNoise::FractalFBm>(); else f = FastNoise::New<FastNoise::FractalRidged>();
        f->SetSource(gen); f->SetOctaveCount(n["octaves"]); f->SetGain(n["gain"].get<float>()); f->SetLacunarity(n["lacunarity"]);
        gen = f;
        float gain = n["gain"], weight = 1;
        for (int i = 1; i < n["octaves"].get<int>(); ++i) { weight *= gain; amplitude += weight; }
    }
    auto out = create(c, Kind::Scalar); int seed = n.value("seed", c.seed);
    float stepX = 1.0f / c.width, stepY = 1.0f / c.height;
    // A 4D torus gives periodic sampling without blending borders. Position-array
    // batches let each worker generate a row while preserving the full image period.
    if (n.value("tile", c.tile)) {
        std::vector<float> xs(c.width), zs(c.width), zeros(c.width, 0);
        for (int x = 0; x < c.width; ++x) {
            float angle = 2 * pi * (x + 0.5f) / c.width;
            xs[x] = std::cos(angle) / (2 * pi); zs[x] = std::sin(angle) / (2 * pi);
        }
        c.workers.rows(c.height, [&](int y) {
            float angle = 2 * pi * (y + 0.5f) / c.height;
            gen->GenPositionArray4D(out->pixels.data() + static_cast<size_t>(y) * c.width, c.width, xs.data(), zeros.data(), zs.data(), zeros.data(), 0, std::cos(angle) / (2 * pi), 0, std::sin(angle) / (2 * pi), seed);
        });
    }
    else c.workers.rows(c.height, [&](int y) { gen->GenUniformGrid2D(out->pixels.data() + static_cast<size_t>(y) * c.width, 0.5f * stepX, (y + 0.5f) * stepY, c.width, 1, stepX, stepY, seed); });
    c.workers.rows(c.height, [&](int y) { for (int x = 0; x < c.width; ++x) { auto& v = out->pixels[static_cast<size_t>(y) * c.width + x]; v = clamp(0.5f + 0.5f * v / amplitude); } });
    return out;
}

struct Placement { float x, y, sx, sy, cs, sn, opacity; int left, right, top, bottom; };
ImagePtr stamp(const Json& n, const ImagePtr& input, Context& c) {
    auto out = create(c, input->kind); std::vector<Placement> placements;
    bool wrap = n["wrap"]; auto size = pair(n["size"]); float angle = n["angle"], opacity = n["opacity"];
    auto add = [&](float x, float y, Pair sz, float degrees, float strength) {
        float a = radians(degrees), cs = std::cos(a), sn = std::sin(a);
        float ex = (std::abs(cs) * sz[0] + std::abs(sn) * sz[1]) * 0.5f, ey = (std::abs(sn) * sz[0] + std::abs(cs) * sz[1]) * 0.5f;
        if (wrap) { x -= std::floor(x); y -= std::floor(y); }
        for (int oy = wrap ? -1 : 0; oy <= (wrap ? 1 : 0); ++oy) for (int ox = wrap ? -1 : 0; ox <= (wrap ? 1 : 0); ++ox) {
            float px = x + ox, py = y + oy;
            int l = std::max(0, static_cast<int>(std::floor((px - ex) * c.width))), r = std::min(c.width, static_cast<int>(std::ceil((px + ex) * c.width)));
            int t = std::max(0, static_cast<int>(std::floor((py - ey) * c.height))), b = std::min(c.height, static_cast<int>(std::ceil((py + ey) * c.height)));
            if (l < r && t < b) placements.push_back({px, py, sz[0], sz[1], cs, sn, strength, l, r, t, b});
        }
    };
    std::string op = n["op"];
    if (op == "stamp") for (const auto& p : n["points"]) { auto position = pair(p["position"]); add(position[0], position[1], pair(p["size"]), p["angle"], p["opacity"]); }
    if (op == "array") {
        int nx = n["count"][0], ny = n["count"][1]; float jitter = n["jitter"], rotation = n["rotation_jitter"];
        uint32_t state = static_cast<uint32_t>(n.value("seed", c.seed));
        auto random = [&]() { state += 0x9e3779b9u; uint32_t x = state; x = (x ^ (x >> 16)) * 0x21f0aaadu; x = (x ^ (x >> 15)) * 0x735a2d97u; x ^= x >> 15; return static_cast<float>(x >> 8) / 16777216.0f; };
        for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
            float px = (x + 0.5f + (random() - 0.5f) * jitter) / nx;
            float py = (y + 0.5f + (random() - 0.5f) * jitter) / ny;
            float rotationAngle = angle + (random() * 2 - 1) * rotation;
            add(px, py, size, rotationAngle, opacity);
        }
    }
    if (op == "radial") {
        int count = n["count"]; auto center = pair(n["center"]); float radius = n["radius"], start = n["start"], sweep = n["sweep"]; bool orient = n["orient"];
        for (int i = 0; i < count; ++i) { float deg = start + sweep * i / count, a = radians(deg); add(center[0] + std::cos(a) * radius, center[1] + std::sin(a) * radius, size, angle + (orient ? deg : 0), opacity); }
    }
    // Row bands avoid scanning every stamp for every pixel, while preserving order.
    std::vector<std::vector<size_t>> bands((c.height + 31) / 32); size_t entries = 0;
    for (size_t i = 0; i < placements.size(); ++i) for (int band = placements[i].top / 32; band <= (placements[i].bottom - 1) / 32; ++band) {
        if (++entries > 4000000) throw std::runtime_error("stamp overlap is too large; reduce count or size");
        bands[band].push_back(i);
    }
    std::string mode = n["mode"];
    c.workers.rows(c.height, [&](int y) {
        float v = (y + 0.5f) / c.height;
        for (auto index : bands[y / 32]) {
            const auto& p = placements[index]; if (y < p.top || y >= p.bottom) continue;
            for (int x = p.left; x < p.right; ++x) {
                float dx = (x + 0.5f) / c.width - p.x, dy = v - p.y;
                float u = (p.cs * dx + p.sn * dy) / p.sx + 0.5f, sv = (-p.sn * dx + p.cs * dy) / p.sy + 0.5f;
                if (u < 0 || u >= 1 || sv < 0 || sv >= 1) continue;
                auto b = input->sample(u, sv, "transparent");
                out->set(x, y, composite(out->get(x, y), b, mode, p.opacity, input->channels == 1));
            }
        }
    });
    return out;
}
ImagePtr blur(const Json& n, ImagePtr input, Context& c) {
    int radius = n["radius"], passes = n["passes"]; if (!radius) return input;
    std::string edge = n["edge"];
    // Blur color in premultiplied alpha to prevent dark outlines on stamps.
    auto working = create(c, input->kind);
    c.workers.rows(c.height, [&](int y) { for (int x = 0; x < c.width; ++x) { auto p = input->get(x, y); if (input->kind == Kind::Color) for (int k = 0; k < 3; ++k) p[k] *= p[3]; working->set(x, y, p); } });
    auto temp = create(c, input->kind);
    auto fetch = [&](const Image& image, int x, int y) {
        if (edge == "repeat") { x = (x % c.width + c.width) % c.width; y = (y % c.height + c.height) % c.height; }
        else if (edge == "transparent" && (x < 0 || x >= c.width || y < 0 || y >= c.height)) return Pixel{0, 0, 0, 0};
        else { x = std::clamp(x, 0, c.width - 1); y = std::clamp(y, 0, c.height - 1); }
        return image.get(x, y);
    };
    auto pass = [&](bool horizontal) {
        c.workers.rows(horizontal ? c.height : c.width, [&](int row) {
            int length = horizontal ? c.width : c.height; std::array<double, 4> sum{};
            auto sample = [&](int position) { return fetch(*working, horizontal ? position : row, horizontal ? row : position); };
            for (int i = -radius; i <= radius; ++i) { auto p = sample(i); for (int k = 0; k < 4; ++k) sum[k] += p[k]; }
            for (int i = 0; i < length; ++i) {
                Pixel p{}; for (int k = 0; k < 4; ++k) p[k] = static_cast<float>(sum[k] / (2 * radius + 1));
                temp->set(horizontal ? i : row, horizontal ? row : i, p);
                auto before = sample(i - radius), after = sample(i + radius + 1);
                for (int k = 0; k < 4; ++k) sum[k] += static_cast<double>(after[k]) - before[k];
            }
        });
        std::swap(working, temp);
    };
    for (int p = 0; p < passes; ++p) { pass(true); pass(false); }
    if (input->kind == Kind::Color) c.workers.rows(c.height, [&](int y) { for (int x = 0; x < c.width; ++x) { auto p = working->get(x, y); for (int k = 0; k < 3; ++k) p[k] = p[3] > 1e-8f ? p[k] / p[3] : 0; working->set(x, y, p); } });
    return working;
}
}
ImagePtr execute(const Json& n, const std::map<std::string, ImagePtr>& inputs, Context& c) {
    std::string op = n["op"];
    auto input = [&](const std::string& key) { return inputs.at(n[key].get<std::string>()); };
    auto pixels = [&](Kind kind, const std::function<Pixel(int, int, float, float)>& fn) {
        auto out = create(c, kind);
        c.workers.rows(c.height, [&](int y) { for (int x = 0; x < c.width; ++x) out->set(x, y, fn(x, y, (x + 0.5f) / c.width, (y + 0.5f) / c.height)); });
        return out;
    };
    if (op == "simplex" || op == "perlin" || op == "value" || op == "white" || op == "clouds" || op == "voronoi") return noise(n, c);
    if (op == "constant") { auto p = color(n["value"]); return pixels(n["value"].is_number() ? Kind::Scalar : Kind::Color, [=](int, int, float, float) { return p; }); }
    if (op == "checker") { int nx = n["count"][0], ny = n["count"][1]; return pixels(Kind::Scalar, [=](int, int, float u, float v) { return scalar((static_cast<int>(u * nx) + static_cast<int>(v * ny)) % 2); }); }
    if (op == "stripes") {
        constexpr double tau = 6.2831853071795864769;
        const double count = n["count"], phase = n["phase"], angle = n["angle"].get<double>() * tau / 360;
        const double cs = std::cos(angle), sn = std::sin(angle), duty = n["duty"];
        const double footprint = count * (std::abs(cs) / c.width + std::abs(sn) / c.height);
        const std::string wave = n["wave"];
        // Integrate one-dimensional profiles over the projected pixel width. This
        // preserves coverage for thin stripes and averages unresolved periods.
        auto integral = [=](double q) {
            double periods = std::floor(q), t = q - periods;
            if (wave == "square") return periods * duty + std::min(t, duty);
            if (wave == "saw") return periods * 0.5 + t * t * 0.5;
            if (wave == "triangle") return periods * 0.5 + (t <= 0.5 ? t * t : 2 * t - t * t - 0.5);
            return q * 0.5 - std::sin(tau * t) / (2 * tau);
        };
        return pixels(Kind::Scalar, [=](int, int, float u, float v) {
            double q = (u * cs + v * sn) * count + phase;
            q -= std::floor(q);
            return scalar(clamp(static_cast<float>((integral(q + footprint * 0.5) - integral(q - footprint * 0.5)) / footprint)));
        });
    }
    if (op == "gradient") {
        auto center = pair(n["center"]); float a = radians(n["angle"]), cs = std::cos(a), sn = std::sin(a), radius = n["radius"]; bool reverse = n["reverse"]; std::string type = n["type"];
        return pixels(Kind::Scalar, [=](int, int, float u, float v) {
            float dx = u - center[0], dy = v - center[1], value = 0;
            if (type == "linear") value = clamp(dx * cs + dy * sn + 0.5f);
            else if (type == "radial") value = clamp(std::sqrt(dx * dx + dy * dy) / radius);
            else { value = (std::atan2(dy, dx) - a) / (2 * pi); value -= std::floor(value); }
            return scalar(reverse ? 1 - value : value);
        });
    }
    if (op == "shape") {
        auto center = pair(n["center"]), size = pair(n["size"]); float a = radians(n["angle"]), cs = std::cos(a), sn = std::sin(a), feather = n["softness"], thickness = n["thickness"];
        std::string type = n["type"]; bool colored = n.contains("color"); Pixel tint = colored ? color(n["color"]) : scalar(1);
        float aa = std::max(2.0f / (c.width * size[0]), 2.0f / (c.height * size[1]));
        return pixels(colored ? Kind::Color : Kind::Scalar, [=](int, int, float u, float v) {
            float dx = u - center[0], dy = v - center[1], x = 2 * (cs * dx + sn * dy) / size[0], y = 2 * (-sn * dx + cs * dy) / size[1];
            float distance = std::sqrt(x * x + y * y);
            if (type == "box") distance = std::max(std::abs(x), std::abs(y));
            if (type == "diamond") distance = std::abs(x) + std::abs(y);
            float value = smooth((1 - distance) / std::max(aa, feather));
            if (type == "ring") value *= smooth((distance - (1 - thickness)) / std::max(aa, feather));
            auto p = tint; if (colored) p[3] *= value; else p = scalar(value); return p;
        });
    }
    if (op == "image") {
        std::string kind = n["kind"]; auto sourceImage = readPng(c.base / n["path"].get<std::string>(), kind == "scalar" ? Kind::Scalar : (kind == "normal" ? Kind::Normal : Kind::Color), n["srgb"], c.memory);
        if (sourceImage->width == c.width && sourceImage->height == c.height) return sourceImage;
        return pixels(sourceImage->kind, [&](int, int, float u, float v) { return sourceImage->sample(u, v, "clamp"); });
    }
    if (op == "blend") {
        auto a = input("a"), b = input("b"); auto mask = n.contains("mask") ? input("mask") : ImagePtr{};
        Kind kind = a->kind == Kind::Color || b->kind == Kind::Color ? Kind::Color : (a->kind == Kind::Normal || b->kind == Kind::Normal ? Kind::Normal : Kind::Scalar);
        std::string mode = n["mode"]; float opacity = n["opacity"];
        return pixels(kind, [&](int x, int y, float, float) { return composite(a->get(x, y), b->get(x, y), mode, opacity * (mask ? clamp(luminance(mask->get(x, y))) : 1), kind == Kind::Scalar); });
    }
    if (op == "pack") {
        auto r = input("r"), g = input("g"), b = input("b"), a = n.contains("a") ? input("a") : ImagePtr{};
        return pixels(Kind::Normal, [&](int x, int y, float, float) { return Pixel{luminance(r->get(x, y)), luminance(g->get(x, y)), luminance(b->get(x, y)), a ? clamp(luminance(a->get(x, y))) : 1}; });
    }
    auto src = input("input");
    if (op == "stamp" || op == "array" || op == "radial") return stamp(n, src, c);
    if (op == "blur") return blur(n, src, c);
    if (op == "invert") return pixels(src->kind, [&](int x, int y, float, float) { auto p = src->get(x, y); for (int k = 0; k < 3; ++k) p[k] = 1 - p[k]; return p; });
    if (op == "grayscale") return pixels(Kind::Scalar, [&](int x, int y, float, float) { return scalar(luminance(src->get(x, y))); });
    if (op == "levels") {
        auto in = pair(n["in"]), range = pair(n["out"]); float exponent = 1 / n["gamma"].get<float>();
        return pixels(src->kind, [&](int x, int y, float, float) { auto p = src->get(x, y); for (int k = 0; k < 3; ++k) p[k] = range[0] + (range[1] - range[0]) * std::pow(clamp((p[k] - in[0]) / (in[1] - in[0])), exponent); return p; });
    }
    if (op == "threshold") {
        float threshold = n["value"], softness = n["softness"];
        return pixels(Kind::Scalar, [&](int x, int y, float, float) { float value = luminance(src->get(x, y)); return scalar(softness > 0 ? smooth((value - threshold) / softness + 0.5f) : (value >= threshold ? 1 : 0)); });
    }
    if (op == "transform") {
        auto scale = pair(n["scale"]), offset = pair(n["offset"]); float a = radians(n["angle"]), cs = std::cos(a), sn = std::sin(a); std::string edge = n["edge"];
        return pixels(src->kind, [&](int, int, float u, float v) { float dx = u - 0.5f - offset[0], dy = v - 0.5f - offset[1]; return src->sample((cs * dx + sn * dy) / scale[0] + 0.5f, (-sn * dx + cs * dy) / scale[1] + 0.5f, edge); });
    }
    if (op == "warp") {
        auto dx = input("x"), dy = input("y"); float strength = n["strength"]; std::string edge = n["edge"];
        return pixels(src->kind, [&](int x, int y, float u, float v) { return src->sample(u + (luminance(dx->get(x, y)) * 2 - 1) * strength, v + (luminance(dy->get(x, y)) * 2 - 1) * strength, edge); });
    }
    if (op == "directional_warp") {
        if (n["strength"].get<float>() == 0) return src;
        const auto control = n.contains("intensity") ? input("intensity") : ImagePtr{};
        const std::string channel = n["channel"], edge = n["edge"];
        const int component = channel == "r" ? 0 : channel == "g" ? 1 : channel == "b" ? 2 : channel == "a" ? 3 : -1;
        const float angle = radians(n["angle"]), dx = std::cos(angle) * n["strength"].get<float>(), dy = std::sin(angle) * n["strength"].get<float>(), midlevel = n["midlevel"];
        return pixels(src->kind, [&](int x, int y, float u, float v) {
            Pixel p = control ? control->get(x, y) : scalar(1);
            float amount = clamp(component < 0 ? luminance(p) : p[component]) - midlevel;
            return src->sample(u + dx * amount, v + dy * amount, edge);
        });
    }
    if (op == "ramp") {
        std::vector<std::pair<float, Pixel>> stops; for (const auto& stop : n["stops"]) stops.emplace_back(stop[0].get<float>(), color(stop[1])); std::string interpolation = n["interpolation"];
        return pixels(Kind::Color, [&](int x, int y, float, float) {
            float value = luminance(src->get(x, y));
            if (value <= stops.front().first) return stops.front().second;
            if (value >= stops.back().first) return stops.back().second;
            auto it = std::upper_bound(stops.begin(), stops.end(), value, [](float v, const auto& s) { return v < s.first; }); const auto& a = *(it - 1); const auto& b = *it;
            float t = (value - a.first) / (b.first - a.first); if (interpolation == "smooth") t = smooth(t); if (interpolation == "constant") t = 0;
            return mix(a.second, b.second, t);
        });
    }
    if (op == "normal") {
        float strength = n["strength"], sign = n["convention"] == "directx" ? 1.0f : -1.0f; std::string edge = n["edge"];
        return pixels(Kind::Normal, [&](int, int, float u, float v) {
            float dx = (luminance(src->sample(u + 1.0f / c.width, v, edge)) - luminance(src->sample(u - 1.0f / c.width, v, edge))) * c.width * 0.5f * strength;
            float dy = (luminance(src->sample(u, v + 1.0f / c.height, edge)) - luminance(src->sample(u, v - 1.0f / c.height, edge))) * c.height * 0.5f * strength;
            float length = std::sqrt(dx * dx + dy * dy + 1);
            return Pixel{0.5f - dx / length * 0.5f, 0.5f + sign * dy / length * 0.5f, 0.5f + 0.5f / length, 1};
        });
    }
    throw std::runtime_error("unimplemented op '" + op + "'");
}
}
