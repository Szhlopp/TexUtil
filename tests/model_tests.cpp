#include "texutil/model.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void expect(bool ok, const std::string& message) { if(!ok)throw std::runtime_error(message); }
template<class F> void rejects(F fn, const std::string& message) { bool failed=false;try{fn();}catch(const std::exception&){failed=true;}expect(failed,message); }
tex::model::Mesh plane() {
    using namespace tex::model;Mesh mesh;mesh.materials={"plane"};mesh.vertices={{{0,0,0},{0,0,1},{0,0},true},{{1,0,0},{0,0,1},{1,0},true},{{1,1,0},{0,0,1},{1,1},true},{{0,1,0},{0,0,1},{0,1},true}};mesh.triangles={{{0,1,2},0},{{0,2,3},0}};bounds(mesh);return mesh;
}
}
int main(int argc, char** argv) {
    try {
        expect(argc==3,"expected test work directory and source directory");std::filesystem::path work=argv[1],source=argv[2];std::filesystem::create_directories(work);
        auto mesh=plane();auto info=tex::model::checkUvs(mesh,64);expect(info["usable_for_baking"].get<bool>(),"plane should have usable UVs");expect(info["coverage"]==1.0,"shared diagonal must not cause holes");expect(info["overlap_samples"]==0,"shared diagonal must not count as overlap");
        auto missing=mesh;missing.vertices[0].hasUv=false;expect(!tex::model::checkUvs(missing)["usable_for_preview"].get<bool>(),"missing UVs must be detected");
        auto outside=mesh;outside.vertices[1].uv.x=2;expect(tex::model::checkUvs(outside)["outside_0_1_triangles"].get<int>()>0,"outside UVs must be detected");
        auto invalid=mesh;invalid.vertices[1].uv.x=std::numeric_limits<float>::quiet_NaN();expect(tex::model::checkUvs(invalid)["nonfinite_uv_triangles"].get<int>()>0,"nonfinite UVs must be detected");
        auto overlap=mesh;overlap.triangles.push_back(overlap.triangles[0]);expect(tex::model::checkUvs(overlap)["overlap_samples"].get<int>()>0,"overlaps must be detected");
        rejects([&]{tex::model::bake(overlap,work/"bad",32,8,0,2,2,64,{"ao"});},"overlapping UVs cannot bake");
        auto degenerate=mesh;degenerate.vertices[1].uv=degenerate.vertices[0].uv;expect(tex::model::checkUvs(degenerate)["degenerate_uv_triangles"].get<int>()>0,"degenerate UV triangle missing");
        for(auto& v:missing.vertices)v.hasUv=false;tex::model::unwrap(missing,128,2);expect(tex::model::checkUvs(missing,128)["usable_for_baking"].get<bool>(),"xatlas should fix missing UVs");
        auto path=work/"unwrapped.obj";auto mtl=work/"unwrapped.mtl";std::filesystem::remove(path);std::filesystem::remove(mtl);tex::model::saveObj(missing,path);auto reloaded=tex::model::load(path);expect(tex::model::checkUvs(reloaded)["usable_for_baking"].get<bool>(),"exported OBJ UVs must round trip");rejects([&]{tex::model::saveObj(missing,path);},"UV output must refuse overwrite");
        auto reference=tex::model::load(source/"assets/models/preview-torus.obj");
        for(const char* file:{"torus.fbx","torus.glb","torus.gltf"}){auto loaded=tex::model::load(source/"tests/fixtures/models"/file);expect(loaded.triangles.size()==reference.triangles.size(),std::string(file)+" triangle count");expect(tex::model::checkUvs(loaded,128)["usable_for_baking"].get<bool>(),std::string(file)+" UV conversion");expect(tex::model::length(loaded.maximum-reference.maximum)<.001f,std::string(file)+" transform conversion");expect(loaded.materials==reference.materials,std::string(file)+" material slots");}
        auto miniature=reference;for(auto& v:miniature.vertices)v.position=v.position*.001f;tex::model::bounds(miniature);auto originalMaximum=miniature.maximum;tex::model::unwrap(miniature,256,4);expect(tex::model::checkUvs(miniature,256)["usable_for_baking"].get<bool>(),"small-scale geometry must unwrap without discarded faces");expect(tex::model::length(miniature.maximum-originalMaximum)==0,"atlas normalization must preserve model dimensions");
        auto withoutUv=tex::model::load(source/"tests/fixtures/models/torus-no-uv.obj");expect(!tex::model::checkUvs(withoutUv)["usable_for_preview"].get<bool>(),"no-UV OBJ fixture");
        auto memory=std::make_shared<tex::Memory>();memory->limit=32*1024*1024;
        auto result=tex::model::bake(mesh,work/"plane",32,32,2,2,1,64,{"ao","curvature","position","object_normal","materialids","coverage"});
        auto ao=tex::readPng(work/"plane/ao.png",tex::Kind::Scalar,false,memory);expect(*std::min_element(ao->pixels.begin(),ao->pixels.end())>.999,"unoccluded plane AO must be white");
        auto curv=tex::readPng(work/"plane/curvature.png",tex::Kind::Scalar,false,memory);expect(std::all_of(curv->pixels.begin(),curv->pixels.end(),[](float v){return std::abs(v-.5f)<.0001f;}),"flat plane curvature must be neutral");
        tex::model::bake(mesh,work/"plane-parallel",32,32,2,2,4,64,{"ao"});auto parallel=tex::readPng(work/"plane-parallel/ao.png",tex::Kind::Scalar,false,memory);expect(parallel->pixels==ao->pixels,"bakes must be deterministic across worker counts");
        tex::Json imported={{"size",32},{"imports",{{"mesh","plane/maps.json"}}},{"nodes",tex::Json::object()},{"outputs",{{"ao-copy.png","mesh.ao"}}}};tex::Options options;options.out=work/"imported";auto rendered=tex::Graph(imported,work,options).render();expect(rendered["reachable"]==1,"bake refs must execute through graph imports");
        auto torus=tex::model::load(source/"assets/models/preview-torus.obj");expect(torus.materials.size()==3,"OBJ material IDs were lost");tex::model::bake(torus,work/"torus",64,32,1,0,2,128,{"ao","thickness","curvature","materialids"});
        auto ta=tex::readPng(work/"torus/ao.png",tex::Kind::Scalar,false,memory),tt=tex::readPng(work/"torus/thickness.png",tex::Kind::Scalar,false,memory),tc=tex::readPng(work/"torus/curvature.png",tex::Kind::Scalar,false,memory);
        expect(*std::min_element(ta->pixels.begin(),ta->pixels.end())<.95f,"torus hole must occlude ambient light");expect(*std::max_element(ta->pixels.begin(),ta->pixels.end())>.95f,"exterior should remain exposed");
        expect(*std::max_element(tt->pixels.begin(),tt->pixels.end())<.95f&&*std::min_element(tt->pixels.begin(),tt->pixels.end())>.05f,"closed torus must have finite physical thickness");expect(*std::max_element(tc->pixels.begin(),tc->pixels.end())-*std::min_element(tc->pixels.begin(),tc->pixels.end())>.05f,"curvature must vary around torus");
        rejects([&]{tex::model::bake(mesh,work/"bad",32,8,0,2,2,1,{"unknown"});},"unknown maps must fail");rejects([&]{tex::model::bake(mesh,work/"bad",2048,8,0,2,2,1,{"ao"});},"insufficient bake memory must fail");
        if(tex::filamentAvailable()) {
            tex::Json doc;
            doc["nodes"]={{"color",{{"op","constant"},{"value","#807060"}}}};
            doc["outputs"]["color.png"]="color";
            doc["outputs"]["material.mtlx"]={{"type","materialx"},{"inputs",{{"base_color",{{"texture","color.png"}}}}}};
            doc["outputs"]["preview.png"]={{"type","preview"},{"model",(source/"assets/models/preview-torus.obj").string()},{"material","material.mtlx"},{"environment",(source/"assets/hdri/studio.hdr").string()},{"views",{{0,0,0},{0,90,0}}}};
            auto tri=doc;tri["outputs"]["preview.png"]["projection"]="triplanar";tri["outputs"]["preview.png"]["projection_scale"]=2.5;tex::Graph triValid(tri,source);
            tri["outputs"]["preview.png"]["projection_scale"]=0;rejects([&]{tex::Graph g(tri,source);},"zero projection scale must fail");
            tri=doc;tri["outputs"]["preview.png"]["projection"]="unknown";rejects([&]{tex::Graph g(tri,source);},"unknown projection must fail");
            auto multi=doc;auto& preview=multi["outputs"]["preview.png"];preview.erase("material");preview["materials"]={{"body","material.mtlx"},{"band",{{"graph",(source/"samples/cork.json").string()},{"material","cork.mtlx"},{"projection","triplanar"},{"projection_scale",3}}}};tex::Graph multiValid(multi,source);
            auto ordered=multi;auto& orderedPreview=ordered["outputs"]["preview.png"];orderedPreview["render_order"]="center_out";orderedPreview["materials"]["body"]={{"material","material.mtlx"},{"refraction","cubemap"},{"render_order",1},{"render_channel",2},{"culling","back"}};
            auto parsed=tex::parsePreview(orderedPreview,source);expect(parsed["render_order"]=="center_out"&&parsed["materials"]["body"]["render_order"]==1&&parsed["materials"]["body"]["refraction"]=="cubemap","preview ordering and cubemap bindings must survive parsing");
            for(const auto& field:std::vector<std::pair<std::string,tex::Json>>{{"render_order",8},{"render_order",1.5},{"render_channel",1},{"render_channel",8},{"culling","side"},{"refraction","unknown"}}){auto invalid=ordered;invalid["outputs"]["preview.png"]["materials"]["body"][field.first]=field.second;rejects([&]{tex::Graph g(invalid,source);},"invalid ordering or refraction control must fail");}
            auto invalidOrdering=ordered;invalidOrdering["outputs"]["preview.png"]["render_order"]="nearest";rejects([&]{tex::Graph g(invalidOrdering,source);},"unknown scene ordering must fail");
            auto missingGraph=multi;missingGraph["outputs"]["preview.png"]["materials"]["band"]["graph"]="missing-material.json";rejects([&]{tex::Graph g(missingGraph,source);},"missing external recipe must fail");
            auto badMaterial=multi;badMaterial["outputs"]["preview.png"]["materials"]["body"]="missing.mtlx";rejects([&]{tex::Graph g(badMaterial,source);},"unknown local slot material must fail");
            auto only=multi;only["nodes"]=tex::Json::object();only["outputs"]={{"preview.png",preview}};only["outputs"]["preview.png"]["materials"]["body"]=(source/"samples/glass.json").string();tex::Graph onlyPreview(only,source);
            tex::Graph valid(doc,source);auto bad=doc;bad["outputs"]["preview.png"]["views"]={{0,0}};rejects([&]{tex::Graph g(bad,source);},"invalid rotations must fail");bad=doc;bad["outputs"]["preview.png"]["material"]="color.png";rejects([&]{tex::Graph g(bad,source);},"preview must reference a material");bad=doc;bad["outputs"]["material.mtlx"]["inputs"]["base_color"]["texture"]="preview.png";rejects([&]{tex::Graph g(bad,source);},"preview cannot feed its own material");
        }
        std::cout<<"Model UV, xatlas, geometry baking, imports and preview validation passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
