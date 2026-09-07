#include "texutil/texutil.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
using namespace tex;
namespace {
void check(bool v, const std::string& message) { if (!v) throw std::runtime_error(message); }
void near(double a, double b, double epsilon = 1e-5) { check(std::abs(a - b) <= epsilon, "expected " + std::to_string(b) + ", got " + std::to_string(a)); }
struct Fixture {
    Workers workers; std::shared_ptr<Memory> memory = std::make_shared<Memory>(); Context c;
    explicit Fixture(int w = 32, int h = 32, int threads = 1) : workers(threads), c{w, h, 42, true, ".", workers, memory} {}
    ImagePtr image(const std::function<float(int, int)>& fn) { auto im = std::make_shared<Image>(c.width, c.height, Kind::Scalar, memory); for (int y = 0; y < c.height; ++y) for (int x = 0; x < c.width; ++x) im->set(x, y, {fn(x,y),0,0,1}); return im; }
    ImagePtr run(Json n, std::map<std::string, ImagePtr> inputs = {}) { return execute(normalizedNode("test", n), inputs, c); }
};
double mass(const ImagePtr& image) { return std::accumulate(image->pixels.begin(), image->pixels.end(), 0.0); }
void testDistance() {
    Fixture f(7, 5); auto src = f.image([](int x, int y) { return (x == 0 && y == 1) || (x == 4 && y == 3) ? 1.0f : 0.0f; });
    for (std::string edge : {"repeat", "clamp", "transparent"}) for (std::string side : {"inside", "outside", "signed"}) {
        auto actual = f.run({{"op","distance"},{"input","s"},{"edge",edge},{"side",side},{"radius",10}}, {{"s",src}});
        for (int y = 0; y < 5; ++y) for (int x = 0; x < 7; ++x) {
            bool foreground = src->get(x,y)[0] >= .5f; bool target = side == "outside" || (side == "signed" && !foreground); double closest = INFINITY;
            for (int sy = 0; sy < 5; ++sy) for (int sx = 0; sx < 7; ++sx) if ((src->get(sx,sy)[0] >= .5f) == target) {
                int dx = std::abs(x-sx), dy = std::abs(y-sy); if (edge == "repeat") { dx = std::min(dx,7-dx); dy = std::min(dy,5-dy); } closest = std::min(closest,std::hypot(dx,dy));
            }
            if (edge == "transparent" && !target) closest = std::min(closest,double(std::min({x+1,7-x,y+1,5-y})));
            double expected = std::min(1.0, closest/10); if (side == "signed") expected = .5 + (foreground ? 1 : -1)*expected*.5; near(actual->get(x,y)[0],expected);
        }
    }
    for (float value : {0.0f,1.0f}) {
        auto constant = f.image([&](int,int) { return value; }); auto out = f.run({{"op","distance"},{"input","s"},{"side","signed"}},{{"s",constant}});
        for (float v : out->pixels) near(v,value);
    }
    auto box = f.image([](int x,int y) { return x>=1 && x<=5 && y>=1 && y<=3 ? 1.0f : 0.0f; });
    auto bevel = f.run({{"op","bevel"},{"input","s"},{"radius",2},{"profile","linear"}},{{"s",box}}); near(bevel->get(3,2)[0],1); near(bevel->get(1,1)[0],.5); near(bevel->get(0,0)[0],0);
}
void testFilters() {
    Fixture f(33,33); auto impulse = f.image([](int x,int y) { return x==16 && y==16 ? 1.0f : 0.0f; });
    auto gaussian = f.run({{"op","gaussian_blur"},{"input","s"},{"sigma",2}},{{"s",impulse}}); near(mass(gaussian),1,2e-6);
    double kernelSum = 0; for (int i=-6;i<=6;++i) kernelSum += std::exp(-i*i/8.0); near(gaussian->get(16,16)[0],1/(kernelSum*kernelSum));
    near(gaussian->get(14,16)[0],gaussian->get(16,18)[0]);
    auto directional = f.run({{"op","directional_blur"},{"input","s"},{"length",8},{"samples",9}},{{"s",impulse}});
    near(mass(directional),1); near(directional->get(12,16)[0],1.0/9); near(directional->get(16,15)[0],0);
    auto flat = f.image([](int,int) { return .37f; });
    for (std::string mode : {"min","max","average"}) { auto out = f.run({{"op","slope_blur"},{"input","s"},{"slope","flat"},{"mode",mode}},{{"s",impulse},{"flat",flat}}); for (size_t i=0;i<out->pixels.size();++i) near(out->pixels[i],impulse->pixels[i]); }
    auto slope = f.image([](int x,int) { return x/33.0f; });
    auto moved = f.run({{"op","slope_blur"},{"input","s"},{"slope","slope"},{"strength",4},{"samples",4},{"mode","max"}},{{"s",impulse},{"slope",slope}}); near(moved->get(20,16)[0],1,1e-4); near(moved->get(12,16)[0],0);
    auto red = f.run({{"op","shape"},{"color","#ff000080"},{"size",.3}});
    for (auto spec : std::vector<Json>{{{"op","gaussian_blur"},{"sigma",3}},{{"op","directional_blur"},{"length",8}}}) { spec["input"]="s"; spec["edge"]="transparent"; auto out = f.run(spec,{{"s",red}}); for (int y=0;y<33;++y) for (int x=0;x<33;++x) { auto p=out->get(x,y); if(p[3]>1e-5) { near(p[0],1); near(p[1],0); } } }
    auto ramp = f.image([](int x,int) { return float(x)/32; });
    const std::map<std::string,float> expected{{"add",.75f},{"subtract",.25f},{"multiply",.125f},{"divide",2},{"min",.25f},{"max",.5f},{"pow",std::pow(.5f,.25f)},{"abs",.5f},{"clamp",.5f},{"fract",.5f},{"quantize",4.0f/7}};
    for (auto [mode,value] : expected) near(f.run({{"op","math"},{"input","s"},{"mode",mode},{"value",.25}},{{"s",ramp}})->get(16,0)[0],value);
    near(f.run({{"op","math"},{"input","s"},{"mode","divide"},{"value",0}},{{"s",ramp}})->get(16,0)[0],0);
    auto constant = f.run({{"op","auto_levels"},{"input","s"}},{{"s",flat}}); near(mass(constant),0);
    auto ranged = f.run({{"op","auto_levels"},{"input","s"},{"range",{.2,.8}}},{{"s",ramp}}); near(ranged->get(0,0)[0],.2);near(ranged->get(32,0)[0],.8);
    auto mask = f.run({{"op","range_mask"},{"input","s"},{"range",{.25,.75}},{"softness",.25}},{{"s",ramp}});near(mask->get(0,0)[0],0);near(mask->get(4,0)[0],.5);near(mask->get(8,0)[0],1);near(mask->get(28,0)[0],.5);
}
void testErosion() {
    for (int size : {1,2,32}) for (std::string edge : {"repeat","clamp"}) {
        Fixture f(size,size), multi(size,size,4);
        auto terrain = f.image([](int x,int y) { return .3f + .2f*std::sin(x*.7f)*std::cos(y*.4f); }); auto black = f.image([](int,int) { return 0.0f; }); auto flat = f.image([](int,int) { return .5f; });
        for (std::string mode : {"time","wind","rain"}) {
            Json n={{"op","erode"},{"input","s"},{"mode",mode},{"edge",edge},{"iterations",24},{"rate",.7},{"capacity",40},{"rainfall",.05}};
            auto result = f.run(n,{{"s",terrain}}), deterministic = multi.run(n,{{"s",terrain}});
            check(result->pixels == deterministic->pixels,mode+" thread determinism"); near(mass(result),mass(terrain),mass(terrain)*2e-6+1e-6);
            for (float v : result->pixels) check(std::isfinite(v)&&v>=0,mode+" nonnegative finite");
            auto stable = f.run(n,{{"s",flat}}); for (float v : stable->pixels) near(v,.5,1e-6);
            if (size==32) check(result->pixels != terrain->pixels,mode+" changes uneven height");
            n["mask"]="mask"; auto protectedResult = f.run(n,{{"s",terrain},{"mask",black}}); check(protectedResult->pixels==terrain->pixels,mode+" zero mask identity"); n.erase("mask");
            n["iterations"]=0; check(f.run(n,{{"s",terrain}})->pixels==terrain->pixels,mode+" zero steps identity");
        }
    }
    Fixture f(32,32); auto hill = f.image([](int x,int y) { return x>=12 && x<20 && y>=12 && y<20 ? .8f : 0.0f; });
    auto centroid = [](const ImagePtr& im) { double m=0; for(int y=0;y<im->height;++y)for(int x=0;x<im->width;++x)m+=x*im->get(x,y)[0];return m/mass(im); };
    for (float angle : {0.0f,180.0f}) { auto out=f.run({{"op","erode"},{"input","s"},{"mode","wind"},{"angle",angle},{"iterations",8},{"edge","clamp"}},{{"s",hill}}); check(angle==0 ? centroid(out)>centroid(hill) : centroid(out)<centroid(hill),"wind transports downwind"); }
    auto rain=f.run({{"op","erode"},{"input","s"},{"mode","rain"},{"seed",42}},{{"s",hill}}), rain2=f.run({{"op","erode"},{"input","s"},{"mode","rain"},{"seed",43}},{{"s",hill}});check(rain->pixels!=rain2->pixels,"rain seed changes flow");
    auto time=f.run({{"op","erode"},{"input","s"},{"mode","time"}},{{"s",hill}});
    auto energy=[](const ImagePtr& im) { double sum=0; for(float v:im->pixels)sum+=v*v;return sum; };check(energy(time)<energy(hill),"time relaxation reduces roughness energy");
    bool failed=false; auto negative=f.image([](int,int) {return -.1f;});try { f.run({{"op","erode"},{"input","s"}},{{"s",negative}}); } catch(const std::exception&) {failed=true;}check(failed,"negative height rejected");
}
void testSourcesAndPresets() {
    Fixture f(64,64), multi(64,64,4); auto shape=f.run({{"op","shape"},{"type","star"}}), black=f.image([](int,int){return 0.0f;});
    for(std::string op:{"scatter","array"}) {
        Json n={{"op",op},{"input","s"},{"size",.06},{"size_jitter",.6},{"rotation_jitter",180},{"sources",{"s"}},{"value_range",{.3,1}},{"wrap",true}};
        if(op=="array")n["count"]={8,8};else n["count"]=100;
        check(f.run(n,{{"s",shape}})->pixels==multi.run(n,{{"s",shape}})->pixels,"sampler deterministic");
        n["mask"]="black";near(mass(f.run(n,{{"s",shape},{"black",black}})),0);n.erase("mask");n["scale_map"]="black";near(mass(f.run(n,{{"s",shape},{"black",black}})),0);
    }
    for(std::string op:{"gaussian_noise","blue_noise"}) { Json n={{"op",op},{"seed",42}};if(op=="blue_noise") {n["count"]=64;n["radius"]=.8;}auto a=f.run(n);check(a->pixels==multi.run(n)->pixels,"noise deterministic");n["seed"]=43;check(a->pixels!=f.run(n)->pixels,"noise seed varies"); }
    auto gaussian=f.run({{"op","gaussian_noise"},{"deviation",.1}});double mean=mass(gaussian)/gaussian->pixels.size(),var=0;for(float v:gaussian->pixels)var+=(v-mean)*(v-mean);near(mean,.5,.006);near(var/gaussian->pixels.size(),.01,.001);
    for(std::string type:{"polygon","star","capsule","gaussian"}) {auto im=f.run({{"op","shape"},{"type",type}});check(mass(im)>5,"shape has coverage");near(im->get(0,0)[0],0);check(im->get(32,32)[0]>.95,"shape center");}
    for(std::string op:{"clouds","perlin","simplex","value","voronoi","white"})for(bool tile:{true,false}) {Json n={{"op",op},{"stretch",{.5,3}},{"angle",30},{"tile",tile}};auto a=f.run(n);check(a->pixels==multi.run(n)->pixels,"anisotropic noise deterministic");for(float v:a->pixels)check(std::isfinite(v),"anisotropic noise finite");}
    for (const auto& name : presetNames()) {
        Json doc={{"size",64},{"tile",true},{"nodes",{{"result",{{"op","preset"},{"name",name},{"config",{{"seed",7}}}}}}},{"outputs",{{"result.png","result"}}}};
        ImagePtr a,b;Options one;one.threads=1;Options four;four.threads=4;
        Graph(doc,".",one).render([&](const Output&,const ImagePtr& im){a=im;});Graph(doc,".",four).render([&](const Output&,const ImagePtr& im){b=im;});
        check(a->pixels==b->pixels,"preset deterministic "+name.get<std::string>());auto bounds=std::minmax_element(a->pixels.begin(),a->pixels.end());check(*bounds.second-*bounds.first>.01,"preset nonconstant "+name.get<std::string>());
    }
    for (const auto& name : presetNames()) for (double scale : {0.25,4.0}) {
        auto expanded = expandPresets({{"p",{{"op","preset"},{"name",name},{"config",{{"scale",scale},{"detail",1},{"angle",35},{"seed",-2147483647-1}}}}}});
        for (auto it=expanded.begin();it!=expanded.end();++it) normalizedNode(it.key(),it.value());
    }
    auto grid = f.run({{"op","array"},{"input","s"},{"count",{2,2}},{"size",.15},{"row_offset",.5},{"wrap",true}},{{"s",shape}});
    check(grid->get(16,16)[0]>.9 && grid->get(32,48)[0]>.9,"odd row offset"); near(grid->get(16,48)[0],0);
    auto white=f.image([](int,int){return 1.0f;}), quarter=f.image([](int,int){return .25f;});
    auto oriented=f.run({{"op","array"},{"input","s"},{"count",{1,1}},{"size",{.6,.1}},{"direction","d"}},{{"s",white},{"d",quarter}});
    near(oriented->get(32,20)[0],1);near(oriented->get(20,32)[0],0);
    auto noValue=f.run({{"op","array"},{"input","s"},{"value_map","v"}},{{"s",white},{"v",black}});near(mass(noValue),0);
    auto red=f.run({{"op","constant"},{"value","#ff0000"}}), green=f.run({{"op","constant"},{"value","#00ff00"}});
    auto varied=f.run({{"op","array"},{"input","r"},{"sources",{"g"}},{"count",{8,8}},{"size",.1}},{{"r",red},{"g",green}});double redSum=0,greenSum=0;for(int y=0;y<64;++y)for(int x=0;x<64;++x){auto v=varied->get(x,y);redSum+=v[0];greenSum+=v[1];}check(redSum>0&&greenSum>0,"multiple source selection");
    bool missing=false;try{Graph({{"nodes",{{"s",{{"op","constant"}}},{"a",{{"op","array"},{"input","s"},{"sources",{"missing"}}}}}},{"outputs",{{"x.png","a"}}}},".");}catch(const std::exception&){missing=true;}check(missing,"additional source reference validated");
    bool frequency=false;try{normalizedNode("n",{{"op","clouds"},{"scale",4096},{"octaves",12},{"stretch",.0001}});}catch(const std::exception&){frequency=true;}check(frequency,"anisotropic octave frequency bounded");
    Fixture memoryFixture(128,128);auto memorySource=memoryFixture.image([](int x,int){return float(x)/128;});memoryFixture.memory->limit=128*128*4*4;bool exhausted=false;try{memoryFixture.run({{"op","erode"},{"input","s"},{"mode","rain"}},{{"s",memorySource}});}catch(const std::exception& e){exhausted=std::string(e.what()).find("memory budget")!=std::string::npos;}check(exhausted,"rain working buffers enforce budget");
    auto rejects=[](Json nodes) {bool failed=false;try{expandPresets(nodes);}catch(const std::exception&){failed=true;}check(failed,"invalid preset rejected");};
    rejects({{"p",{{"op","preset"},{"name","missing"}}}});rejects({{"p",{{"op","preset"},{"config",{{"unknown",1}}}}}});rejects({{"p",{{"op","preset"},{"config",{{"scale",0}}}}}});
    auto expanded=expandPresets({{"p",{{"op","preset"},{"name","brick"}}},{"__preset_0",{{"op","constant"},{"value",.75}}}});check(expanded["__preset_0"]["value"]==.75,"preset name collision avoided");
    bool bad=false;try{normalizedNode("bad",{{"op","scatter"},{"input","x"},{"size",.8},{"size_jitter",.5},{"wrap",true}});}catch(const std::exception&){bad=true;}check(bad,"wrap validates maximum random size");
}
}
int main() {
    int failed=0;for(const auto& [name,test]:std::vector<std::pair<std::string,std::function<void()>>>{{"exact distance and bevel",testDistance},{"filters and math",testFilters},{"erosion conservation and determinism",testErosion},{"sources samplers and presets",testSourcesAndPresets}}){try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}}return failed?1:0;
}
