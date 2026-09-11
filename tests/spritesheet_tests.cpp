#include "texutil/texutil.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace tex;
namespace {
void check(bool ok, const std::string& text) { if(!ok) throw std::runtime_error(text); }
void save(const std::filesystem::path& path, const Json& value) { std::ofstream file(path); file<<value.dump(2); }
std::string bytes(const std::filesystem::path& path) { std::ifstream file(path,std::ios::binary); std::ostringstream out; out<<file.rdbuf(); return out.str(); }
void rejects(const std::function<void()>& fn, const std::string& text) { try { fn(); } catch(const std::exception& e) { check(std::string(e.what()).find(text)!=std::string::npos,"unexpected error: "+std::string(e.what())); return; } throw std::runtime_error("expected error: "+text); }
void test(const std::filesystem::path& work) {
    std::filesystem::create_directories(work);
    Json source={{"nodes",{{"noise",{{"op","simplex"},{"scale",4}}},{"color",{{"op","ramp"},{"input","noise"},{"stops",{{0,{.1,.2,.3,.25}},{1,{.8,.4,.1,1}}}}}},{"normal",{{"op","normal"},{"input","noise"},{"strength",.01}}}}},
        {"outputs",{{"color.png",{{"node","color"},{"alpha",true}}},{"normal.png",{{"node","normal"},{"bits",16}}},{"height.png",{{"node","noise"},{"bits",16}}}}}};
    save(work/"source.json",source);
    Json document={{"size",{16,12}},{"seed",42},{"nodes",Json::object()},{"outputs",{{"atlas.json",{{"type","spritesheet"},{"source","source.json"},{"columns",2},{"rows",2},{"count",3},{"padding",2}}}}}};
    Options options; options.out=work/"result"; options.threads=1;
    Graph(document,work,options).render();
    auto manifest=Json::parse(bytes(options.out/"atlas.json"));
    check(manifest["image_size"]==Json({32,24})&&manifest["content_size"]==Json({12,8})&&manifest["count"]==3,"atlas layout and padding");
    check(manifest["cells"][2]["row"]==1&&manifest["cells"][2]["column"]==0&&manifest["cells"][2]["seed"]==44,"row-major cells with consecutive seeds");
    check(manifest["cells"][0]["uv_rect"]==Json({2.0/32,2.0/24,14.0/32,10.0/24}),"UVs exclude padding and use top-left image coordinates");
    std::map<std::string,ImagePtr> atlases;
    auto memory=std::make_shared<Memory>();
    for(const auto& image:manifest["images"]) {
        auto kind=image["kind"]=="color"?Kind::Color:image["kind"]=="scalar"?Kind::Scalar:Kind::Normal;
        atlases[image["source_output"]]=readPng(options.out/image["file"].get<std::string>(),kind,image["srgb"],memory);
    }
    for(int i=0;i<3;++i) {
        Options cell; cell.width=12; cell.height=8; cell.seed=42+i; cell.threads=1;
        Graph(source,work,cell).render([&](const Output& output, const ImagePtr& image) {
            auto atlas=atlases.at(output.name); int x0=(i%2)*16+2,y0=(i/2)*12+2;
            for(int y=-2;y<10;++y) for(int x=-2;x<14;++x) {
                auto expected=image->get(std::clamp(x,0,11),std::clamp(y,0,7)),actual=atlas->get(x0+x,y0+y);
                for(int c=0;c<4;++c) check(std::abs(expected[c]-actual[c])<(output.bits==16?.00003f:.005f),"atlas content, encoding and edge extrusion match source render");
            }
        });
    }
    check(atlases.at("color.png")->get(24,18)[3]==0,"unused color cells are transparent");
    auto original=bytes(options.out/"atlas.assets/color.png");
    Options parallel=options; parallel.threads=4; parallel.out=work/"parallel";
    Graph(document,work,parallel).render();
    for(const auto& image:manifest["images"]) check(bytes(options.out/image["file"].get<std::string>())==bytes(parallel.out/image["file"].get<std::string>()),"spritesheets repeat across thread counts");
    auto changed=document; changed["seed"]=100; parallel.out=work/"changed"; Graph(changed,work,parallel).render();
    check(original!=bytes(parallel.out/"atlas.assets/color.png"),"new seed changes sprites");
    source["nodes"]["noise"]["seed"]=5; save(work/"source.json",source); parallel.out=work/"pinned"; Graph(document,work,parallel).render();
    auto pinned=readPng(parallel.out/"atlas.assets/color.png",Kind::Color,true,memory);
    for(int y=0;y<8;++y) for(int x=0;x<12;++x) check(pinned->get(x+2,y+2)==pinned->get(x+18,y+2),"explicit source node seeds remain pinned across cells");
    auto bad=document; bad["outputs"]["atlas.json"]["columns"]=0; rejects([&]{Graph(bad,work,options);},"columns");
    bad=document; bad["outputs"]["atlas.json"]["count"]=5; rejects([&]{Graph(bad,work,options);},"capacity");
    bad=document; bad["outputs"]["atlas.json"]["padding"]=6; rejects([&]{Graph(bad,work,options);},"content");
    bad=document; bad["size"]=16384; rejects([&]{Graph(bad,work,options);},"canvas");
    bad=document; bad["outputs"]["atlas.json"]["seed"]=2147483647; rejects([&]{Graph(bad,work,options);},"seed exceeds");
    bad=document; bad["outputs"]["atlas.json"]["colums"]=2; rejects([&]{Graph(bad,work,options);},"unknown field");
    bad=document; bad["nodes"]["constant"]={{"op","constant"}}; bad["outputs"]["atlas.assets/intruder.png"]="constant"; rejects([&]{Graph(bad,work,options);},"reserved spritesheet");
    rejects([&]{Graph(document,work,options).render([](const Output&,const ImagePtr&){});},"disk exports");
    bad=document; bad["outputs"]={{"source.json",document["outputs"]["atlas.json"]}}; Options overwrite=options; overwrite.out=work; auto before=bytes(work/"source.json");
    rejects([&]{Graph(bad,work,overwrite);},"overwrite spritesheet input"); check(bytes(work/"source.json")==before,"source recipe protected");
    auto recursive=source; recursive["outputs"]=document["outputs"]; save(work/"source.json",recursive); rejects([&]{Graph(document,work,options);},"only image"); save(work/"source.json",source);
    bad=document; bad["size"]=64; bad["outputs"]["atlas.json"]["columns"]=16; bad["outputs"]["atlas.json"]["rows"]=16; Options tiny=options; tiny.memoryMb=1; tiny.out=work/"memory-failure";
    rejects([&]{Graph(bad,work,tiny).render();},"memory budget"); check(!std::filesystem::exists(tiny.out/"atlas.json"),"memory failure does not write a manifest");
    auto inputDir=options.out/"atlas.assets";
    source["nodes"]["input"]={{"op","image"},{"path",(inputDir/"color.png").string()}}; save(work/"source.json",source);
    rejects([&]{Graph(document,work,options);},"overlaps an input asset");
}
}
int main(int argc, char** argv) { try { check(argc==2,"expected test directory"); test(std::filesystem::absolute(argv[1])); std::cout<<"PASS spritesheet layout, UVs, padding, channels, seeds, budgets and input protection\n"; return 0; } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
