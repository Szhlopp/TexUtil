#include "texutil/texutil.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace tex {
namespace {
constexpr float pi = 3.14159265358979323846f;
ImagePtr make(Context& c, Kind kind = Kind::Scalar) { return std::make_shared<Image>(c.width, c.height, kind, c.memory); }
Pixel gray(float v) { return {v, v, v, 1}; }
float smooth(float x) { x = clamp(x); return x * x * (3 - 2 * x); }
uint32_t hash(uint32_t x) { x = (x ^ (x >> 16)) * 0x21f0aaadu; x = (x ^ (x >> 15)) * 0x735a2d97u; return x ^ (x >> 15); }
float unit(uint32_t x) { return static_cast<float>(hash(x) >> 8) / 16777216.0f; }
Pixel premultiply(Pixel p, Kind kind) { if (kind == Kind::Color) for (int k = 0; k < 3; ++k) p[k] *= p[3]; return p; }
Pixel unpremultiply(Pixel p, Kind kind) { if (kind == Kind::Color) for (int k = 0; k < 3; ++k) p[k] = p[3] > 1e-8f ? p[k] / p[3] : 0; return p; }
Pixel fetch(const Image& image, int x, int y, const std::string& edge) {
    if (edge == "repeat") { x = (x % image.width + image.width) % image.width; y = (y % image.height + image.height) % image.height; }
    else if (edge == "transparent" && (x < 0 || x >= image.width || y < 0 || y >= image.height)) return {0, 0, 0, 0};
    else { x = std::clamp(x, 0, image.width - 1); y = std::clamp(y, 0, image.height - 1); }
    return image.get(x, y);
}
// Lower envelope of squared-distance parabolas. Infinite sites never enter the envelope.
void edtLine(const std::vector<double>& f, std::vector<double>& d) {
    int n = static_cast<int>(f.size()), k = -1; std::vector<int> v(n); std::vector<double> z(n + 1);
    for (int q = 0; q < n; ++q) {
        if (!std::isfinite(f[q])) continue;
        double s = -INFINITY;
        while (k >= 0) { int p = v[k]; s = ((f[q] + double(q) * q) - (f[p] + double(p) * p)) / (2.0 * (q - p)); if (s > z[k]) break; --k; }
        ++k; v[k] = q; z[k] = k == 0 ? -INFINITY : s; z[k + 1] = INFINITY;
    }
    if (k < 0) { std::fill(d.begin(), d.end(), INFINITY); return; }
    k = 0;
    for (int q = 0; q < n; ++q) { while (z[k + 1] < q) ++k; double dx = q - v[k]; d[q] = dx * dx + f[v[k]]; }
}
ImagePtr distanceField(const ImagePtr& input, bool toForeground, float threshold, const std::string& edge, Context& c) {
    auto a = make(c), b = make(c); bool repeat = edge == "repeat";
    c.workers.rows(c.height, [&](int y) {
        int length = c.width * (repeat ? 3 : 1); std::vector<double> f(length), d(length);
        for (int x = 0; x < length; ++x) f[x] = (luminance(input->get(x % c.width, y)) >= threshold) == toForeground ? 0 : INFINITY;
        edtLine(f, d); for (int x = 0; x < c.width; ++x) a->set(x, y, gray(static_cast<float>(d[x + (repeat ? c.width : 0)])));
    });
    c.workers.rows(c.width, [&](int x) {
        int length = c.height * (repeat ? 3 : 1); std::vector<double> f(length), d(length);
        for (int y = 0; y < length; ++y) f[y] = a->get(x, y % c.height)[0];
        edtLine(f, d);
        for (int y = 0; y < c.height; ++y) {
            double value = std::sqrt(d[y + (repeat ? c.height : 0)]);
            if (edge == "transparent" && !toForeground) value = std::min(value, double(std::min({x + 1, c.width - x, y + 1, c.height - y})));
            b->set(x, y, gray(static_cast<float>(value)));
        }
    });
    return b;
}
}
ImagePtr advanced(const Json& n, const std::map<std::string, ImagePtr>& inputs, Context& c) {
    std::string op = n["op"];
    auto pixels = [&](Kind kind, const std::function<Pixel(int, int)>& fn) { auto out = make(c, kind); c.workers.rows(c.height, [&](int y) { for (int x = 0; x < c.width; ++x) out->set(x, y, fn(x, y)); }); return out; };
    if (op == "gaussian_noise") {
        uint32_t seed = static_cast<uint32_t>(n.value("seed", c.seed)); float mean = n["mean"], deviation = n["deviation"];
        return pixels(Kind::Scalar, [&](int x, int y) { uint32_t i = static_cast<uint32_t>(y * c.width + x); float u = std::max(1e-7f, unit(i * 2 + seed)), v = unit(i * 2 + seed + 1); return gray(clamp(mean + deviation * std::sqrt(-2 * std::log(u)) * std::cos(2 * pi * v))); });
    }
    if (op == "blue_noise") {
        int count = n["count"], candidates = n["candidates"]; float radius = n["radius"]; uint32_t state = static_cast<uint32_t>(n.value("seed", c.seed));
        auto random = [&]() { state += 0x9e3779b9u; return unit(state); };
        std::vector<std::array<float, 2>> points; points.reserve(count);
        for (int i = 0; i < count; ++i) {
            std::array<float, 2> best{}; float bestDistance = -1;
            for (int j = 0; j < candidates; ++j) {
                std::array<float, 2> p{random(), random()}; float nearest = INFINITY;
                for (auto q : points) { float dx = std::abs(p[0] - q[0]), dy = std::abs(p[1] - q[1]); dx = std::min(dx, 1 - dx) * c.width; dy = std::min(dy, 1 - dy) * c.height; nearest = std::min(nearest, dx * dx + dy * dy); }
                if (nearest > bestDistance) { best = p; bestDistance = nearest; }
            }
            points.push_back(best);
        }
        auto out = make(c);
        for (auto p : points) {
            float px = p[0] * c.width - 0.5f, py = p[1] * c.height - 0.5f;
            for (int y = static_cast<int>(std::floor(py - radius - 0.5f)); y <= static_cast<int>(std::ceil(py + radius + 0.5f)); ++y) for (int x = static_cast<int>(std::floor(px - radius - 0.5f)); x <= static_cast<int>(std::ceil(px + radius + 0.5f)); ++x) {
                int wx = (x % c.width + c.width) % c.width, wy = (y % c.height + c.height) % c.height; float v = clamp(radius + 0.5f - std::hypot(x - px, y - py));
                out->set(wx, wy, gray(std::max(out->get(wx, wy)[0], v)));
            }
        }
        return out;
    }
    if (op != "math" && op != "auto_levels" && op != "range_mask" && op != "distance" && op != "bevel" && op != "gaussian_blur" && op != "directional_blur" && op != "slope_blur") return {};
    auto src = inputs.at(n["input"].get<std::string>());
    if (op == "math") {
        auto b = n.contains("b") ? inputs.at(n["b"].get<std::string>()) : ImagePtr{}; float value = n["value"], lo = n["range"][0], hi = n["range"][1]; int steps = n["steps"]; std::string mode = n["mode"];
        Kind kind = src->kind == Kind::Color || (b && b->kind == Kind::Color) ? Kind::Color : (src->kind == Kind::Normal || (b && b->kind == Kind::Normal) ? Kind::Normal : Kind::Scalar);
        return pixels(kind, [&](int x, int y) {
            auto p = src->get(x, y), q = b ? b->get(x, y) : gray(value);
            for (int k = 0; k < 3; ++k) {
                float a = p[k], v = q[k];
                if (mode == "add") p[k] = a + v; else if (mode == "subtract") p[k] = a - v; else if (mode == "multiply") p[k] = a * v;
                else if (mode == "divide") p[k] = std::abs(v) < 1e-8f ? 0 : a / v;
                else if (mode == "min") p[k] = std::min(a, v); else if (mode == "max") p[k] = std::max(a, v);
                else if (mode == "pow") p[k] = a <= 0 && v < 0 ? 0 : std::pow(std::max(0.0f, a), v);
                else if (mode == "abs") p[k] = std::abs(a); else if (mode == "fract") p[k] = a - std::floor(a);
                else if (mode == "clamp") p[k] = std::clamp(a, lo, hi);
                else p[k] = hi == lo ? lo : lo + std::round(clamp((a - lo) / (hi - lo)) * (steps - 1)) * (hi - lo) / (steps - 1);
                if (!std::isfinite(p[k])) throw std::runtime_error("nonfinite math result; reduce operand range");
            }
            return p;
        });
    }
    if (op == "auto_levels") {
        Pixel lo{INFINITY, INFINITY, INFINITY, 0}, hi{-INFINITY, -INFINITY, -INFINITY, 0};
        for (int y = 0; y < c.height; ++y) for (int x = 0; x < c.width; ++x) { auto p = src->get(x, y); for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]); } }
        if (!n["per_channel"].get<bool>()) { float a = std::min({lo[0], lo[1], lo[2]}), b = std::max({hi[0], hi[1], hi[2]}); lo = gray(a); hi = gray(b); }
        float a = n["range"][0], b = n["range"][1];
        return pixels(src->kind, [&](int x, int y) { auto p = src->get(x, y); for (int k = 0; k < 3; ++k) p[k] = a + (b - a) * (hi[k] > lo[k] ? clamp((p[k] - lo[k]) / (hi[k] - lo[k])) : 0); return p; });
    }
    if (op == "range_mask") {
        float lo = n["range"][0], hi = n["range"][1], softness = n["softness"];
        return pixels(Kind::Scalar, [&](int x, int y) { float v = luminance(src->get(x, y)); return gray(softness > 0 ? smooth((v - lo) / softness + 1) * smooth((hi - v) / softness + 1) : (v >= lo && v <= hi ? 1 : 0)); });
    }
    std::string edge = n["edge"];
    if (op == "distance" || op == "bevel") {
        float threshold = n["threshold"], radius = n["radius"]; std::string side = n.value("side", std::string("inside")), profile = n.value("profile", std::string("linear")); float height = n.value("height", 1.0f);
        auto inside = side != "outside" ? distanceField(src, false, threshold, edge, c) : ImagePtr{};
        auto outside = side != "inside" ? distanceField(src, true, threshold, edge, c) : ImagePtr{};
        return pixels(Kind::Scalar, [&](int x, int y) {
            float v = side == "outside" ? clamp(outside->get(x, y)[0] / radius) : clamp(inside->get(x, y)[0] / radius);
            if (side == "signed") v = luminance(src->get(x, y)) >= threshold ? 0.5f + v * 0.5f : 0.5f - clamp(outside->get(x, y)[0] / radius) * 0.5f;
            if (op == "bevel") { if (profile == "smooth") v = smooth(v); if (profile == "round") v = std::sqrt(std::max(0.0f, 1 - (1 - v) * (1 - v))); v *= height; }
            return gray(v);
        });
    }
    if (op == "gaussian_blur") {
        float sigma = n["sigma"]; if (sigma == 0) return src; int radius = static_cast<int>(std::ceil(3 * sigma)); std::vector<double> weights(2 * radius + 1); double sum = 0;
        for (int i = -radius; i <= radius; ++i) { double w = std::exp(-double(i) * i / (2 * sigma * sigma)); weights[i + radius] = w; sum += w; }
        for (auto& w : weights) w /= sum;
        auto a = make(c, src->kind), b = make(c, src->kind);
        c.workers.rows(c.height, [&](int y) { for (int x = 0; x < c.width; ++x) { Pixel p{}; for (int i = -radius; i <= radius; ++i) { auto q = premultiply(fetch(*src, x + i, y, edge), src->kind); for (int k = 0; k < 4; ++k) p[k] += static_cast<float>(q[k] * weights[i + radius]); } a->set(x, y, p); } });
        c.workers.rows(c.height, [&](int y) { for (int x = 0; x < c.width; ++x) { Pixel p{}; for (int i = -radius; i <= radius; ++i) { auto q = fetch(*a, x, y + i, edge); for (int k = 0; k < 4; ++k) p[k] += static_cast<float>(q[k] * weights[i + radius]); } b->set(x, y, unpremultiply(p, src->kind)); } });
        return b;
    }
    int samples = n["samples"];
    if (op == "directional_blur") {
        float length = n["length"], angle = n["angle"].get<float>() * pi / 180, dx = std::cos(angle) * length / c.width, dy = std::sin(angle) * length / c.height;
        if (length == 0) return src;
        return pixels(src->kind, [&](int x, int y) { Pixel p{}; float u = (x + 0.5f) / c.width, v = (y + 0.5f) / c.height; for (int i = 0; i < samples; ++i) { float t = float(i) / (samples - 1) - 0.5f; auto q = premultiply(src->sample(u + dx * t, v + dy * t, edge), src->kind); for (int k = 0; k < 4; ++k) p[k] += q[k] / samples; } return unpremultiply(p, src->kind); });
    }
    auto slope = inputs.at(n["slope"].get<std::string>()); float step = n["strength"].get<float>() / samples; std::string mode = n["mode"];
    if (step == 0) return src;
    return pixels(src->kind, [&](int x, int y) {
        float u = (x + 0.5f) / c.width, v = (y + 0.5f) / c.height; auto p = src->get(x, y); if (mode == "average") p = premultiply(p, src->kind);
        for (int i = 0; i < samples; ++i) {
            float dx = luminance(slope->sample(u + 1.0f / c.width, v, edge)) - luminance(slope->sample(u - 1.0f / c.width, v, edge));
            float dy = luminance(slope->sample(u, v + 1.0f / c.height, edge)) - luminance(slope->sample(u, v - 1.0f / c.height, edge));
            float magnitude = std::hypot(dx, dy); if (magnitude > 1e-8f) { u -= step * dx / magnitude / c.width; v -= step * dy / magnitude / c.height; }
            auto q = src->sample(u, v, edge); if (mode == "average") q = premultiply(q, src->kind);
            for (int k = 0; k < 4; ++k) { if (mode == "average") p[k] += q[k]; else if (mode == "min") p[k] = std::min(p[k], q[k]); else p[k] = std::max(p[k], q[k]); }
        }
        if (mode == "average") { for (auto& v : p) v /= samples + 1; p = unpremultiply(p, src->kind); }
        return p;
    });
}
}
