#include "texutil/texutil.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace tex {
namespace {
struct IndexBuffer {
    std::shared_ptr<Memory> memory; size_t bytes; std::unique_ptr<uint32_t[]> data;
    IndexBuffer(size_t count, std::shared_ptr<Memory> m) : memory(std::move(m)), bytes(count * sizeof(uint32_t)) {
        if (bytes > memory->limit || memory->current > memory->limit - bytes) throw std::runtime_error("float-buffer memory budget exceeded by flood-fill index buffers; increase --memory");
        data.reset(new uint32_t[count]); memory->current += bytes; memory->peak = std::max(memory->peak, memory->current);
    }
    ~IndexBuffer() { memory->current -= bytes; }
};
float component(Pixel p, const std::string& channel) { return channel == "alpha" ? p[3] : channel == "r" ? p[0] : channel == "g" ? p[1] : channel == "b" ? p[2] : luminance(p); }
}
ImagePtr fieldOperations(const Json& n, const std::map<std::string, ImagePtr>& inputs, Context& c) {
    auto src = inputs.at(n["input"].get<std::string>()); const int w = c.width, h = c.height; const size_t count = static_cast<size_t>(w) * h;
    auto make = [&]() { return std::make_shared<Image>(w, h, Kind::Scalar, c.memory); }; auto out = make(); bool repeat = n["edge"] == "repeat";
    if (n["op"] == "flood_fill") {
        std::string mode = n["mode"], channel = n["channel"]; float threshold = n["threshold"]; bool diagonal = n["connectivity"] == 8;
        IndexBuffer visited(count, c.memory), queue(count, c.memory); std::fill_n(visited.data.get(), count, 0u); uint32_t region = 0;
        auto foreground = [&](size_t i) { return component(src->get(static_cast<int>(i % w), static_cast<int>(i / w)), channel) >= threshold; };
        auto fill = [&](size_t start) {
            if (visited.data[start] || !foreground(start)) return;
            ++region; size_t head = 0, tail = 1; queue.data[0] = static_cast<uint32_t>(start); visited.data[start] = region;
            while (head < tail) {
                uint32_t index = queue.data[head++]; int x = index % w, y = index / w;
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx == 0 && dy == 0) || (!diagonal && dx != 0 && dy != 0)) continue;
                    int nx = x + dx, ny = y + dy;
                    if (repeat) { nx = (nx + w) % w; ny = (ny + h) % h; } else if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
                    size_t j = static_cast<size_t>(ny) * w + nx;
                    if (!visited.data[j] && foreground(j)) { visited.data[j] = region; queue.data[tail++] = static_cast<uint32_t>(j); }
                }
            }
            uint32_t hash = region + static_cast<uint32_t>(n.value("seed", c.seed)); hash = (hash ^ (hash >> 16)) * 0x21f0aaadu; hash = (hash ^ (hash >> 15)) * 0x735a2d97u; hash ^= hash >> 15;
            float value = mode == "select" ? 1 : mode == "area" ? static_cast<float>(double(tail) / count) : mode == "random" ? .1f + .9f * static_cast<float>(hash >> 8) / 16777216.0f : static_cast<float>(region);
            for (size_t i = 0; i < tail; ++i) out->pixels[queue.data[i]] = value;
        };
        if (mode == "select") {
            float u = n["point"][0], v = n["point"][1]; if (repeat) { u -= std::floor(u); v -= std::floor(v); } else { u = clamp(u); v = clamp(v); }
            int x = std::min(w - 1, static_cast<int>(u * w)), y = std::min(h - 1, static_cast<int>(v * h)); fill(static_cast<size_t>(y) * w + x);
        } else {
            for (size_t i = 0; i < count; ++i) fill(i);
            if (mode == "labels" && region) {
                if (region > 16777216) throw std::runtime_error("labels exceed float integer precision; use area or random mode");
                for (auto& value : out->pixels) value /= region;
            }
        }
        return out;
    }
    if (src->kind == Kind::Scalar) throw std::runtime_error("normal_to_height requires an RGB normal map; import it with kind:normal");
    float strength = n["strength"], mean = n["mean"], minZ = n["min_z"], sign = n["convention"] == "directx" ? 1 : -1;
    int iterations = n["iterations"]; double tolerance = n["tolerance"];
    auto gx = make(), gy = make(), residual = make(), direction = make(), applied = make();
    c.workers.rows(h, [&](int y) { for (int x = 0; x < w; ++x) {
        size_t i = static_cast<size_t>(y) * w + x; auto p = src->get(x,y);
        for (int k = 0; k < 3; ++k) if (!std::isfinite(p[k]) || p[k] < 0 || p[k] > 1) throw std::runtime_error("normal components must be finite encoded RGB in 0..1");
        if (p[2] < .5f) throw std::runtime_error("normal Z must be nonnegative for a single-valued heightfield");
        float z = std::max(minZ, p[2] * 2 - 1); gx->pixels[i] = -(p[0] * 2 - 1) / z / strength / w; gy->pixels[i] = sign * (p[1] * 2 - 1) / z / strength / h;
    } });
    auto neighbor = [&](int x, int y) { if (repeat) { x = (x + w) % w; y = (y + h) % h; } else { x = std::clamp(x,0,w-1); y = std::clamp(y,0,h-1); } return static_cast<size_t>(y) * w + x; };
    // This is the exact transpose of the normal node's clamped/periodic central derivative.
    auto adjoint = [&](const ImagePtr& target) {
        c.workers.rows(h, [&](int y) { for (int x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y) * w + x; float dx = 0, dy = 0;
            if (w > 1) { dx = (gx->pixels[neighbor(x-1,y)] - gx->pixels[neighbor(x+1,y)]) * .5f; if (!repeat && x == 0) dx -= gx->pixels[i]; if (!repeat && x == w-1) dx += gx->pixels[i]; }
            if (h > 1) { dy = (gy->pixels[neighbor(x,y-1)] - gy->pixels[neighbor(x,y+1)]) * .5f; if (!repeat && y == 0) dy -= gy->pixels[i]; if (!repeat && y == h-1) dy += gy->pixels[i]; }
            target->pixels[i] = dx + dy;
        } });
    };
    adjoint(residual); direction->pixels = residual->pixels;
    // Row reductions followed by fixed-order summation keep results independent of workers.
    auto dot = [&](const ImagePtr& a, const ImagePtr& b) { std::vector<double> rows(h); c.workers.rows(h, [&](int y) { double sum = 0; for (int x = 0; x < w; ++x) { size_t i = static_cast<size_t>(y) * w + x; sum += double(a->pixels[i]) * b->pixels[i]; } rows[y] = sum; }); double sum = 0; for (double row : rows) sum += row; return sum; };
    double rr = dot(residual,residual), initial = rr;
    for (int iteration = 0; iteration < iterations && rr > initial * tolerance * tolerance && rr > 1e-30; ++iteration) {
        c.workers.rows(h, [&](int y) { for (int x = 0; x < w; ++x) { size_t i = static_cast<size_t>(y) * w + x; gx->pixels[i] = (direction->pixels[neighbor(x+1,y)] - direction->pixels[neighbor(x-1,y)]) * .5f; gy->pixels[i] = (direction->pixels[neighbor(x,y+1)] - direction->pixels[neighbor(x,y-1)]) * .5f; } });
        adjoint(applied); double denominator = dot(direction,applied); if (denominator <= 1e-30) break;
        double alpha = rr / denominator;
        c.workers.rows(h, [&](int y) { for (int x = 0; x < w; ++x) { size_t i = static_cast<size_t>(y) * w + x; out->pixels[i] += static_cast<float>(alpha * direction->pixels[i]); residual->pixels[i] -= static_cast<float>(alpha * applied->pixels[i]); } });
        double next = dot(residual,residual), beta = next / rr;
        c.workers.rows(h, [&](int y) { for (int x = 0; x < w; ++x) { size_t i = static_cast<size_t>(y) * w + x; direction->pixels[i] = residual->pixels[i] + static_cast<float>(beta * direction->pixels[i]); } }); rr = next;
    }
    double average = 0; for (float value : out->pixels) average += value; average /= count;
    for (auto& value : out->pixels) { value = static_cast<float>(value - average + mean); if (!std::isfinite(value)) throw std::runtime_error("normal integration became nonfinite; increase min_z or reduce slope range"); }
    return out;
}
}
