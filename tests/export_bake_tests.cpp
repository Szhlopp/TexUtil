#include "texutil/model.hpp"
#include "texutil/material_recipe.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
using namespace tex;
void expect(bool ok, const std::string& s) { if(!ok)throw std::runtime_error(s); }
template<class F> void rejects(F fn) { bool failed=false;try{fn();}catch(const std::exception&){failed=true;}expect(failed,"expected rejection"); }
void save(const std::filesystem::path& p, const Json& j) { std::ofstream f(p);f<<j; }
Json read(const std::filesystem::path& p) { std::ifstream f(p);return Json::parse(f); }
model::Mesh plane(bool mirror=false) {
    using namespace model;Mesh m;m.materials={"surface"};m.vertices={{{0,0,0},{0,0,1},{0,0},true},{{1,0,0},{0,0,1},{1,0},true},{{1,1,0},{0,0,1},{1,1},true},{{0,1,0},{0,0,1},{0,1},true}};m.triangles={{{0,1,2},0},{{0,2,3},0}};if(mirror)for(auto& v:m.vertices)v.uv.x=1-v.uv.x;bounds(m);return m;
}
}
int main(int argc, char** argv) {
    try{
        expect(argc==2||argc==3,"work directory and optional GPU-test source directory required");std::filesystem::path work=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(work);
        for(auto name:{"plane","mirror","overlap"}){auto m=plane(std::string(name)=="mirror");if(std::string(name)=="overlap")m.triangles.push_back(m.triangles[0]);std::filesystem::remove(work/(std::string(name)+".obj"));std::filesystem::remove(work/(std::string(name)+".mtl"));model::saveObj(m,work/(std::string(name)+".obj"));}
        Json material={{"type","materialx"},{"inputs",{{"base_color",{{"texture","color.png"}}},{"specular_roughness",.3},{"metalness",0},{"specular_IOR",1.333},{"transmission",.85},{"transmission_color",{{"texture","color.png"}}},{"coat",.2}}},{"normal",{{"texture","normal.png"},{"convention","directx"},{"scale",.5}}}};
        Json source={{"size",16},{"nodes",{{"color",{{"op","constant"},{"value",Json::array({.5,.2,.1})}}},{"height",{{"op","constant"},{"value",.7}}},{"normal",{{"op","constant"},{"value",Json::array({.7,.3,.91231056})}}}}},{"outputs",{{"color.png","color"},{"height.png","height"},{"normal.png",{{"node","normal"},{"srgb",false}}},{"material.mtlx",material}}}};
        save(work/"source.json",source);
        Json bake={{"type","export_bake"},{"model","plane.obj"},{"size",32},{"padding",4},{"material",{{"graph","source.json"},{"height","height.png"}}}};
        auto run=[&](const Json& settings,const std::string& dir,unsigned threads=1){Options o;o.out=work/dir;o.threads=threads;o.memoryMb=64;return Graph({{"nodes",Json::object()},{"outputs",{{"manifest.json",settings}}}},work,o).render();};
        run(bake,"uv");auto memory=std::make_shared<Memory>();auto load=[&](std::string p,Kind k=Kind::Scalar,bool srgb=false){return readPng(work/p,k,srgb,memory);};
        auto c=load("uv/manifest.assets/1/color.png",Kind::Color,true);expect(std::abs(c->get(16,16)[0]-.5)<.005,"color must be encoded exactly once");
        auto r=load("uv/manifest.assets/1/roughness.png");expect(std::abs(r->get(16,16)[0]-.3)<.0001,"constant roughness bake");
        auto h=load("uv/manifest.assets/1/height.png");expect(std::abs(h->get(16,16)[0]-.7)<.0001,"explicit scalar height must be retained");
        auto n=load("uv/manifest.assets/1/normal.png",Kind::Normal);float expectedX=.2f/std::sqrt(.08f+.82462112f*.82462112f);expect(std::abs(n->get(16,16)[0]-(expectedX*.5f+.5f))<.0001,"normal strength must be applied once");
        auto result=read(work/"uv/manifest.assets/1/material.json");expect(result["outputs"]["material.mtlx"]["inputs"]["specular_IOR"]==1.333,"IOR must survive baking");expect(result["outputs"]["material.mtlx"]["inputs"]["coat"]==.2,"coat constant must survive");expect(result["outputs"]["material.mtlx"]["inputs"]["transmission_color"]["texture"]=="extra_transmission_color.png","texture-bound optics must be reprojected");
        auto ogl=bake;ogl["normal_convention"]="opengl";run(ogl,"ogl");auto gn=load("ogl/manifest.assets/1/normal.png",Kind::Normal);expect(std::abs(gn->get(16,16)[1]+n->get(16,16)[1]-1)<.0001,"normal convention must flip green once");
        auto tri=bake;tri["material"]["projection"]="triplanar";run(tri,"tri");tri["model"]="mirror.obj";run(tri,"mirrored");auto tn=load("tri/manifest.assets/1/normal.png",Kind::Normal),mn=load("mirrored/manifest.assets/1/normal.png",Kind::Normal);expect(std::abs(tn->get(16,16)[0]+mn->get(16,16)[0]-1)<.0001,"mirrored UVs must reverse tangent X");
        run(bake,"parallel",4);expect(load("parallel/manifest.assets/1/normal.png",Kind::Normal)->pixels==n->pixels,"bake must be deterministic across threads");
        // Constant-only materials need no image graph and must report absent height honestly.
        save(work/"constant.json",{{"nodes",Json::object()},{"outputs",{{"material.mtlx",{{"type","materialx"},{"inputs",{{"metalness",1},{"base_color",Json::array({.8,.6,.2})}}}}}}}});
        auto constant=bake;constant["material"]={{"graph","constant.json"}};run(constant,"constant");
        auto neutral=load("constant/manifest.assets/1/normal.png",Kind::Normal);expect(std::abs(neutral->get(16,16)[0]-.5)<.0001&&neutral->get(16,16)[2]>.999,"missing normals must be flat");
        expect(std::abs(load("constant/manifest.assets/1/height.png")->get(16,16)[0]-.5)<.0001,"missing height must use neutral midlevel");
        expect(!read(work/"constant/manifest.json")["materials"][0]["height"]["source_present"].get<bool>(),"missing height must be identified in manifest");
        auto bad=bake;bad["model"]="overlap.obj";rejects([&]{run(bad,"bad-overlap");});bad=bake;bad["maps"]={"bogus"};rejects([&]{run(bad,"bad-map");});bad=bake;bad["material"]["height"]="color.png";rejects([&]{run(bad,"bad-height");});
        bad=bake;bad["material"]["projection_scale"]=0;rejects([&]{run(bad,"bad-scale");});
        rejects([&]{Options o;o.out=work/"memory";o.memoryMb=1;Graph({{"nodes",Json::object()},{"outputs",{{"manifest.json",bake}}}},work,o).render();});
        // A root image output cannot replace the source recipe before the bake executes.
        rejects([&]{Options o;o.out=work;Graph({{"nodes",{{"x",{{"op","constant"},{"value",0}}}}},{"outputs",{{"source.json",bake}}}},work,o);});
        // Two material islands retain separate values with independent padding.
        auto two=plane();two.materials={"left","right"};for(auto& v:two.vertices){v.uv.x=.05f+v.uv.x*.4f;v.uv.y=.1f+v.uv.y*.8f;}auto right=plane();for(auto v:right.vertices){v.position.x+=2;v.uv.x=.55f+v.uv.x*.4f;v.uv.y=.1f+v.uv.y*.8f;two.vertices.push_back(v);}for(auto t:right.triangles){for(auto& i:t.vertices)i+=4;t.material=1;two.triangles.push_back(t);}model::bounds(two);std::filesystem::remove(work/"two.obj");std::filesystem::remove(work/"two.mtl");model::saveObj(two,work/"two.obj");
        auto red=source;red["nodes"]["color"]["value"]=Json::array({1,0,0});save(work/"red.json",red);auto blue=source;blue["nodes"]["color"]["value"]=Json::array({0,0,1});save(work/"blue.json",blue);
        auto multi=bake;multi.erase("material");multi["model"]="two.obj";multi["materials"]={{"#1","red.json"},{"#2","blue.json"}};run(multi,"multi");auto left=load("multi/manifest.assets/1/color.png",Kind::Color),rightMap=load("multi/manifest.assets/2/color.png",Kind::Color);expect(left->get(8,16)[0]>.99&&left->get(24,16)[0]==0,"first material mask");expect(rightMap->get(24,16)[2]>.99&&rightMap->get(8,16)[2]==0,"second material mask");expect(left->get(0,16)[0]>.99,"island padding must extend border");
        if(argc==3){
            expect(filamentAvailable(),"GPU regression needs a Filament build");Image quadrants(2,2,Kind::Color,memory);quadrants.set(0,0,{1,0,0,1});quadrants.set(1,0,{0,1,0,1});quadrants.set(0,1,{0,0,1,1});quadrants.set(1,1,{1,1,0,1});writeImage(work/"quadrants.png",quadrants,"png",8,false,true,{0,0,0,1});
            Json q;q["size"]=32;q["nodes"]={{"color",{{"op","image"},{"path","quadrants.png"}}}};q["outputs"]["color.png"]="color";q["outputs"]["material.mtlx"]={{"type","materialx"},{"inputs",{{"base_color",{{"texture","color.png"}}},{"specular_roughness",1},{"metalness",0}}}};save(work/"quadrants.json",q);
            auto qb=bake;qb["material"]={{"graph","quadrants.json"}};run(qb,"quadrant-bake");
            Options o;o.out=work/"gpu";Json p={{"type","preview"},{"model",(work/"plane.obj").string()},{"material",{{"graph",(work/"quadrant-bake/manifest.assets/1/material.json").string()}}},{"environment",(std::filesystem::path(argv[2])/"assets/hdri/studio.hdr").string()},{"size",256}};Graph({{"nodes",Json::object()},{"outputs",{{"check.png",p}}}},work,o).render();
            auto image=load("gpu/check.png",Kind::Color,true);auto tl=image->get(92,92),tr=image->get(164,92),bl=image->get(92,164),br=image->get(164,164);
            expect(tl[0]>tl[1]*2&&tl[0]>tl[2]*2,"GPU atlas top-left must be red, not vertically flipped");expect(tr[1]>tr[0]*2&&tr[1]>tr[2]*2,"GPU atlas top-right must be green");expect(bl[2]>bl[0]*2&&bl[2]>bl[1]*2,"GPU atlas bottom-left must be blue");expect(br[0]>br[2]*2&&br[1]>br[2]*2,"GPU atlas bottom-right must be yellow");
            std::cout<<"GPU UV orientation regression passed\n";
        }
        std::cout<<"ExportBake color spaces, height, normal strength/conventions, mirrored UVs, material slots, padding, optical preservation and validation passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
