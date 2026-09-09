#include "texutil/model.hpp"
#define TINYBVH_IMPLEMENTATION
#include <tiny_bvh.h>
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <tuple>

namespace tex::model {
namespace {
void require(bool ok, const std::string& message) { if(!ok)throw std::runtime_error("model bake: "+message); }
std::vector<float> curvature(const Mesh& mesh) {
    float diagonal=length(mesh.maximum-mesh.minimum),epsilon=diagonal*1e-7f;
    std::map<std::tuple<int64_t,int64_t,int64_t>,size_t> keys;std::vector<size_t> index;std::vector<Vec3> positions,normals,laplacian;std::vector<float> areas;
    for(const auto& v:mesh.vertices){auto p=v.position-mesh.minimum;auto key=std::make_tuple(int64_t(std::llround(p.x/epsilon)),int64_t(std::llround(p.y/epsilon)),int64_t(std::llround(p.z/epsilon)));auto [it,added]=keys.emplace(key,positions.size());if(added){positions.push_back(v.position);normals.push_back({});laplacian.push_back({});areas.push_back(0);}index.push_back(it->second);}
    for(const auto& t:mesh.triangles) {
        size_t i[3]={index[t.vertices[0]],index[t.vertices[1]],index[t.vertices[2]]};Vec3 p[3]={positions[i[0]],positions[i[1]],positions[i[2]]};auto normal=cross(p[1]-p[0],p[2]-p[0]);float twiceArea=length(normal);if(twiceArea<1e-20f)continue;
        for(int j=0;j<3;++j){normals[i[j]]=normals[i[j]]+normal;areas[i[j]]+=twiceArea/6;int a=(j+1)%3,b=(j+2)%3;float cot=dot(p[a]-p[j],p[b]-p[j])/twiceArea;auto delta=(p[a]-p[b])*cot;laplacian[i[a]]=laplacian[i[a]]+delta;laplacian[i[b]]=laplacian[i[b]]-delta;}
    }
    std::vector<float> out;out.reserve(index.size());
    for(auto i:index){float h=areas[i]>1e-20f?dot(laplacian[i],normalized(normals[i]))/(4*areas[i]):0;out.push_back(.5f+.5f*std::tanh(h*diagonal*.1f));}return out;
}
uint32_t hash(uint32_t x) { x^=x>>16;x*=0x7feb352d;x^=x>>15;x*=0x846ca68b;x^=x>>16;return x; }
float random(uint32_t x) { return (hash(x)>>8)*(1.f/16777216.f); }
Vec3 hemisphere(Vec3 n, unsigned sample, unsigned samples, uint32_t seed) {
    float r=std::sqrt((sample+.5f)/samples),phi=6.28318530718f*std::fmod(sample*.61803398875f+random(seed),1.f);
    auto tangent=normalized(cross(std::abs(n.y)<.95f?Vec3{0,1,0}:Vec3{1,0,0},n)),bitangent=cross(n,tangent);
    return tangent*(r*std::cos(phi))+bitangent*(r*std::sin(phi))+n*std::sqrt(std::max(0.f,1-r*r));
}
void pad(Image& image, const Raster& raster, int distance) {
    int s=raster.size;std::vector<int32_t> source=raster.faces;for(size_t i=0;i<source.size();++i)source[i]=source[i]<0?-1:int32_t(i);
    // A bounded wavefront extends UV island border texels without wrapping islands.
    std::vector<int32_t> next(source);
    for(int step=0;step<distance;++step){bool changed=false;next=source;for(int y=0;y<s;++y)for(int x=0;x<s;++x){size_t i=size_t(y)*s+x;if(source[i]>=0)continue;for(auto d:{std::pair<int,int>{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}}){int xx=x+d.first,yy=y+d.second;if(xx>=0&&xx<s&&yy>=0&&yy<s&&source[size_t(yy)*s+xx]>=0){next[i]=source[size_t(yy)*s+xx];changed=true;break;}}}source.swap(next);if(!changed)break;}
    for(size_t i=0;i<source.size();++i)if(raster.faces[i]<0&&source[i]>=0)image.set(int(i%s),int(i/s),image.get(source[i]%s,source[i]/s));
}
}
Json bake(const Mesh& mesh, const std::filesystem::path& output, int size, unsigned samples, float distance, int padding, unsigned threads, size_t memoryMb, const std::vector<std::string>& maps) {
    require(size>=16&&size<=4096,"size must be 16..4096");require(samples>=1&&samples<=4096,"samples must be 1..4096");require(padding>=0&&padding<=64,"padding must be 0..64");
    const std::set<std::string> allowed={"curvature","ao","thickness","materialids","position","object_normal","coverage"};
    require(!maps.empty(),"at least one map is required");std::set<std::string> selected;for(const auto& map:maps){require(allowed.count(map),"unknown map: "+map);require(selected.insert(map).second,"duplicate map: "+map);}
    auto check=checkUvs(mesh,std::min(size,512));require(check["usable_for_baking"].get<bool>(),"UVs need a unique 0..1 atlas; run model check-uvs and model uv first");
    size_t reserved=size_t(size)*size*20+mesh.vertices.size()*sizeof(float)+mesh.triangles.size()*256;
    require(memoryMb*1024ull*1024>reserved+size_t(size)*size*16,"memory budget too small for mesh, raster, padding and map buffers");
    auto raster=rasterize(mesh,size);require(raster.overlaps==0,"UV overlaps detected at bake resolution");
    auto memory=std::make_shared<Memory>();memory->limit=memoryMb*1024ull*1024-reserved;Workers workers(threads);
    float diagonal=length(mesh.maximum-mesh.minimum);if(distance==0)distance=diagonal;require(std::isfinite(distance)&&distance>0,"distance must be positive");
    float epsilon=std::max(1e-7f,diagonal*1e-5f);std::vector<tinybvh::bvhvec4> vertices;std::unique_ptr<tinybvh::BVH> bvh;
    if(selected.count("ao")||selected.count("thickness")){vertices.reserve(mesh.triangles.size()*3);for(const auto& t:mesh.triangles)for(auto i:t.vertices){auto p=mesh.vertices[i].position;vertices.emplace_back(p.x,p.y,p.z,0);}bvh=std::make_unique<tinybvh::BVH>();bvh->Build(vertices.data(),uint32_t(mesh.triangles.size()));}
    std::vector<float> curv;if(selected.count("curvature"))curv=curvature(mesh);
    Json graph={{"size",size},{"tile",false},{"nodes",Json::object()},{"outputs",Json::object()}};
    Json report={{"size",size},{"samples",samples},{"distance",distance},{"padding",padding},{"uv_check",check},{"files",Json::array()},{"materials",Json::array()}};
    for(size_t i=0;i<mesh.materials.size();++i)report["materials"].push_back({{"id",i},{"name",mesh.materials[i]},{"value",mesh.materials.size()>1?double(i)/(mesh.materials.size()-1):0.0}});
    std::filesystem::create_directories(output);
    for(const auto& map:maps) {
        Kind kind=(map=="position"||map=="object_normal")?Kind::Normal:Kind::Scalar;Image image(size,size,kind,memory);
        workers.rows(size,[&](int y){for(int x=0;x<size;++x){size_t ix=size_t(y)*size+x;int f=raster.faces[ix];Pixel value=map=="curvature"?Pixel{.5,.5,.5,1}:map=="ao"?Pixel{1,1,1,1}:map=="object_normal"?Pixel{.5,.5,1,1}:Pixel{0,0,0,1};
            if(f>=0){auto t=mesh.triangles[f];auto a=mesh.vertices[t.vertices[0]],b=mesh.vertices[t.vertices[1]],c=mesh.vertices[t.vertices[2]];auto weights=raster.barycentrics[ix];float wa=weights.x,wb=weights.y,wc=1-wa-wb;auto p=a.position*wa+b.position*wb+c.position*wc;auto n=normalized(a.normal*wa+b.normal*wb+c.normal*wc);auto geometric=normalized(cross(b.position-a.position,c.position-a.position));if(dot(n,geometric)<0)n=n*(-1);
                float scalar=0;
                if(map=="coverage")scalar=1;
                if(map=="curvature")scalar=curv[t.vertices[0]]*wa+curv[t.vertices[1]]*wb+curv[t.vertices[2]]*wc;
                if(map=="materialids")scalar=mesh.materials.size()>1?float(t.material)/(mesh.materials.size()-1):0;
                if(map=="position"){auto q=p-mesh.minimum,d=mesh.maximum-mesh.minimum;value={d.x>0?q.x/d.x:0,d.y>0?q.y/d.y:0,d.z>0?q.z/d.z:0,1};}
                if(map=="object_normal")value={n.x*.5f+.5f,n.y*.5f+.5f,n.z*.5f+.5f,1};
                if(map=="ao"||map=="thickness") {
                    bool thickness=map=="thickness";float sum=0;auto directionNormal=thickness?n*(-1):n;auto origin=p+geometric*(thickness?-epsilon:epsilon);
                    for(unsigned sample=0;sample<samples;++sample){auto direction=hemisphere(directionNormal,sample,samples,uint32_t(ix));tinybvh::Ray ray({origin.x,origin.y,origin.z},{direction.x,direction.y,direction.z},distance);
                        if(thickness){bvh->Intersect(ray);sum+=std::min(ray.hit.t,distance)/distance;}else sum+=bvh->IsOccluded(ray)?0.f:1.f;
                    }
                    scalar=sum/samples;
                }
                if(kind==Kind::Scalar)value={scalar,scalar,scalar,1};
            }image.set(x,y,value);
        }});
        if(map!="coverage")pad(image,raster,padding);
        auto file=map+".png";writeImage(output/file,image,"png",16,false,false,{0,0,0,1});report["files"].push_back(file);
        graph["nodes"][map]={{"op","image"},{"path",file},{"kind",kind==Kind::Scalar?"scalar":"normal"},{"srgb",false}};graph["outputs"]["export-"+file]={{"node",map},{"bits",16}};
    }
    report["curvature_encoding"]="cotangent mean curvature: 0.5 flat, above convex, below concave; normalized with 0.5 + 0.5*tanh(H*diagonal*0.1)";
    report["thickness_encoding"]="mean inward cosine-weighted ray distance / distance; misses use distance; closed outward-facing geometry required";
    report["units"]="meters for converted FBX/glTF; OBJ uses its source coordinates as meters";
    std::ofstream refs(output/"maps.json"),info(output/"bake-info.json");require(bool(refs)&&bool(info),"cannot write bake metadata");refs<<graph.dump(2)<<'\n';info<<report.dump(2)<<'\n';refs.flush();info.flush();require(bool(refs)&&bool(info),"failed writing bake metadata");return report;
}
}
