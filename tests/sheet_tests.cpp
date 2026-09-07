#include "texutil/texutil.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace tex;
namespace {
void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
void near(float a, float b, float tolerance = 1e-5f) { check(std::abs(a-b)<=tolerance,"expected "+std::to_string(b)+", got "+std::to_string(a)); }
Json sheet(Json items) { return {{"type","sheet"},{"columns",2},{"cell",32},{"padding",4},{"labels",false},{"items",items}}; }
Json doc(Json nodes, Json output) { return {{"size",32},{"nodes",nodes},{"outputs",{{"sheet.png",output}}}}; }
ImagePtr render(Json document, unsigned threads = 1) { Options options;options.threads=threads;ImagePtr result;Graph(document,".",options).render([&](const Output& o,const ImagePtr& image){if(o.sheet)result=image;});return result; }
void rejects(Json document, const std::string& text) { bool failed=false;try{Graph(document,".");}catch(const std::exception& e){failed=std::string(e.what()).find(text)!=std::string::npos;}check(failed,"expected validation error: "+text); }
void testSheet() {
    Json nodes={{"height",{{"op","constant"},{"value",.5}}},{"normal",{{"op","normal"},{"input","height"}}}};
    auto document=doc(nodes,sheet({"height","normal"}));auto image=render(document);
    check(image->width==76&&image->height==40,"sheet dimensions");
    near(toSrgb(image->get(20,20)[0]),.5);near(toSrgb(image->get(56,20)[0]),.5);near(toSrgb(image->get(56,20)[1]),.5);near(image->get(56,20)[2],1);
    check(render(document,4)->pixels==image->pixels,"sheet worker determinism");
    nodes["color"]={{"op","constant"},{"value",{.5,.5,.5}}};document=doc(nodes,sheet({"height","color"}));image=render(document);
    near(toSrgb(image->get(20,20)[0]),.5);near(toSrgb(image->get(56,20)[0]),toSrgb(.5));
    document["alpha"]=true;document["srgb"]=false;document["format"]="pfm";document["bits"]=32;
    image=render(document);near(image->get(20,20)[3],1); // Presentation defaults are independent of data-export defaults.
    auto both=doc(nodes,sheet({"height","height"}));
    both["outputs"]["sheet.png"]["items"]=Json::array({{{"node","height"},{"label","Repeated A"}},{{"node","height"},{"label","Repeated B"}}});
    both["outputs"]["height.png"]="height";both["outputs"]["other-sheet.png"]=sheet({"height"});int callbacks=0;
    auto stats=Graph(both,".").render([&](const Output&,const ImagePtr&){++callbacks;});check(callbacks==3,"each sheet and regular output emitted once");check(stats["timings"].size()==1,"sheet-only dependencies execute once, unused nodes pruned");
    check(stats["output_details"].size()==3,"output details include all images");
    Json checkerNodes={{"checker",{{"op","checker"},{"count",{64,64}}}}};auto down=doc(checkerNodes,sheet({"checker"}));down["size"]=64;down["outputs"]["sheet.png"]["cell"]=16;
    image=render(down);for(int y=4;y<20;++y)for(int x=4;x<20;++x)near(toSrgb(image->get(x,y)[0]),.5); // Box average, not aliased point samples.
    Json alphaNodes={{"red",{{"op","constant"},{"value",{1,0,0,.5}}}}};auto transparent=doc(alphaNodes,sheet({"red"}));transparent["outputs"]["sheet.png"]["background"]="#0000ff";image=render(transparent);near(image->get(20,20)[0],.5);near(image->get(20,20)[2],.5);near(image->get(20,20)[3],1);
    auto wide=doc({{"white",{{"op","constant"},{"value",1}}}},sheet({"white"}));wide["size"]={32,16};image=render(wide);near(image->get(20,12)[0],1);near(image->get(20,27)[0],1);check(image->get(20,5)[0]<.1,"letterbox aspect ratio preserved");
    auto labeled=doc(nodes,sheet({"height"}));labeled["outputs"]["sheet.png"]["title"]="Sheet title";labeled["outputs"]["sheet.png"]["labels"]=true;labeled["outputs"]["sheet.png"]["font_scale"]=1;image=render(labeled);check(image->height>40,"title and caption space allocated");
    auto longLabel=labeled;longLabel["outputs"]["sheet.png"]["items"]=Json::array({{{"node","height"},{"label",std::string(128,'A')}}});render(longLabel); // Long text is clipped to the cell, with ellipsis.
    Json chain;Json items=Json::array();std::string previous;
    for(int i=0;i<10;++i){auto name="n"+std::to_string(i);chain[name]=i?Json{{"op","invert"},{"input",previous}}:Json{{"op","constant"},{"value",.5}};items.push_back(name);previous=name;}
    auto live=doc(chain,sheet(items));live["size"]=256;live["outputs"]["sheet.png"]["columns"]=5;live["outputs"]["sheet.png"]["cell"]=16;live["outputs"]["sheet.png"]["padding"]=0;
    Options options;options.memoryMb=1;options.threads=1;stats=Graph(live,".",options).render([](const Output&,const ImagePtr&){});check(stats["peak_buffer_mb"].get<double>()<.6,"sheet collects thumbnails without retaining all full-sized nodes");
    auto bad=doc(nodes,sheet({"missing"}));rejects(bad,"unknown node");bad=doc(nodes,sheet(Json::array()));rejects(bad,"1..256");
    bad=labeled;bad["outputs"]["sheet.png"]["columns"]=0;rejects(bad,"columns");bad=labeled;bad["outputs"]["sheet.png"]["cell"]=2048;bad["outputs"]["sheet.png"]["columns"]=32;rejects(bad,"canvas exceeds");
    bad=labeled;bad["outputs"]["sheet.png"]["title"]="Two\nlines";rejects(bad,"printable ASCII");bad=labeled;bad["outputs"]["sheet.png"]["srgb"]=false;rejects(bad,"require PNG");
    bad=labeled;bad["outputs"]["sheet.png"]["node"]="height";rejects(bad,"unknown field");bad=labeled;bad["outputs"]["sheet.png"]["items"]=Json::array({{{"node","height"},{"lable","typo"}}});rejects(bad,"unknown item field");
    bad=labeled;bad["outputs"]["../escape.png"]=bad["outputs"]["sheet.png"];rejects(bad,"cannot contain");
}
}
int main() { try{testSheet();std::cout<<"PASS native sheet layout, encoding, sampling, scheduling and validation\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL sheet: "<<e.what()<<'\n';return 1;} }
