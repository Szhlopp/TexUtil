#include "texutil/texutil.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace tex;
namespace {
void check(bool ok, const std::string& message) { if(!ok)throw std::runtime_error(message); }
void save(const std::filesystem::path& path, const Json& doc) { std::filesystem::create_directories(path.parent_path());std::ofstream f(path);f<<doc.dump(2);check(bool(f),"write fixture"); }
std::map<std::string,ImagePtr> render(const Json& d, const std::filesystem::path& base, Options options = {}) {
    options.threads=1;std::map<std::string,ImagePtr> images;Graph(d,base,options).render([&](const Output& o,const ImagePtr& image){if(image)images[o.name]=image;});return images;
}
void rejects(const Json& d, const std::filesystem::path& base, const std::string& expected) {
    bool failed=false;try{Graph g(d,base);}catch(const std::exception& e){failed=std::string(e.what()).find(expected)!=std::string::npos;}
    check(failed,"expected rejection: "+expected);
}
void test(const std::filesystem::path& base) {
    Json child={{"size",32},{"seed",81},{"tile",true},{"nodes",{
        {"noise",{{"op","clouds"},{"scale",4}}},{"fixed",{{"op","clouds"},{"scale",4},{"seed",912},{"tile",false}}},
        {"recipe",{{"op","preset"},{"name","dirt"}}}}},
        {"outputs",{{"child.png","noise"},{"fixed.png","fixed"},{"recipe.png","recipe"}}}};
    save(base/"lib/child.json",child);
    Json d={{"size",32},{"seed",123},{"tile",false},{"imports",{{"a","lib/child.json"},{"b","lib/child.json"}}},
        {"nodes",{{"difference",{{"op","blend"},{"a","a.noise"},{"b","b.noise"},{"mode","difference"}}}}},
        {"outputs",{{"noise.png","a.noise"},{"fixed.png","a.fixed"},{"recipe.png","a.recipe"},{"difference.png","difference"}}}};
    auto standalone=render(child,base/"lib"), imported=render(d,base);
    check(standalone.at("child.png")->pixels==imported.at("noise.png")->pixels,"import preserves document seed/tile");
    check(standalone.at("fixed.png")->pixels==imported.at("fixed.png")->pixels,"import preserves explicit node seed/tile");
    check(standalone.at("recipe.png")->pixels==imported.at("recipe.png")->pixels,"preset macro uses imported defaults and scope");
    for(float value:imported.at("difference.png")->pixels)check(value==0,"same file can be imported twice independently");
    Options override;override.seed=789;override.width=64;override.height=64;
    auto large=render(d,base,override);auto resized=child;resized["size"]=64;
    check(large.at("noise.png")->pixels==render(resized,base/"lib").at("child.png")->pixels,"parent resolution applies; root seed override does not change imported seed");
    Json wrapper={{"imports",{{"nested","child.json"}}},{"nodes",{{"result",{{"op","invert"},{"input","nested.noise"}}}}}};
    save(base/"lib/wrapper.json",wrapper);
    Json nested={{"size",32},{"imports",{{"outer","lib/wrapper.json"}}},{"outputs",{{"nested.png","outer.nested.noise"}}}};
    check(render(nested,base).at("nested.png")->pixels==standalone.at("child.png")->pixels,"nested imports and import-only parent");
    // Referenced lists, shared scheduling, and ignored child output declarations.
    Json list={{"imports",{{"n","child.json"}}},{"nodes",{{"grid",{{"op","array"},{"input","n.noise"},{"sources",{"n.fixed"}},{"count",{2,2}},{"size",.5}}}}},{"outputs",{{"ignored.png","missing-output-node"}}}};
    save(base/"lib/list.json",list);auto arrays=nested;arrays["imports"]={{"outer","lib/list.json"}};arrays["outputs"]={{"grid.png","outer.grid"}};render(arrays,base);
    int outputs=0;auto stats=Graph(nested,base).render([&](const Output&,const ImagePtr&){++outputs;});
    check(outputs==1&&stats["timings"].size()==1,"only selected dependency runs; child outputs ignored");
    // An image inside a library resolves relative to the library, not the parent/CWD.
    Options files;files.out=base/"lib/assets";files.threads=1;
    Graph({{"size",8},{"nodes",{{"value",{{"op","constant"},{"value",.75}}}}},{"outputs",{{"source.png","value"}}}},base,files).render();
    save(base/"lib/image.json",{{"nodes",{{"asset",{{"op","image"},{"path","assets/source.png"},{"kind","scalar"}}}}}});
    auto imageDoc=nested;imageDoc["imports"]={{"pic","lib/image.json"}};imageDoc["outputs"]={{"image.png","pic.asset"}};
    auto loaded=render(imageDoc,base).at("image.png");check(loaded->get(0,0)[0]>.74,"relative image path");
    imageDoc["outputs"]={{"lib/assets/source.png","pic.asset"}};files.out=base;bool protectedImage=false;
    try{Graph g(imageDoc,base,files);}catch(const std::exception& e){protectedImage=std::string(e.what()).find("overwrite input image")!=std::string::npos;}check(protectedImage,"imported image overwrite protection");
    save(base/"lib/recipe.png",child);auto recipeDoc=d;recipeDoc["imports"]={{"a","lib/recipe.png"},{"b","lib/child.json"}};recipeDoc["outputs"]={{"lib/recipe.png","a.noise"}};bool protectedJson=false;
    try{Graph g(recipeDoc,base,files);}catch(const std::exception& e){protectedJson=std::string(e.what()).find("overwrite imported JSON")!=std::string::npos;}check(protectedJson,"imported JSON overwrite protection");
    auto bad=d;bad["nodes"]["a.noise"]={{"op","constant"}};rejects(bad,base,"collides");
    bad=d;bad["imports"]={{"bad.alias","lib/child.json"}};rejects(bad,base,"ASCII identifier");
    bad=d;bad["imports"]["a"]=42;rejects(bad,base,"JSON filename");
    bad=d;bad["imports"]["a"]=std::string("lib/child.json")+'\0'+"suffix";rejects(bad,base,"NUL");
    bad=d;bad["imports"]=Json::array();rejects(bad,base,"map aliases");
    bad=d;bad["imports"]["a"]="missing.json";rejects(bad,base,"missing.json");
    auto invalid=child;invalid["seed"]="bad";save(base/"lib/invalid.json",invalid);bad=d;bad["imports"]["a"]="lib/invalid.json";rejects(bad,base,"seed");
    invalid=child;invalid["nodes"]["unused"]={{"op","invert"},{"input","parent-value"}};save(base/"lib/invalid.json",invalid);bad["nodes"]["parent-value"]={{"op","constant"}};rejects(bad,base,"a.parent-value");
    invalid=child;invalid["nodes"]["loop"]={{"op","invert"},{"input","loop"}};save(base/"lib/invalid.json",invalid);rejects(bad,base,"cycle detected");
    save(base/"lib/cycle.json",{{"imports",{{"self","./cycle.json"}}}});bad=d;bad["imports"]["a"]="lib/cycle.json";rejects(bad,base,"file cycle");
    std::error_code ec;std::filesystem::create_symlink("cycle.json",base/"lib/link.json",ec);
    if(!ec){save(base/"lib/cycle.json",{{"imports",{{"self","link.json"}}}});rejects(bad,base,"file cycle");}
    save(base/"lib/cycle-a.json",{{"imports",{{"b","cycle-b.json"}}}});save(base/"lib/cycle-b.json",{{"imports",{{"a","cycle-a.json"}}}});bad["imports"]["a"]="lib/cycle-a.json";rejects(bad,base,"file cycle");
    for(int i=0;i<18;++i)save(base/("depth"+std::to_string(i)+".json"),i==17?child:Json{{"imports",{{"next","depth"+std::to_string(i+1)+".json"}}}});
    bad["imports"]["a"]="depth0.json";rejects(bad,base,"depth is 16");
    Json many=Json::object();for(int i=0;i<129;++i)many["a"+std::to_string(i)]="lib/child.json";bad=d;bad["imports"]=many;rejects(bad,base,"128 import instances");
    Json big;big["nodes"]=Json::object();for(int i=0;i<2100;++i)big["nodes"]["n"+std::to_string(i)]={{"op","constant"}};save(base/"large.json",big);bad=d;bad["imports"]={{"a","large.json"},{"b","large.json"}};rejects(bad,base,"4096 nodes");
}
}
int main(int argc, char** argv) { try{check(argc==2,"expected fixture directory");test(std::filesystem::absolute(argv[1]));std::cout<<"PASS graph imports, scoping, settings, paths, pruning and limits\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL imports: "<<e.what()<<'\n';return 1;} }
