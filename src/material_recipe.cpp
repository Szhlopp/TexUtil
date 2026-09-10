#include "texutil/material_recipe.hpp"
#include <cmath>
#include <fstream>
#include <set>
#include <stdexcept>
namespace tex {
namespace {
void require(bool ok, const std::string& message) { if(!ok)throw std::runtime_error("material recipe: "+message); }
void bound(const Json& value, float lo, float hi, bool integer, const std::string& name) { require(value.is_number()&&(!integer||value.is_number_integer()),name+" has wrong numeric type");double n=value.get<double>();require(std::isfinite(n)&&n>=lo&&n<=hi,name+" out of range"); }
}
Json parseMaterialBinding(Json value, const std::filesystem::path& base, bool bake) {
    if(value.is_string()){auto path=value.get<std::string>();value=std::filesystem::path(path).extension()==".json"?Json{{"graph",path}}:Json{{"material",path}};}
    require(value.is_object(),"binding must be an object or filename");
    std::set<std::string> allowed={"graph","material","projection","projection_scale","projection_blend","thickness","refraction","render_order","render_channel","culling"};
    if(bake)for(auto key:{"height","height_scale","height_midlevel"})allowed.insert(key);
    for(auto it=value.begin();it!=value.end();++it)require(allowed.count(it.key()),"unknown binding field '"+it.key()+"'");
    require(value.contains("graph")||value.contains("material"),"binding requires graph or material");
    if(value.contains("material"))require(value["material"].is_string()&&!value["material"].get<std::string>().empty(),"material must be an output filename");
    if(value.contains("graph")){require(value["graph"].is_string(),"graph must be a JSON filename");auto p=std::filesystem::weakly_canonical(base/value["graph"].get<std::string>());require(std::filesystem::is_regular_file(p),"graph does not exist: "+p.string());value["graph"]=p.string();}
    if(value.contains("projection"))require(value["projection"]=="uv"||value["projection"]=="triplanar","projection must be uv or triplanar");
    if(value.contains("refraction"))require(value["refraction"]=="auto"||value["refraction"]=="opaque"||value["refraction"]=="cubemap","refraction must be auto, opaque or cubemap");
    if(value.contains("culling"))require(value["culling"]=="none"||value["culling"]=="back"||value["culling"]=="front","culling must be none, back or front");
    for(auto key:{"projection_scale","projection_blend","thickness","render_order","render_channel","height_scale","height_midlevel"})if(value.contains(key)){
        std::string k=key;float lo=0,hi=10;bool integer=false;
        if(k=="projection_scale"){lo=.0001f;hi=10000;}if(k=="projection_blend"){lo=1;hi=16;}if(k=="render_order"){hi=7;integer=true;}if(k=="render_channel"){lo=2;hi=7;integer=true;}if(k=="height_scale"||k=="height_midlevel"){lo=-10000;hi=10000;}
        bound(value[key],lo,hi,integer,key);
    }
    if(value.contains("height"))require(value["height"].is_string()&&!value["height"].get<std::string>().empty(),"height must name a scalar PNG output");
    return value;
}
MaterialRecipe loadMaterialRecipe(const Json& binding, const std::vector<std::string>& extraTextures) {
    require(binding.contains("graph"),"external graph required");auto path=std::filesystem::path(binding.at("graph").get<std::string>());
    require(std::filesystem::file_size(path)<=8*1024*1024,"graph exceeds 8 MiB");std::ifstream file(path);require(bool(file),"cannot read graph");Json doc=Json::parse(file);
    require(doc.contains("outputs")&&doc["outputs"].is_object(),"graph needs outputs");std::string name=binding.value("material",std::string{});
    if(name.empty())for(auto it=doc["outputs"].begin();it!=doc["outputs"].end();++it)if(it.value().is_object()&&it.value().value("type",std::string{})=="materialx"){require(name.empty(),"multiple MaterialX outputs; specify material: "+path.string());name=it.key();}
    require(!name.empty()&&doc["outputs"].contains(name),"selected material output is missing");Json material=parseMaterialX(doc["outputs"][name]);
    std::set<std::string> textures(extraTextures.begin(),extraTextures.end());
    std::function<void(const Json&)> collect=[&](const Json& v){if(v.is_object()){if(v.contains("texture")&&v["texture"].is_string())textures.insert(v["texture"].get<std::string>());for(const auto& child:v)collect(child);}else if(v.is_array())for(const auto& child:v)collect(child);};collect(material);
    Json outputs={{name,doc["outputs"][name]}};
    for(const auto& texture:textures){require(doc["outputs"].contains(texture),"missing texture output: "+texture);auto o=doc["outputs"][texture];require(!o.is_object()||o.value("type",std::string("image"))=="image","texture must reference an image output");outputs[texture]=o;}
    doc["outputs"]=outputs;return {doc,material,path.parent_path(),name};
}
}
