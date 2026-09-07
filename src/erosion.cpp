#include "texutil/texutil.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tex {
ImagePtr erode(const Json& n, const ImagePtr& input, const ImagePtr& mask, Context& c) {
    auto make = [&](Kind kind = Kind::Scalar) { return std::make_shared<Image>(c.width, c.height, kind, c.memory); };
    auto h = make(), next = make(); const int w = c.width, rows = c.height; const size_t count = static_cast<size_t>(w) * rows;
    c.workers.rows(rows, [&](int y) { for (int x = 0; x < w; ++x) { float value = input->kind == Kind::Scalar ? input->get(x, y)[0] : luminance(input->get(x, y)); if (!std::isfinite(value) || value < 0 || value > 10000) throw std::runtime_error("erode requires finite heights in 0..10000; remap input first"); h->pixels[static_cast<size_t>(y) * w + x] = value; } });
    int iterations = n["iterations"]; float rate = n["rate"], talus = n["talus"]; std::string mode = n["mode"], edge = n["edge"]; bool repeat = edge == "repeat";
    if (iterations == 0 || rate == 0) return h;
    const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1}, opposite[4] = {1, 0, 3, 2};
    auto neighbor = [&](int x, int y, int k) -> size_t { int nx = x + dx[k], ny = y + dy[k]; if (repeat) { nx = (nx + w) % w; ny = (ny + rows) % rows; } else if (nx < 0 || nx >= w || ny < 0 || ny >= rows) return static_cast<size_t>(y) * w + x; return static_cast<size_t>(ny) * w + nx; };
    auto mobility = [&](size_t i) { return mask ? clamp(luminance(mask->get(static_cast<int>(i % w), static_cast<int>(i / w)))) : 1.0f; };
    if (mode == "time") {
        // Symmetric pair fluxes conserve material, with at most 1/4 leaving per neighbor.
        for (int iteration = 0; iteration < iterations; ++iteration) {
            c.workers.rows(rows, [&](int y) { for (int x = 0; x < w; ++x) {
                size_t i = static_cast<size_t>(y) * w + x; double value = h->pixels[i]; float m = mobility(i);
                for (int k = 0; k < 4; ++k) { size_t j = neighbor(x, y, k); float difference = h->pixels[j] - h->pixels[i]; value += std::copysign(std::max(0.0f, std::abs(difference) - talus), difference) * rate * 0.25 * std::min(m, mobility(j)); }
                next->pixels[i] = static_cast<float>(value);
            } });
            std::swap(h, next);
        }
        return h;
    }
    if (mode == "wind") {
        float angle = n["angle"].get<float>() * 3.14159265358979323846f / 180, vx = std::cos(angle), vy = std::sin(angle), distance = n["distance"];
        if (distance == 0) return h;
        auto removed = make();
        for (int iteration = 0; iteration < iterations; ++iteration) {
            c.workers.rows(rows, [&](int y) { for (int x = 0; x < w; ++x) {
                size_t i = static_cast<size_t>(y) * w + x; float upstream = h->sample((x + 0.5f - vx) / w, (y + 0.5f - vy) / rows, edge)[0];
                float amount = std::min(h->pixels[i], std::max(0.0f, h->pixels[i] - upstream - talus) * rate * 0.25f * mobility(i));
                removed->pixels[i] = amount; next->pixels[i] = h->pixels[i] - amount;
            } });
            // Deterministic deposition order avoids atomics and races at shared destinations.
            for (int y = 0; y < rows; ++y) for (int x = 0; x < w; ++x) {
                float amount = removed->pixels[static_cast<size_t>(y) * w + x]; if (amount == 0) continue;
                float px = x + vx * distance, py = y + vy * distance; int ix = static_cast<int>(std::floor(px)), iy = static_cast<int>(std::floor(py)); float fx = px - ix, fy = py - iy;
                for (int oy = 0; oy < 2; ++oy) for (int ox = 0; ox < 2; ++ox) { int nx = ix + ox, ny = iy + oy; if (repeat) { nx = (nx % w + w) % w; ny = (ny % rows + rows) % rows; } else { nx = std::clamp(nx, 0, w - 1); ny = std::clamp(ny, 0, rows - 1); } next->pixels[static_cast<size_t>(ny) * w + nx] += amount * (ox ? fx : 1 - fx) * (oy ? fy : 1 - fy); }
            }
            std::swap(h, next);
        }
        return h;
    }
    auto water = make(), waterNext = make(), sediment = make(), sedimentNext = make(), flux = make(Kind::Normal);
    float rainfall = n["rainfall"], capacity = n["capacity"], deposition = n["deposition"], evaporation = n["evaporation"]; uint32_t seed = static_cast<uint32_t>(n.value("seed", c.seed));
    if (rainfall == 0 || capacity == 0) return h;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        c.workers.rows(rows, [&](int y) { for (int x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y) * w + x; uint32_t r = static_cast<uint32_t>(i) + seed + static_cast<uint32_t>(iteration) * 0x9e3779b9u; r = (r ^ (r >> 16)) * 0x21f0aaadu; r = (r ^ (r >> 15)) * 0x735a2d97u; r ^= r >> 15;
            water->pixels[i] += rainfall * (0.5f + static_cast<float>(r >> 8) / 16777216.0f);
        } });
        // Outgoing shallow-water flow is limited by locally available water.
        c.workers.rows(rows, [&](int y) { for (int x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y) * w + x; float surface = h->pixels[i] + water->pixels[i], sum = 0; Pixel f{};
            for (int k = 0; k < 4; ++k) { size_t j = neighbor(x, y, k); f[k] = std::max(0.0f, surface - h->pixels[j] - water->pixels[j]) * 0.25f; if (j == i) f[k] = 0; sum += f[k]; }
            float scale = sum > water->pixels[i] && sum > 0 ? water->pixels[i] / sum : 1; for (auto& value : f) value *= scale; flux->set(x, y, f);
        } });
        c.workers.rows(rows, [&](int y) { for (int x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y) * w + x; auto f = flux->get(x, y); float transport = 0, downhill = 0;
            for (int k = 0; k < 4; ++k) { float drop = std::max(0.0f, h->pixels[i] - h->pixels[neighbor(x, y, k)]); transport += f[k] * drop; downhill = std::max(downhill, drop); }
            float target = capacity * transport, load = sediment->pixels[i], change = 0;
            if (load < target) change = -std::min({h->pixels[i], downhill * 0.25f, (target - load) * rate * mobility(i)});
            else change = (load - target) * deposition;
            next->pixels[i] = h->pixels[i] + change; sediment->pixels[i] -= change;
        } });
        std::swap(h, next);
        c.workers.rows(rows, [&](int y) { for (int x = 0; x < w; ++x) {
            size_t i = static_cast<size_t>(y) * w + x; auto f = flux->get(x, y); double outflow = double(f[0]) + f[1] + f[2] + f[3]; float fraction = water->pixels[i] > 0 ? clamp(static_cast<float>(outflow / water->pixels[i])) : 0;
            double wvalue = std::max(0.0, double(water->pixels[i]) - outflow), svalue = sediment->pixels[i] * (1 - fraction);
            for (int k = 0; k < 4; ++k) { size_t j = neighbor(x, y, k); if (j == i) continue; float incoming = flux->pixels[j * 4 + opposite[k]]; wvalue += incoming; svalue += water->pixels[j] > 0 ? sediment->pixels[j] * clamp(incoming / water->pixels[j]) : 0; }
            waterNext->pixels[i] = static_cast<float>(wvalue * (1 - evaporation)); sedimentNext->pixels[i] = static_cast<float>(svalue * (1 - evaporation)); h->pixels[i] += static_cast<float>(svalue * evaporation);
        } });
        std::swap(water, waterNext); std::swap(sediment, sedimentNext);
    }
    for (size_t i = 0; i < count; ++i) h->pixels[i] += sediment->pixels[i];
    return h;
}
}
