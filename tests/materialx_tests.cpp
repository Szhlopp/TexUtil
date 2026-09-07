#include "texutil/texutil.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace tex;
namespace {
void check(bool ok, const std::string& message) { if(!ok)throw std::runtime_error(message); }
Json document() {
    return Json::parse(R"json({
      "size":16,"tile":true,
      "nodes":{"color":{"op":"constant","value":"#803020"},"height":{"op":"constant","value":0.6},"normal":{"op":"normal","input":"height","convention":"directx"}},
      "outputs":{
        "textures/a&b.png":"color","height.png":{"node":"height","bits":16},"normal.png":"normal",
        "materials/test.mtlx":{"type":"materialx","name":"Test","inputs":{"base_color":{"texture":"textures/a&b.png"},"specular_roughness":{"texture":"height.png"},"specular_IOR":1.33,"transmission_color":"#804020","thin_walled":true,"coat":0.4},"normal":{"texture":"normal.png","convention":"directx"},"displacement":{"texture":"height.png","scale":0.035,"midlevel":0.5}}
      }
    })json");
}
void rejects(Json doc, const std::string& expected, bool render = false) {
    bool failed=false;try{Graph g(doc,".");if(render)g.render([](const Output&,const ImagePtr&){});}catch(const std::exception& e){failed=std::string(e.what()).find(expected)!=std::string::npos;}
    check(failed,"expected error containing: "+expected);
}
void test(const std::filesystem::path& work) {
    auto d=document();std::string text;int images=0,materials=0;
    auto stats=Graph(d,".").render([&](const Output& o,const ImagePtr& image){if(o.material){check(!image,"material callback has no image");check(images==3,"textures precede material");text=o.text;++materials;}else{check(bool(image),"image callback valid");++images;}});
    check(images==3&&materials==1,"output callback counts");check(stats["timings"].size()==3,"no additional raster execution for materials");
    check(stats["output_details"].back()["type"]=="materialx","stats identify MaterialX");
    check(text.find("../textures/a&amp;b.png")!=std::string::npos,"relative XML-escaped paths");
    check(text.find("colorspace=\"srgb_texture\"")!=std::string::npos,"color texture encoding");
    check(text.find("colorspace=\"lin_rec709\"")!=std::string::npos,"data stays linear");
    check(text.find("1, -1, 1")!=std::string::npos&&text.find("0, 1, 0")!=std::string::npos,"DirectX green inversion");
    check(text.find("displacementshader")!=std::string::npos,"displacement is bound to material");
    auto m=parseMaterialX(d["outputs"]["materials/test.mtlx"]);check(std::abs(m["inputs"]["transmission_color"][0].get<float>()-toLinear(128.f/255))<1e-6f,"hex colors converted to linear");
    auto bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["ior"]=1.5;rejects(bad,"unknown Standard Surface input");
    bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["base_color"]={{"texture","missing.png"}};rejects(bad,"unknown output");
    bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["thin_walled"]=1;rejects(bad,"boolean");
    bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["specular_IOR"]=std::numeric_limits<double>::infinity();rejects(bad,"finite number");
    bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["specular_IOR"]=1e300;rejects(bad,"MaterialX float");
    bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["base_color"]={1,2};rejects(bad,"three components");
    bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["base_color"]="#ffffff80";rejects(bad,"opaque");
    bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["normal"]={0,0,1};rejects(bad,"conflicts");
    bad=d;bad["outputs"]["materials/test.mtlx"]["normal"]["convention"]="bad";rejects(bad,"convention");
    bad=d;bad["outputs"]["materials/test.mtlx"]["normal"]["scale"]=-1;rejects(bad,"nonnegative");
    bad=d;bad["outputs"]["materials/test.mtlx"]["name"]="bad\"name";rejects(bad,"identifier");
    bad=d;bad["outputs"]["materials/test.mtlx"]["bits"]=16;rejects(bad,"unknown field");
    bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["base_color"]={{"texture","foo\n.png"}};rejects(bad,"control characters");
    bad=d;bad["outputs"]["sheet.png"]={{"type","sheet"},{"items",{"color"}}};bad["outputs"]["materials/test.mtlx"]["inputs"]["base_color"]={{"texture","sheet.png"}};rejects(bad,"regular PNG");
    bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["base_color"]={{"texture","materials/test.mtlx"}};rejects(bad,"regular PNG");
    bad=d;bad["outputs"]["materials/test.mtlx"]["inputs"]["specular_roughness"]={{"texture","textures/a&b.png"}};rejects(bad,"grayscale node",true);
    bad=d;bad["outputs"]["materials/test.mtlx"]["normal"]["texture"]="textures/a&b.png";rejects(bad,"raw RGB",true);
    bad=d;bad["outputs"]["../escape.mtlx"]=bad["outputs"]["materials/test.mtlx"];rejects(bad,"cannot contain");
    bad=d;bad["outputs"]["materials/test"]=bad["outputs"]["materials/test.mtlx"];rejects(bad,"duplicate output");
    bad=d;bad["outputs"]["wrong.png"]=bad["outputs"]["materials/test.mtlx"];rejects(bad,"extension must match");
    // Emit fixtures for independent validation with the official MaterialX SDK.
    Options options;options.out=work;options.threads=1;Graph(d,".",options).render();
    d["outputs"]["linear.png"]={{"node","color"},{"srgb",false}};
    d["outputs"]["materials/linear.mtlx"]={{"type","materialx"},{"name","Linear"},{"inputs",{{"base_color",{{"texture","linear.png"}}}}},{"normal",{{"texture","normal.png"},{"convention","opengl"}}}};
    Json all=Json::object();auto schema=materialXInputs();for(auto it=schema.begin();it!=schema.end();++it)all[it.key()]=it.value().value("default",Json::array({0,0,1}));
    d["outputs"]["all-inputs.mtlx"]={{"type","materialx"},{"name","AllInputs"},{"inputs",all}};
    Graph(d,".",options).render();
    Json constants={{"size",1},{"nodes",{{"unused",{{"op","constant"}}}}},{"outputs",{{"constant.mtlx",{{"type","materialx"},{"inputs",{{"base_color","#abcdef"},{"specular_IOR",1.4}}}}}}}};
    stats=Graph(constants,".",options).render();check(stats["timings"].empty(),"constant-only material skips raster nodes");
}
}
int main(int argc, char** argv) { try{check(argc==2,"expected test output directory");test(argv[1]);std::cout<<"PASS MaterialX export, inputs, paths, encodings and validation\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL MaterialX: "<<e.what()<<'\n';return 1;} }
