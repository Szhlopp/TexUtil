#include "texutil/model.hpp"
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace tex::model {
namespace {
int integer(const std::string& text, int low, int high) { size_t end=0;int value=std::stoi(text,&end);if(end!=text.size()||value<low||value>high)throw std::runtime_error("model option integer out of range: "+text);return value; }
}
int command(int argc, char** argv) {
    if(argc<3||std::string(argv[2])=="--help") {
        std::cout<<R"(Model operations (static OBJ, FBX, glTF and GLB):
  texutil model check-uvs model.obj [--size 512] [--json]
  texutil model check uvs model.obj [same options]
  texutil model uv model.fbx --out model-uv.obj [--size 1024] [--padding 4] [--json]
  texutil model bake model-uv.obj --out out/bake [--size 1024] [--samples 64]
         [--maps curvature,ao,thickness,materialids,position,object_normal,coverage]
         [--distance N] [--padding 4] [--threads N] [--memory 1024] [--json]

UV checks report sampled overlaps; exit 1 means unsuitable for unique-atlas baking.
Unwrap writes a new OBJ/MTL pair and refuses existing paths. Preserve the source.
Bake writes 16-bit linear PNGs plus maps.json for graph imports and bake-info.json.
Distance is in mesh units; default is the bounding-box diagonal. Sizes up to 4096.
Preview rotations, HDR lighting and material bindings use type:preview JSON outputs.
See docs/MODELS.md.
)";return 0;
    }
    std::string action=argv[2];int pos=3;
    if(action=="check"){if(pos>=argc||std::string(argv[pos++])!="uvs")throw std::runtime_error("expected model check uvs FILE");action="check-uvs";}
    if(action!="check-uvs"&&action!="uv"&&action!="bake")throw std::runtime_error("unknown model operation: "+action);
    if(pos>=argc)throw std::runtime_error("missing model filename");std::filesystem::path input=argv[pos++],out;
    int size=action=="check-uvs"?512:1024,padding=4;unsigned samples=64,threads=0;size_t memory=1024;float distance=0;bool json=false;
    std::vector<std::string> maps={"curvature","ao","thickness","materialids","position","object_normal","coverage"};
    std::set<std::string> allowed=action=="check-uvs"?std::set<std::string>{"--size"}:action=="uv"?std::set<std::string>{"--out","--size","--padding"}:std::set<std::string>{"--out","--size","--padding","--samples","--threads","--memory","--distance","--maps"};
    for(;pos<argc;++pos){std::string arg=argv[pos];if(arg=="--json"){json=true;continue;}if(!allowed.count(arg))throw std::runtime_error("unknown model option: "+arg);if(++pos>=argc)throw std::runtime_error("missing value for "+arg);std::string value=argv[pos];
        if(arg=="--out")out=value;if(arg=="--size")size=integer(value,32,4096);if(arg=="--padding")padding=integer(value,0,64);if(arg=="--samples")samples=integer(value,1,4096);if(arg=="--threads")threads=integer(value,1,256);if(arg=="--memory")memory=integer(value,1,1048576);
        if(arg=="--distance"){size_t end=0;distance=std::stof(value,&end);if(end!=value.size()||!std::isfinite(distance)||distance<=0)throw std::runtime_error("distance must be finite and positive");}
        if(arg=="--maps"){maps.clear();std::istringstream stream(value);std::string map;while(std::getline(stream,map,','))maps.push_back(map=="curv"?"curvature":map);}
    }
    if(action!="check-uvs"&&out.empty())throw std::runtime_error("model uv/bake requires --out");
    auto mesh=load(input);Json result;
    if(action=="uv"){unwrap(mesh,size,padding);saveObj(mesh,out);result=checkUvs(mesh,std::min(size,1024));result["file"]=out.string();}
    else if(action=="bake"){result=bake(mesh,out,size,samples,distance,padding,threads,memory,maps);result["graph"]=(out/"maps.json").string();}
    else result=checkUvs(mesh,size);
    result["input"]=input.string();result["operation"]=action;
    if(json)std::cout<<result.dump(2)<<'\n';
    else if(action=="bake")std::cout<<"Baked "<<result["files"].size()<<" maps. Import "<<(out/"maps.json").string()<<'\n';
    else {std::cout<<result.dump(2)<<'\n';if(action=="uv")std::cout<<"Use the new OBJ for both baking and previews.\n";}
    return action=="check-uvs"&&!result["usable_for_baking"].get<bool>()?1:0;
}
}
