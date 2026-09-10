#include "texutil/texutil.hpp"
#include <cmath>
#include <iomanip>
#include <locale>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace tex {
namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error("MaterialX: " + message); }
void fields(const Json& object, const std::set<std::string>& allowed) {
    require(object.is_object(), "expected an object");
    for (auto it=object.begin();it!=object.end();++it) require(allowed.count(it.key()), "unknown field '"+it.key()+"'");
}
bool identifier(const std::string& s) {
    auto letter=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';};
    if (s.empty()||s.size()>128||!letter(s.front())) return false;
    for(char c:s) if(!letter(c)&&!(c>='0'&&c<='9')) return false;
    return true;
}
std::string xml(const std::string& value) {
    std::string out;
    for(unsigned char c:value) {
        require(c>=32, "XML strings cannot contain control characters");
        switch(c) { case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\'':out+="&apos;";break;default:out+=static_cast<char>(c); }
    }
    return out;
}
double number(const Json& value) {
    require(value.is_number(), "expected a finite number"); double n=value.get<double>();
    require(std::isfinite(n)&&std::abs(n)<=std::numeric_limits<float>::max(), "expected a finite number representable as a MaterialX float"); return n;
}
Json valueFor(const Json& value, const std::string& type) {
    if(type=="float") return number(value);
    if(type=="boolean") {require(value.is_boolean(),"expected boolean");return value;}
    if(type=="color3"&&value.is_string()) {
        auto p=color(value);require(p[3]==1,"color inputs must be opaque");return Json::array({p[0],p[1],p[2]});
    }
    require(value.is_array()&&value.size()==3,"expected three components for "+type);
    Json result=Json::array();for(const auto& v:value)result.push_back(number(v));return result;
}
std::string texture(const Json& value) {
    require(value.is_string(), "texture must name an exported PNG filename");auto name=value.get<std::string>();
    require(!name.empty()&&name.find('\\')==std::string::npos&&name.find(':')==std::string::npos,"texture needs a portable relative filename");
    xml(name);return name;
}
using Binding = std::pair<std::string,std::string>;
std::vector<Binding> bindings(const Json& material) {
    std::vector<Binding> result;
    for(auto it=material["inputs"].begin();it!=material["inputs"].end();++it) if(it.value().is_object()) result.emplace_back(it.value()["texture"].get<std::string>(),materialXInputs().at(it.key())["type"].get<std::string>());
    if(material.contains("normal"))result.emplace_back(material["normal"]["texture"].get<std::string>(),"normal");
    if(material.contains("displacement"))result.emplace_back(material["displacement"]["texture"].get<std::string>(),"float");
    return result;
}
const Output& findOutput(const std::vector<Output>& outputs, const std::string& name) {
    for(const auto& o:outputs)if(o.name==name)return o;
    throw std::runtime_error("MaterialX: texture references unknown output '"+name+"' (use its final filename, including extension)");
}
std::string valueText(const Json& value) {
    if(value.is_boolean())return value.get<bool>()?"true":"false";
    std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17);
    if(value.is_array()) {for(size_t i=0;i<value.size();++i){if(i)s<<", ";s<<value[i].get<double>();}}
    else s<<value.get<double>();return s.str();
}
std::string input(const std::string& name, const std::string& type, const std::string& attribute, const std::string& value) { return "    <input name=\""+name+"\" type=\""+type+"\" "+attribute+"=\""+xml(value)+"\" />\n"; }
std::string node(const std::string& category, const std::string& name, const std::string& type, const std::string& inputs, const std::string& extra = "") { return "  <"+category+" name=\""+name+"\" type=\""+type+"\""+extra+">\n"+inputs+"  </"+category+">\n"; }
}
Json materialXInputs() {
    static const Json schema=Json::parse(
#include "materialx_inputs.inc"
    );return schema;
}
Json parseMaterialX(const Json& output) {
    fields(output,{"type","name","version","texture_paths","shader","inputs","normal","displacement"});
    Json result=output;
    auto version=output.value("version",Json("1.38"));require(version=="1.38"||version=="1.39","version must be 1.38 or 1.39 (string)");result["version"]=version;
    auto paths=output.value("texture_paths",Json("relative"));require(paths=="relative"||paths=="absolute","texture_paths must be relative or absolute");result["texture_paths"]=paths;
    require(output.value("shader",Json("standard_surface"))=="standard_surface","shader must be standard_surface");
    auto name=output.value("name",Json("Material"));require(name.is_string()&&identifier(name.get<std::string>()),"name must be an ASCII identifier, maximum 128 characters");result["name"]=name;
    result["inputs"]=output.value("inputs",Json::object());require(result["inputs"].is_object(),"inputs must be an object");
    auto schema=materialXInputs();
    for(auto it=result["inputs"].begin();it!=result["inputs"].end();++it) {
        require(schema.contains(it.key()),"unknown Standard Surface input '"+it.key()+"'; see texutil materialx");
        std::string type=schema[it.key()]["type"];
        if(it.value().is_object()) {fields(it.value(),{"texture"});require(it.value().contains("texture"),"texture binding needs texture");require(type!="boolean","boolean inputs cannot use textures");it.value()["texture"]=texture(it.value()["texture"]);}
        else it.value()=valueFor(it.value(),type);
    }
    if(output.contains("normal")) {
        auto v=output["normal"];fields(v,{"texture","convention","scale"});require(v.contains("texture"),"normal needs texture");v["texture"]=texture(v["texture"]);
        v["convention"]=v.value("convention",Json("directx"));require(v["convention"]=="directx"||v["convention"]=="opengl","normal convention must be directx or opengl");
        v["scale"]=number(v.value("scale",Json(1)));require(v["scale"].get<double>()>=0,"normal scale must be nonnegative");
        require(!result["inputs"].contains("normal"),"normal helper conflicts with inputs.normal");result["normal"]=v;
    }
    if(output.contains("displacement")) {
        auto v=output["displacement"];fields(v,{"texture","scale","midlevel"});require(v.contains("texture"),"displacement needs texture");v["texture"]=texture(v["texture"]);
        v["scale"]=number(v.value("scale",Json(0.01)));v["midlevel"]=number(v.value("midlevel",Json(0.5)));result["displacement"]=v;
    }
    return result;
}
void validateMaterialXReferences(const std::vector<Output>& outputs) {
    for(const auto& out:outputs)if(out.material)for(const auto& [name,type]:bindings(*out.material)) {
        const auto& source=findOutput(outputs,name);
        require(!source.sheet&&!source.material&&!source.preview&&!source.bake&&source.format=="png","texture '"+name+"' must reference a regular PNG output");
    }
}
void validateMaterialXImage(const std::vector<Output>& outputs, const Output& output, Kind kind) {
    for(const auto& out:outputs)if(out.material)for(const auto& [name,type]:bindings(*out.material))if(name==output.name) {
        if(type=="float")require(kind==Kind::Scalar,"scalar texture '"+name+"' needs a grayscale node (use grayscale or a channel-selection node)");
        if(type=="normal"||type=="vector3")require(kind!=Kind::Scalar&&(kind!=Kind::Color||!output.srgb),"normal/vector texture '"+name+"' needs raw RGB data, not scalar or sRGB color");
        if(type=="color3")require(kind!=Kind::Normal,"normal data cannot be used as color texture '"+name+"'");
    }
}
std::string materialXDocument(const Output& output, const std::vector<Output>& outputs, const std::map<std::string, Kind>& kinds, bool tile, const std::filesystem::path& outputDirectory) {
    const auto& m=*output.material;std::string prefix="tx_"+m["name"].get<std::string>()+"_", body,surface;
    auto image=[&](const std::string& key,const std::string& file,const std::string& type) {
        const auto& source=findOutput(outputs,file);
        auto parent=std::filesystem::path(output.name).parent_path();if(parent.empty())parent=".";
        auto path=m.value("texture_paths",std::string("relative"))=="absolute"?std::filesystem::absolute(outputDirectory/file).lexically_normal().generic_string():std::filesystem::path(file).lexically_relative(parent).generic_string();require(!path.empty(),"cannot make texture path");
        std::string colorspace=type=="color3"&&kinds.at(file)==Kind::Color&&source.srgb?"srgb_texture":"lin_rec709";
        auto id=prefix+key+"_image";
        body+=node("image",id,type,input("file","filename","value",path)+input("uaddressmode","string","value",tile?"periodic":"clamp")+input("vaddressmode","string","value",tile?"periodic":"clamp")," colorspace=\""+colorspace+"\"");return id;
    };
    auto schema=materialXInputs();
    for(auto it=m["inputs"].begin();it!=m["inputs"].end();++it) {
        auto type=schema[it.key()]["type"].get<std::string>();
        surface+=it.value().is_object()?input(it.key(),type,"nodename",image(it.key(),it.value()["texture"],type)):input(it.key(),type,"value",valueText(it.value()));
    }
    if(m.contains("normal")) {
        const auto& normal=m["normal"];auto id=image("normal",normal["texture"],"vector3");
        if(normal["convention"]=="directx") {
            body+=node("multiply",prefix+"normal_flip","vector3",input("in1","vector3","nodename",id)+input("in2","vector3","value","1, -1, 1"));
            body+=node("add",prefix+"normal_bias","vector3",input("in1","vector3","nodename",prefix+"normal_flip")+input("in2","vector3","value","0, 1, 0"));id=prefix+"normal_bias";
        }
        // Tangent space is the 1.38 default; 1.39 removed the space input entirely.
        body+=node("normalmap",prefix+"normal_map","vector3",input("in","vector3","nodename",id)+input("scale","float","value",valueText(normal["scale"])));
        surface+=input("normal","vector3","nodename",prefix+"normal_map");
    }
    body+=node("standard_surface",prefix+"surface","surfaceshader",surface," version=\"1.0.1\"");
    std::string material=input("surfaceshader","surfaceshader","nodename",prefix+"surface");
    if(m.contains("displacement")) {
        const auto& d=m["displacement"];auto id=image("height",d["texture"],"float");
        body+=node("subtract",prefix+"height_centered","float",input("in1","float","nodename",id)+input("in2","float","value",valueText(d["midlevel"])));
        body+=node("displacement",prefix+"displacement","displacementshader",input("displacement","float","nodename",prefix+"height_centered")+input("scale","float","value",valueText(d["scale"])));
        material+=input("displacementshader","displacementshader","nodename",prefix+"displacement");
    }
    body+=node("surfacematerial",m["name"],"material",material);
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<materialx version=\""+m.value("version",std::string("1.38"))+"\" colorspace=\"lin_rec709\">\n"+body+"</materialx>\n";
}
}
