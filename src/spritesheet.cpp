#include "texutil/texutil.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace tex {
namespace {
void require(bool ok, const std::string& message) { if(!ok) throw std::runtime_error("spritesheet: "+message); }
int integer(const Json& value, int lo, int hi, const std::string& name) {
    require(value.is_number_integer()&&value.get<double>()>=lo&&value.get<double>()<=hi,name+" must be an integer in "+std::to_string(lo)+".."+std::to_string(hi)); return value.get<int>();
}
}
Json parseSpritesheet(const Json& output, const std::filesystem::path& base, const Options& options, int width, int height, int seed) {
    const std::set<std::string> allowed={"type","source","columns","rows","count","padding","seed"};
    for(auto it=output.begin();it!=output.end();++it) require(allowed.count(it.key()),"unknown field '"+it.key()+"'");
    require(output.contains("source")&&output["source"].is_string(),"source must be a JSON filename");
    auto name=output["source"].get<std::string>(); require(!name.empty()&&name.find('\0')==std::string::npos,"invalid source filename");
    auto source=std::filesystem::weakly_canonical(base/name);
    require(std::filesystem::is_regular_file(source),"source file does not exist");
    require(std::filesystem::file_size(source)<=8*1024*1024,"source exceeds 8 MiB");
    std::ifstream stream(source); require(bool(stream),"cannot read source"); Json document=Json::parse(stream);
    int columns=integer(output.value("columns",Json(4)),1,64,"columns"),count=integer(output.value("count",Json(16)),1,256,"count");
    int rows=integer(output.value("rows",Json((count+columns-1)/columns)),1,64,"rows"),padding=integer(output.value("padding",Json(4)),0,64,"padding");
    int firstSeed=integer(output.value("seed",Json(seed)),std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),"seed");
    require(count<=columns*rows,"count exceeds grid capacity");
    require(width>2*padding&&height>2*padding,"padding leaves no content pixels");
    require(int64_t(width)*columns<=16384&&int64_t(height)*rows<=16384,"canvas exceeds 16384 pixels per side");
    require(int64_t(firstSeed)+count-1<=std::numeric_limits<int>::max(),"variant seed exceeds signed 32-bit range");
    require(document.is_object()&&document.contains("outputs")&&document["outputs"].is_object(),"source needs outputs");
    // Avoid recursive external builds. Inspection sheets/MaterialX are validated
    // with the source, then excluded from the texture atlas package.
    Json images=Json::object(),skipped=Json::array();
    for(auto it=document["outputs"].begin();it!=document["outputs"].end();++it) {
        auto spec=it.value(); require(spec.is_string()||spec.is_object(),"invalid source output");
        std::string type=spec.is_string()?"image":spec.value("type",std::string("image"));
        require(type=="image"||type=="sheet"||type=="materialx","source supports only image, sheet and materialx outputs");
        if(type=="image") {
            auto extension=std::filesystem::path(it.key()).extension().string();
            std::string format=spec.is_object()?spec.value("format",extension.empty()?document.value("format",std::string("png")):extension.substr(1)):extension.empty()?document.value("format",std::string("png")):extension.substr(1);
            require(format=="png","source image outputs must be PNG"); images[it.key()]=it.value();
        } else skipped.push_back(it.key());
    }
    require(!images.empty()&&images.size()<=16,"source requires 1..16 PNG outputs");
    Options cellOptions=options; cellOptions.width=width-2*padding; cellOptions.height=height-2*padding; cellOptions.seed=firstSeed;
    Graph validated(document,source.parent_path(),cellOptions);
    auto expanded=expandImports(document,source.parent_path(),firstSeed,document.value("tile",false));
    Json inputs=Json::array({source.string()});
    for(const auto& file:expanded.files) inputs.push_back(file.string());
    for(const auto& node:expanded.nodes) if(node["op"]=="image") inputs.push_back(node["path"]);
    document["outputs"]=images;
    return {{"source",source.string()},{"document",document},{"inputs",inputs},{"skipped_outputs",skipped},{"columns",columns},{"rows",rows},{"count",count},{"padding",padding},{"seed",firstSeed},{"cell_size",{width,height}}};
}
Json renderSpritesheet(const Json& settings, const std::filesystem::path& manifest, const Options& options, std::shared_ptr<Memory> memory) {
    int columns=settings["columns"],rows=settings["rows"],count=settings["count"],padding=settings["padding"],firstSeed=settings["seed"];
    int cellWidth=settings["cell_size"][0],cellHeight=settings["cell_size"][1],width=columns*cellWidth,height=rows*cellHeight;
    const auto& document=settings.at("document"); auto base=std::filesystem::path(settings["source"].get<std::string>()).parent_path();
    auto package=manifest.stem().string()+".assets";
    Json result={{"format","texutil-spritesheet"},{"version",1},{"type","spritesheet"},{"uv_origin","top-left"},{"columns",columns},{"rows",rows},{"count",count},{"cell_size",{cellWidth,cellHeight}},{"content_size",{cellWidth-2*padding,cellHeight-2*padding}},{"padding",padding},{"image_size",{width,height}},{"seed",firstSeed},{"images",Json::array()},{"cells",Json::array()},{"files",Json::array()},{"skipped_outputs",settings["skipped_outputs"]}};
    struct Canvas { Output output; ImagePtr image; };
    std::map<std::string,Canvas> canvases;
    Options cellOptions=options; cellOptions.width=cellWidth-2*padding; cellOptions.height=cellHeight-2*padding;
    for(int i=0;i<count;++i) {
        cellOptions.seed=firstSeed+i; int column=i%columns,row=i/columns,x0=column*cellWidth+padding,y0=row*cellHeight+padding;
        result["cells"].push_back({{"index",i},{"seed",firstSeed+i},{"column",column},{"row",row},{"pixels",{x0,y0,x0+cellOptions.width,y0+cellOptions.height}},{"uv_rect",{double(x0)/width,double(y0)/height,double(x0+cellOptions.width)/width,double(y0+cellOptions.height)/height}}});
        Graph graph(document,base,cellOptions);
        graph.render([&](const Output& output, const ImagePtr& image) {
            require(bool(image)&&!output.sheet&&!output.material,"unexpected source output");
            auto found=canvases.find(output.name);
            if(found==canvases.end()) {
                auto canvas=std::make_shared<Image>(width,height,image->kind,memory);
                // Unoccupied normal cells stay neutral; color cells stay transparent.
                if(image->kind==Kind::Normal) for(int y=0;y<height;++y) for(int x=0;x<width;++x) canvas->set(x,y,{.5f,.5f,1,1});
                found=canvases.emplace(output.name,Canvas{output,canvas}).first;
            }
            require(found->second.image->kind==image->kind,"output kind changed between variants");
            for(int y=-padding;y<image->height+padding;++y) for(int x=-padding;x<image->width+padding;++x)
                found->second.image->set(x0+x,y0+y,image->get(std::clamp(x,0,image->width-1),std::clamp(y,0,image->height-1)));
        },memory);
    }
    Pixel background=document.contains("background")?color(document["background"]):Pixel{0,0,0,1};
    // All variants must succeed before beginning disk exports. The manifest is last.
    for(const auto& [name,canvas]:canvases) {
        auto relative=std::filesystem::path(package)/name; const auto& output=canvas.output;
        writeImage(manifest.parent_path()/relative,*canvas.image,"png",output.bits,output.alpha,output.srgb,background);
        std::string kind=canvas.image->kind==Kind::Color?"color":canvas.image->kind==Kind::Normal?"data":"scalar";
        result["images"].push_back({{"source_output",name},{"file",relative.generic_string()},{"kind",kind},{"bits",output.bits},{"alpha",output.alpha},{"srgb",output.srgb&&canvas.image->kind==Kind::Color}});
        result["files"].push_back(relative.generic_string());
    }
    std::filesystem::create_directories(manifest.parent_path()); std::ofstream file(manifest);
    require(bool(file),"cannot write manifest"); file<<result.dump(2)<<'\n'; file.close(); require(bool(file),"failed writing manifest");
    return result;
}
}
