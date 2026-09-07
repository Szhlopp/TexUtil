#include "texutil/texutil.hpp"
namespace tex {
namespace {
Json f(std::string type, Json value, std::string text) { return {{"type", type}, {"default", value}, {"description", text}}; }
Json num(double value, double lo, double hi, std::string text) { auto j = f("number", value, text); j["min"] = lo; j["max"] = hi; return j; }
Json integer(int value, int lo, int hi, std::string text) { auto j = num(value, lo, hi, text); j["type"] = "integer"; return j; }
Json choice(std::string value, Json choices, std::string text) { auto j = f("string", value, text); j["enum"] = choices; return j; }
}
void addAdvancedCatalog(Json& r) {
    auto add = [&](std::string name, std::string text, Json params) { r[name] = {{"description", text}, {"parameters", params}}; };
    Json input = {{"type", "reference"}, {"required", true}, {"description", "Source node name."}};
    auto edge = choice("repeat", {"repeat", "clamp", "transparent"}, "Boundary sampling.");
    auto seed = f("seed", nullptr, "Override document seed.");
    auto angle = num(0, -36000, 36000, "Clockwise degrees; 0 points right.");
    for (auto op : {"simplex", "perlin", "value", "white", "clouds", "voronoi"}) {
        r[op]["parameters"]["stretch"] = f("positive_pair", 1, "Feature stretch [x,y] in the generation domain, before rasterization.");
        r[op]["parameters"]["angle"] = angle;
    }
    auto& shape = r["shape"]["parameters"];
    shape["type"]["enum"] = {"circle", "box", "diamond", "ring", "polygon", "star", "capsule", "gaussian"};
    shape["sides"] = integer(6, 3, 64, "Polygon sides or star points.");
    shape["inner_radius"] = num(0.45, 0.01, 1, "Star inner radius relative to outer radius.");
    shape["sigma"] = num(0.3, 0.01, 2, "Gaussian standard deviation relative to half-size; cropped at radius 1.");
    for (auto op : {"array", "scatter"}) {
        auto p = r["array"]["parameters"];
        p["sources"] = f("references", Json::array(), "Additional source nodes; select uniformly including input.");
        p["mask"] = f("reference", nullptr, "Density probability from clamped luminance at each candidate center.");
        p["direction"] = f("reference", nullptr, "Clamped luminance adds 0..360 degrees at each stamp center.");
        p["scale_map"] = f("reference", nullptr, "Clamped luminance multiplies stamp size; black skips stamp.");
        p["value_map"] = f("reference", nullptr, "Clamped luminance multiplies stamp RGB/value.");
        p["size_jitter"] = num(0, 0, 0.99, "Uniform random +/- fraction of size.");
        p["value_range"] = f("pair", {1, 1}, "Uniform random stamp value multiplier [min,max] within 0..1.");
        p["row_offset"] = num(0, -1, 1, "Odd grid rows shift by this fraction of a cell; array only.");
        if (std::string(op) == "scatter") { p.erase("row_offset"); p.erase("jitter"); p["count"] = integer(500, 1, 10000, "Number of random candidate stamps before density rejection."); }
        add(op, std::string(op) == "array" ? "Grid sampler with optional source variation and control maps." : "Seeded random stamp sampler with optional source variation and control maps.", p);
    }
    add("gaussian_noise", "Independent Gaussian pixel values, clipped to 0..1. Not a blurred noise.", {{"mean", num(0.5, -10, 10, "Distribution mean before clipping.")}, {"deviation", num(0.15, 0, 10, "Standard deviation before clipping.")}, {"seed", seed}});
    add("blue_noise", "Toroidal best-candidate point mask, not a ranked blue-noise dither texture. Generation is O(candidates * count squared).", {{"count", integer(512, 1, 4096, "Number of points.")}, {"candidates", integer(16, 1, 64, "Candidates compared per point; higher gives more even spacing.")}, {"radius", num(1.5, 0.1, 32, "Antialiased dot radius in pixels.")}, {"seed", seed}});
    add("math", "Component-wise field arithmetic, preserving input alpha. Nonfinite results fail rendering.", {{"input", input}, {"b", f("reference", nullptr, "Optional second field; overrides value.")}, {"value", num(1, -10000, 10000, "Second operand when b is absent.")}, {"mode", choice("multiply", {"add", "subtract", "multiply", "divide", "min", "max", "pow", "abs", "clamp", "fract", "quantize"}, "Divide by |b| < 1e-8 returns 0; pow clamps base to >=0 and defines 0 to negative powers as 0.")}, {"range", f("pair", {0, 1}, "Clamp limits, or quantization range; ordered.")}, {"steps", integer(8, 2, 4096, "Quantization levels including both endpoints.")}});
    add("auto_levels", "Stretch observed RGB range to requested range; scalar uses its single channel. Constant channels map to lower output bound.", {{"input", input}, {"range", f("pair", {0, 1}, "Ordered output range.")}, {"per_channel", f("boolean", false, "Use separate RGB extrema; false shares one RGB range.")}});
    add("range_mask", "Select an inclusive luminance interval with optional outside feather.", {{"input", input}, {"range", f("pair", {0.3, 0.7}, "Selected [low,high] interval.")}, {"softness", num(0, 0, 10, "Feather width beyond each end of interval.")}});
    add("gaussian_blur", "Separable Gaussian convolution, radius ceil(3*sigma), normalized discrete weights and premultiplied alpha.", {{"input", input}, {"sigma", num(2, 0, 128, "Standard deviation in pixels; zero is identity.")}, {"edge", edge}});
    add("directional_blur", "Uniform line blur with bilinear taps and premultiplied alpha.", {{"input", input}, {"length", num(12, 0, 4096, "Full line length in pixels.")}, {"angle", angle}, {"samples", integer(17, 2, 513, "Uniform taps; increase for long blur lengths.")}, {"edge", edge}});
    add("slope_blur", "Repeatedly follow the downhill gradient of a scalar slope map and sample the source.", {{"input", input}, {"slope", input}, {"strength", num(8, 0, 4096, "Total travel distance in pixels on non-flat slopes.")}, {"samples", integer(16, 1, 256, "Steps along the slope; includes initial source in reduction.")}, {"mode", choice("average", {"average", "min", "max"}, "Average premultiplied samples, or component minima/maxima.")}, {"edge", edge}});
    Json distance = {{"input", input}, {"threshold", num(0.5, -10000, 10000, "Foreground is luminance >= threshold.")}, {"radius", num(16, 0.01, 32768, "Pixel distance that maps to 1; distances saturate.")}, {"edge", edge}};
    distance["side"] = choice("inside", {"inside", "outside", "signed"}, "Inside to background, outside to foreground, or signed with boundary centered at 0.5.");
    add("distance", "Exact Euclidean distance to nearest opposite-class pixel center, normalized by radius. Repeat is periodic; transparent adds virtual background outside.", distance);
    distance.erase("side"); distance["profile"] = choice("smooth", {"linear", "smooth", "round"}, "Bevel height profile over inside distance."); distance["height"] = num(1, 0, 10000, "Maximum bevel height.");
    add("bevel", "Turn a binary silhouette into a raised heightfield using inside Euclidean distance.", distance);
    auto channel = choice("luminance", {"luminance", "alpha", "r", "g", "b"}, "Scalar control channel; use alpha for transparent silhouettes.");
    add("edge_detect", "Scalar edge magnitude from Sobel, Scharr, or absolute four-neighbor Laplacian.", {{"input", input}, {"method", choice("sobel", {"sobel", "scharr", "laplacian"}, "Sobel/Scharr normalize an axis-aligned unit step to 1.")}, {"channel", channel}, {"strength", num(1, 0, 1000, "Edge response multiplier.")}, {"clamp", f("boolean", true, "Clamp response to 0..1; false preserves larger values.")}, {"edge", edge}});
    add("stroke", "Antialiased outline of a thresholded silhouette using Euclidean distance. Optional tint and source compositing.", {{"input", input}, {"channel", channel}, {"threshold", num(0.5, -10000, 10000, "Foreground threshold.")}, {"width", num(4, 0, 4096, "Total stroke width in pixels; centered strokes split this across the boundary.")}, {"position", choice("outside", {"outside", "inside", "center"}, "Side of the silhouette boundary.")}, {"softness", num(0, 0, 4096, "Extra outward fade width in pixels, beyond the built-in one-pixel antialiasing.")}, {"color", f("color", nullptr, "Optional stroke tint; otherwise output is a scalar mask.")}, {"include_source", f("boolean", false, "Composite stroke over source; scalar masks combine by maximum.")}, {"edge", edge}});
    add("glow", "Gaussian halo from a scalar channel, optionally tinted and added to the source in linear light.", {{"input", input}, {"channel", channel}, {"radius", num(8, 0, 128, "Gaussian sigma in pixels.")}, {"strength", num(1, 0, 100, "Halo intensity; float output may exceed 1.")}, {"threshold", num(0, 0, 1, "Subtract from clamped control before blurring.")}, {"mode", choice("outer", {"outer", "inner", "both"}, "Outside halo, inside edge glow, or unrestricted blur.")}, {"color", f("color", nullptr, "Optional glow tint; defaults white.")}, {"include_source", f("boolean", true, "Add original image to halo.")}, {"edge", edge}});
    auto center = f("pair", {0.5, 0.5}, "Center in normalized image coordinates.");
    auto radius = num(0.5, 0.0001, 16, "Radius in units of the shorter image dimension; pixel-circular on rectangular images.");
    add("swirl", "Rotate sampling around a center with radius-dependent falloff; positive angle visibly twists clockwise.", {{"input", input}, {"center", center}, {"radius", radius}, {"angle", num(180, -36000, 36000, "Maximum clockwise twist in degrees at the center.")}, {"falloff", num(2, 1, 16, "Twist multiplier (1 - radius_fraction)^falloff; outside the radius is unchanged.")}, {"mask", f("reference", nullptr, "Optional clamped luminance angle multiplier.")}, {"wrap", f("boolean", false, "Repeat the swirl center across tile boundaries; requires radius <= 0.5 and edge:repeat.")}, {"edge", edge}});
    auto polarEdge = edge; polarEdge["default"] = "clamp";
    add("polar", "Convert a Cartesian disk to an angle/radius strip or wrap a strip into a disk. Resampling can lose detail at the center.", {{"input", input}, {"mode", choice("from_polar", {"from_polar", "to_polar"}, "from_polar: strip X=angle, Y=radius to disk; to_polar performs the inverse mapping.")}, {"center", center}, {"radius", radius}, {"angle", angle}, {"edge", polarEdge}});
    auto connectivity = integer(4, 4, 8, "Four edge neighbors or eight including diagonals."); connectivity["enum"] = {4,8};
    add("flood_fill", "Connected components of a thresholded scalar channel, with seed selection or per-region values.", {{"input", input}, {"channel", channel}, {"threshold", num(0.5, -10000, 10000, "Foreground is selected channel >= threshold.")}, {"mode", choice("select", {"select", "random", "labels", "area"}, "Selected component mask, seeded region values, normalized region IDs, or region pixel fraction.")}, {"point", center}, {"connectivity", connectivity}, {"edge", choice("repeat", {"repeat", "clamp"}, "Wrap connected regions across edges, or use a closed image boundary.")}, {"seed", seed}});
    add("normal_to_height", "Least-squares integration of a tangent-space normal map using central differences and conjugate gradients. Absolute height and some high-frequency modes cannot be recovered.", {{"input", input}, {"strength", num(1, 0.00001, 1000, "Height scale used by the original normal conversion.")}, {"convention", choice("directx", {"directx", "opengl"}, "Green-channel convention, matching the normal node.")}, {"mean", num(0.5, -10000, 10000, "Mean of the reconstructed heightfield.")}, {"iterations", integer(500, 0, 5000, "Maximum solver iterations; zero returns a flat mean field.")}, {"tolerance", num(0.00001, 0.00000001, 0.1, "Stop at this relative linear-system residual.")}, {"min_z", num(0.01, 0.0001, 1, "Minimum decoded blue/Z denominator; limits near-horizontal slopes.")}, {"edge", choice("repeat", {"repeat", "clamp"}, "Periodic or clamped central differences; match the source normal generation.")}});
    add("erode", "Conservative heightfield weathering: wind transport, rain water/sediment flow, or time thermal relaxation. Pixel-scale artistic simulation, not a calibrated physical solver.", {
        {"input", input}, {"mask", f("reference", nullptr, "Optional clamped mobility mask; rain/wind deposition can land on protected pixels.")},
        {"mode", choice("time", {"wind", "rain", "time"}, "Erosion model.")}, {"iterations", integer(32, 0, 512, "Simulation steps; zero returns grayscale input.")}, {"rate", num(0.3, 0, 1, "Erosion/relaxation rate per step.")},
        {"edge", choice("repeat", {"repeat", "clamp"}, "Periodic or closed boundary; both retain material.")}, {"talus", num(0.005, 0, 10000, "Stable height drop per pixel (time/wind).")},
        {"angle", angle}, {"distance", num(2, 0, 128, "Wind transport distance in pixels.")}, {"rainfall", num(0.01, 0, 1, "Water added per rain step, with seeded spatial variation.")}, {"capacity", num(8, 0, 100, "Rain sediment capacity coefficient.")}, {"deposition", num(0.3, 0, 1, "Excess rain sediment deposition fraction per step.")}, {"evaporation", num(0.1, 0, 1, "Water evaporation fraction per step; deposits that fraction of sediment.")}, {"seed", seed}
    });
    add("preset", "Reusable scalar material recipe expanded to ordinary nodes before validation/execution. See docs/PRESETS.md for config values.", {{"name", choice("dirt", presetNames(), "Built-in preset recipe.")}, {"config", f("object", Json::object(), "Controls: scale (0.25..4), detail (0..1), angle (degrees), seed (integer). Defaults 1, 0.5, 0, document seed. Unknown keys fail.")}});
}
}
