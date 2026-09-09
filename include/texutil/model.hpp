#pragma once
#include "texutil.hpp"
#include <cmath>
#include <cstdint>

namespace tex::model {
struct Vec2 { float x=0,y=0; };
struct Vec3 { float x=0,y=0,z=0; };
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a,a)); }
inline Vec3 normalized(Vec3 a) { float l=length(a);return l>1e-20f?a*(1/l):Vec3{0,1,0}; }
struct Vertex { Vec3 position,normal; Vec2 uv; bool hasUv=false; };
struct Triangle { uint32_t vertices[3]; uint32_t material=0; };
struct Mesh { std::vector<Vertex> vertices; std::vector<Triangle> triangles; std::vector<std::string> materials; Vec3 minimum,maximum; };
struct Raster { int size; std::vector<int32_t> faces; std::vector<Vec2> barycentrics; size_t overlaps=0,covered=0; };
Mesh load(const std::filesystem::path& path);
void bounds(Mesh& mesh);
Raster rasterize(const Mesh& mesh, int size);
Json checkUvs(const Mesh& mesh, int size=512);
void unwrap(Mesh& mesh, int size=1024, int padding=4);
void saveObj(const Mesh& mesh, const std::filesystem::path& path);
Json bake(const Mesh& mesh, const std::filesystem::path& output, int size, unsigned samples, float distance, int padding, unsigned threads, size_t memoryMb, const std::vector<std::string>& maps);
int command(int argc, char** argv);
}

namespace tex {
bool filamentAvailable();
Json parsePreview(const Json& settings, const std::filesystem::path& base);
void validatePreviewReferences(std::vector<Output>& outputs);
ImagePtr renderPreview(const Json& settings, const Json& material, const std::filesystem::path& outputDirectory, std::shared_ptr<Memory> memory);
}
