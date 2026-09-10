#include "texutil/material_recipe.hpp"
#include "texutil/model.hpp"
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>

namespace tex {
namespace {
using namespace model;
void require(bool ok, const std::string& message) { if(!ok)throw std::runtime_error("export_bake: "+message); }
void fields(const Json& v, const std::set<std::string>& allowed) { require(v.is_object(),"expected object");for(auto it=v.begin();it!=v.end();++it)require(allowed.count(it.key()),"unknown field '"+it.key()+"'"); }
int integer(const Json& v, int lo, int hi, const std::string& name) { require(v.is_number_integer(),name+" must be integer");auto n=v.get<int64_t>();require(n>=lo&&n<=hi,name+" out of range");return int(n); }
void saveJson(const std::filesystem::path& p, const Json& doc) { std::filesystem::create_directories(p.parent_path());std::ofstream f(p);require(bool(f),"cannot write "+p.string());f<<doc.dump(2)<<'\n';f.close();require(bool(f),"write failed: "+p.string()); }
bool within(const std::filesystem::path& file, const std::filesystem::path& directory) { auto r=std::filesystem::weakly_canonical(file).lexically_relative(std::filesystem::weakly_canonical(directory));return !r.empty()&&*r.begin()!=".."; }
struct Frame { Vec3 t,b; };
// Lengyel tangent basis, accumulated per indexed vertex. UV handedness stays explicit.
std::vector<Frame> tangentFrames(const Mesh& mesh) {
    std::vector<Vec3> t(mesh.vertices.size()),b(mesh.vertices.size());
    for(const auto& f:mesh.triangles){const auto& a=mesh.vertices[f.vertices[0]];const auto& v=mesh.vertices[f.vertices[1]];const auto& w=mesh.vertices[f.vertices[2]];auto e=v.position-a.position,q=w.position-a.position;float u=v.uv.x-a.uv.x,x=w.uv.x-a.uv.x,z=v.uv.y-a.uv.y,y=w.uv.y-a.uv.y,d=u*y-x*z;require(std::abs(d)>1e-20f,"degenerate UV tangent frame");auto s=(e*y-q*z)*(1/d),r=(q*u-e*x)*(1/d);for(auto i:f.vertices){t[i]=t[i]+s;b[i]=b[i]+r;}}
    std::vector<Frame> frames;frames.reserve(t.size());for(size_t i=0;i<t.size();++i){auto n=normalized(mesh.vertices[i].normal),v=t[i]-n*dot(n,t[i]);if(length(v)<1e-15f)v=cross(std::abs(n.y)<.9f?Vec3{0,1,0}:Vec3{1,0,0},n);v=normalized(v);float sign=dot(cross(n,v),b[i])<0?-1.f:1.f;frames.push_back({v,cross(n,v)*sign});}return frames;
}
struct Channel { std::string name;Kind kind;ImagePtr image;Pixel value;bool normal=false; };
Pixel sample(const Channel& c, Vec2 uv, Vec3 p, Vec3 n, bool tri, float blend, const std::string& edge) {
    if(!c.image)return c.value;
    if(!tri)return c.image->sample(uv.x,1-uv.y,edge);
    float sx=n.x<0?-1:1,sy=n.y<0?-1:1,sz=n.z<0?-1:1;
    float wx=std::pow(std::abs(n.x),blend),wy=std::pow(std::abs(n.y),blend),wz=std::pow(std::abs(n.z),blend),sum=wx+wy+wz;
    auto a=c.image->sample(-p.z*sx,p.y,"repeat"),b=c.image->sample(p.x,-p.z*sy,"repeat"),d=c.image->sample(p.x*sz,p.y,"repeat");Pixel out{};for(int k=0;k<4;++k)out[k]=(a[k]*wx+b[k]*wy+d[k]*wz)/sum;return out;
}
Vec3 detailNormal(const Channel& c, Vec2 uv, Vec3 p, Vec3 n, Frame frame, bool tri, float blend, const std::string& edge, float scale, float sign) {
    if(!c.image)return {0,0,1};
    if(!tri){auto q=c.image->sample(uv.x,1-uv.y,edge);return normalized({(2*q[0]-1)*scale,(2*q[1]-1)*scale*sign,2*q[2]-1});}
    float sx=n.x<0?-1:1,sy=n.y<0?-1:1,sz=n.z<0?-1:1;
    auto slope=[&](float u,float v){auto q=c.image->sample(u,v,"repeat");float z=std::max(2*q[2]-1,.1f);return Vec2{(2*q[0]-1)/z,(2*q[1]-1)*sign/z};};
    auto a=slope(-p.z*sx,p.y),b=slope(p.x,-p.z*sy),d=slope(p.x*sz,p.y);
    float wx=std::pow(std::abs(n.x),blend),wy=std::pow(std::abs(n.y),blend),wz=std::pow(std::abs(n.z),blend),sum=wx+wy+wz;
    Vec3 s=(Vec3{0,a.y,-a.x*sx}*wx+Vec3{b.x,0,-b.y*sy}*wy+Vec3{d.x*sz,d.y,0}*wz)*(1/sum);s=s-n*dot(n,s);auto world=normalized(n+s*scale);
    auto t=normalized(frame.t-n*dot(n,frame.t));float handed=dot(cross(n,t),frame.b)<0?-1.f:1.f;auto bitangent=cross(n,t)*handed;
    return normalized({dot(world,t),dot(world,bitangent),dot(world,n)});
}
std::vector<int32_t> paddingSources(const Raster& raster, const Mesh& mesh, uint32_t slot, int distance) {
    int s=raster.size;std::vector<int32_t> src(raster.faces.size(),-1),next;
    for(size_t i=0;i<src.size();++i)if(raster.faces[i]>=0&&mesh.triangles[raster.faces[i]].material==slot)src[i]=int32_t(i);
    for(int step=0;step<distance;++step){bool changed=false;next=src;for(int y=0;y<s;++y)for(int x=0;x<s;++x){size_t i=size_t(y)*s+x;if(src[i]>=0)continue;for(auto d:{std::pair<int,int>{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}}){int xx=x+d.first,yy=y+d.second;if(xx>=0&&xx<s&&yy>=0&&yy<s&&src[size_t(yy)*s+xx]>=0){next[i]=src[size_t(yy)*s+xx];changed=true;break;}}}src.swap(next);if(!changed)break;}return src;
}
}
Json parseExportBake(const Json& input, const std::filesystem::path& base) {
    fields(input,{"type","model","materials","material","size","padding","maps","normal_convention","projection","projection_scale","projection_blend"});Json out=input;
    require(out.contains("model")&&out["model"].is_string(),"model filename required");auto path=std::filesystem::weakly_canonical(base/out["model"].get<std::string>());require(std::filesystem::is_regular_file(path),"model does not exist");out["model"]=path.string();
    out["size"]=integer(out.value("size",Json(1024)),32,16384,"size");out["padding"]=integer(out.value("padding",Json(8)),0,64,"padding");out["normal_convention"]=out.value("normal_convention",Json("directx"));require(out["normal_convention"]=="directx"||out["normal_convention"]=="opengl","normal_convention must be directx or opengl");
    out["maps"]=out.value("maps",Json::array({"color","roughness","metalness","height","normal"}));require(out["maps"].is_array()&&!out["maps"].empty(),"maps needs at least one channel");std::set<std::string> seen;for(const auto& map:out["maps"]){require(map.is_string(),"map names must be strings");std::string s=map;require(s=="color"||s=="roughness"||s=="metalness"||s=="height"||s=="normal","unknown map: "+s);require(seen.insert(s).second,"duplicate map: "+s);}
    require(out.contains("material")||out.contains("materials"),"material or materials required");
    auto parse=[&](Json v){v=parseMaterialBinding(v,base,true);require(v.contains("graph"),"export_bake bindings require an external graph (local material outputs are not supported)");for(auto key:{"projection","projection_scale","projection_blend"})if(!v.contains(key)&&out.contains(key))v[key]=out[key];return parseMaterialBinding(v,base,true);};
    // Validate projection defaults even when every binding overrides them.
    Json defaults={{"material","validation.mtlx"}};for(auto key:{"projection","projection_scale","projection_blend"})if(out.contains(key))defaults[key]=out[key];parseMaterialBinding(defaults,base);
    if(out.contains("material"))out["material"]=parse(out["material"]);
    if(out.contains("materials")){require(out["materials"].is_object()&&!out["materials"].empty()&&out["materials"].size()<=64,"materials requires 1..64 bindings");for(auto& item:out["materials"].items())item.value()=parse(item.value());}
    return out;
}
Json exportBake(const Json& settings, const std::filesystem::path& manifest, const Options& options) {
    auto mesh=model::load(settings.at("model").get<std::string>());int size=settings.at("size"),padding=settings.at("padding");auto check=model::checkUvs(mesh,std::min(size,512));require(check["usable_for_baking"].get<bool>(),"requires a unique 0..1 UV atlas; unwrap a new copy first");
    size_t pixels=size_t(size)*size,budget=options.memoryMb*1024ull*1024;
    size_t reserve=pixels*36+mesh.vertices.size()*(sizeof(Vertex)+sizeof(Frame)+2*sizeof(Vec3))+mesh.triangles.size()*sizeof(Triangle);
    require(budget>reserve+1024*1024,"memory budget too small for mesh, raster, padding and output; increase --memory");
    auto raster=model::rasterize(mesh,size);require(raster.overlaps==0,"overlapping UVs at bake resolution");auto frames=tangentFrames(mesh);auto center=(mesh.minimum+mesh.maximum)*.5f;
    auto directory=manifest.parent_path()/(manifest.stem().string()+".assets");
    struct Part { uint32_t slot;Json binding;MaterialRecipe recipe; };std::vector<Part> parts;std::set<uint32_t> used;std::set<std::string> matched;
    for(const auto& t:mesh.triangles)used.insert(t.material);
    auto names=settings.value("materials",Json::object());
    // Preflight every dependency before writing a texture. Never overwrite a source graph/image/model.
    auto protect=[&](const std::filesystem::path& path){require(std::filesystem::weakly_canonical(path)!=std::filesystem::weakly_canonical(manifest)&&!within(path,directory),"output package overlaps input asset: "+path.string());};protect(settings.at("model").get<std::string>());
    for(auto slot:used){std::string key=mesh.materials.at(slot);Json binding;if(names.contains(key)){binding=names[key];matched.insert(key);}else if(names.contains("#"+std::to_string(slot))){key="#"+std::to_string(slot);binding=names[key];matched.insert(key);}else{require(settings.contains("material"),"no binding for slot: "+key);binding=settings.at("material");}
        std::vector<std::string> extras;if(binding.contains("height"))extras.push_back(binding["height"]);auto recipe=loadMaterialRecipe(binding,extras);protect(binding.at("graph").get<std::string>());
        auto expanded=expandImports(recipe.document,recipe.base,recipe.document.value("seed",42),recipe.document.value("tile",false));for(const auto& f:expanded.files)protect(f);for(const auto& node:expanded.nodes)if(node["op"]=="image")protect(recipe.base/node["path"].get<std::string>());
        require(!recipe.material["inputs"].contains("normal")&&!recipe.material["inputs"].contains("tangent"),"use the normal helper; direct normal/tangent shader inputs are unsupported");
        Options validateOptions=options;validateOptions.width=validateOptions.height=0;validateOptions.seed.reset();validateOptions.out=directory/std::to_string(slot);Graph valid(recipe.document,recipe.base,validateOptions);
        parts.push_back({slot,binding,std::move(recipe)});
    }
    for(auto it=names.begin();it!=names.end();++it)require(matched.count(it.key()),"binding matches no used slot: "+it.key());
    Json result={{"type","export_bake"},{"version",1},{"model",settings.at("model")},{"size",size},{"padding",padding},{"normal_convention",settings.at("normal_convention")},{"uv_check",check},{"files",Json::array()},{"materials",Json::array()},{"warnings",Json::array()}};
    Json preview={{"nodes",Json::object()},{"outputs",{{"preview.png",{{"type","preview"},{"model",settings.at("model")},{"materials",Json::object()},{"environment","studio"},{"views",{{25,0,0},{25,65,0}}},{"size",512}}}}}};
    auto record=[&](const std::filesystem::path& file){result["files"].push_back(file.lexically_relative(manifest.parent_path()).generic_string());};
    auto schema=materialXInputs();Workers workers(options.threads);double estimatedPeak=double(reserve);
    for(auto& part:parts){const auto& binding=part.binding;auto material=part.recipe.material;auto folder=directory/std::to_string(part.slot);std::map<std::string,ImagePtr> images;
        Options sourceOptions=options;sourceOptions.width=sourceOptions.height=0;sourceOptions.seed.reset();sourceOptions.memoryMb=(budget-reserve)/(1024*1024);sourceOptions.out=folder;
        auto sourceStats=Graph(part.recipe.document,part.recipe.base,sourceOptions).render([&](const Output& output,const ImagePtr& image){if(image)images[output.name]=image;});
        estimatedPeak=std::max(estimatedPeak,double(reserve)+sourceStats.at("peak_buffer_mb").get<double>()*1024*1024);
        size_t sourceBytes=0;for(const auto& i:images)sourceBytes+=i.second->pixels.size()*sizeof(float);require(sourceBytes+reserve<budget,"source maps exceed bake memory budget");
        auto memory=std::make_shared<Memory>();memory->limit=budget-sourceBytes-(reserve-pixels*16);
        auto imageFor=[&](const std::string& name){require(images.count(name),"missing image output: "+name);return images.at(name);};
        std::vector<Channel> channels;
        auto add=[&](std::string name,Kind kind,Json value,bool normal=false){Channel c{name,kind,{},normal?Pixel{.5,.5,1,1}:Pixel{0,0,0,1},normal};if(value.is_object()){c.image=imageFor(value.at("texture"));require(kind!=Kind::Scalar||c.image->kind==Kind::Scalar,"scalar channel requires scalar image: "+name);}else c.value=color(value);channels.push_back(c);};
        Json baked=material;baked["texture_paths"]="relative";
        auto wants=[&](const std::string& name){const auto& m=settings.at("maps");return std::find(m.begin(),m.end(),Json(name))!=m.end();};
        const std::map<std::string,std::string> main={{"base_color","color"},{"specular_roughness","roughness"},{"metalness","metalness"}};
        for(const auto& field:main){auto v=material["inputs"].value(field.first,schema.at(field.first).at("default"));if(wants(field.second)||v.is_object()){auto kind=field.second=="color"?Kind::Color:Kind::Scalar;add(field.second,kind,v);baked["inputs"][field.first]={{"texture",field.second+".png"}};}}
        // Preserve any other texture-bound material parameter using the same projection.
        for(auto it=material["inputs"].begin();it!=material["inputs"].end();++it)if(it.value().is_object()&&!main.count(it.key())){std::string type=schema.at(it.key()).at("type");require(type=="float"||type=="color3","unsupported texture parameter: "+it.key());std::string name="extra_"+it.key();add(name,type=="float"?Kind::Scalar:Kind::Color,it.value());baked["inputs"][it.key()]={{"texture",name+".png"}};}
        bool hasHeight=binding.contains("height")||material.contains("displacement");float mid=binding.value("height_midlevel",material.contains("displacement")?material["displacement"].value("midlevel",.5f):.5f),heightScale=binding.value("height_scale",material.contains("displacement")?material["displacement"].value("scale",.01f):0.f);
        if(wants("height")||hasHeight){Json value=mid;if(hasHeight)value={{"texture",binding.contains("height")?binding["height"]:material["displacement"]["texture"]}};add("height",Kind::Scalar,value);if(material.contains("displacement")||binding.contains("height_scale"))baked["displacement"]={{"texture","height.png"},{"scale",heightScale},{"midlevel",mid}};}
        float normalScale=1,normalSign=1;
        if(wants("normal")||material.contains("normal")){Json value=Json::array({.5,.5,1});if(material.contains("normal")){value={{"texture",material["normal"]["texture"]}};normalScale=material["normal"].value("scale",1.f);normalSign=material["normal"].value("convention",std::string("directx"))=="directx"?-1:1;}add("normal",Kind::Normal,value,true);baked["normal"]={{"texture","normal.png"},{"convention",settings.at("normal_convention")},{"scale",1}};}
        auto sources=paddingSources(raster,mesh,part.slot,padding);bool tri=binding.value("projection",std::string("uv"))=="triplanar";float scale=binding.value("projection_scale",1.f),blend=binding.value("projection_blend",4.f);auto edge=part.recipe.document.value("tile",false)?"repeat":"clamp";bool dx=settings.at("normal_convention")=="directx";
        Json recipe={{"size",size},{"tile",false},{"nodes",Json::object()},{"outputs",Json::object()}};Json channelReport=Json::object();
        std::vector<Output> exports;std::map<std::string,Kind> kinds;
        for(const auto& channel:channels){Image out(size,size,channel.kind,memory);
            workers.rows(size,[&](int y){for(int x=0;x<size;++x){size_t index=size_t(y)*size+x;int face=raster.faces[index];Pixel value=channel.normal?Pixel{.5,.5,1,1}:channel.name=="height"?Pixel{mid,mid,mid,1}:Pixel{0,0,0,1};
                if(face>=0&&mesh.triangles[face].material==part.slot){const auto& f=mesh.triangles[face];const auto& a=mesh.vertices[f.vertices[0]];const auto& b=mesh.vertices[f.vertices[1]];const auto& c=mesh.vertices[f.vertices[2]];auto w=raster.barycentrics[index];float z=1-w.x-w.y;auto p=(a.position*w.x+b.position*w.y+c.position*z-center)*scale;auto n=normalized(a.normal*w.x+b.normal*w.y+c.normal*z);Vec2 uv{a.uv.x*w.x+b.uv.x*w.y+c.uv.x*z,a.uv.y*w.x+b.uv.y*w.y+c.uv.y*z};
                    if(channel.normal){auto fa=frames[f.vertices[0]],fb=frames[f.vertices[1]],fc=frames[f.vertices[2]];Frame frame{fa.t*w.x+fb.t*w.y+fc.t*z,fa.b*w.x+fb.b*w.y+fc.b*z};auto v=detailNormal(channel,uv,p,n,frame,tri,blend,edge,normalScale,normalSign);value={v.x*.5f+.5f,(dx?-v.y:v.y)*.5f+.5f,v.z*.5f+.5f,1};}
                    else value=sample(channel,uv,p,n,tri,blend,edge);
                }out.set(x,y,value);
            }});
            for(size_t i=0;i<sources.size();++i)if(sources[i]>=0&&sources[i]!=int32_t(i))out.set(int(i%size),int(i/size),out.get(sources[i]%size,sources[i]/size));
            auto file=channel.name+".png";int bits=channel.kind==Kind::Color?8:16;writeImage(folder/file,out,"png",bits,false,channel.kind==Kind::Color,{0,0,0,1});record(folder/file);
            recipe["nodes"][channel.name]={{"op","image"},{"path",file},{"kind",channel.kind==Kind::Scalar?"scalar":channel.kind==Kind::Normal?"normal":"color"},{"srgb",channel.kind==Kind::Color}};recipe["outputs"][file]={{"node",channel.name},{"bits",bits},{"srgb",channel.kind==Kind::Color}};
            Output e;e.name=file;e.format="png";e.srgb=channel.kind==Kind::Color;exports.push_back(e);kinds[file]=channel.kind;channelReport[channel.name]={{"file",file},{"bits",bits},{"color_space",e.srgb?"srgb":"linear"}};
        }
        Output mx;mx.name="material.mtlx";mx.material=baked;exports.push_back(mx);validateMaterialXReferences(exports);std::ofstream xml(folder/mx.name);require(bool(xml),"cannot write MaterialX");xml<<materialXDocument(mx,exports,kinds,false,folder);xml.close();require(bool(xml),"MaterialX write failed");record(folder/mx.name);
        recipe["outputs"]["material.mtlx"]=baked;saveJson(folder/"material.json",recipe);record(folder/"material.json");
        auto rel=(folder/"material.json").lexically_relative(directory).generic_string();Json pb={{"graph",rel},{"projection","uv"}};for(auto k:{"refraction","thickness","culling","render_order","render_channel"})if(binding.contains(k))pb[k]=binding[k];preview["outputs"]["preview.png"]["materials"]["#"+std::to_string(part.slot)]=pb;
        result["materials"].push_back({{"slot",part.slot},{"name",mesh.materials[part.slot]},{"source_graph",binding.at("graph")},{"projection",binding.value("projection",std::string("uv"))},{"recipe",(folder/"material.json").lexically_relative(manifest.parent_path()).generic_string()},{"maps",channelReport},{"height",{{"source_present",hasHeight},{"scale",heightScale},{"midlevel",mid}}}});
    }
    result["estimated_peak_working_mb"]=estimatedPeak/(1024*1024);result["projection_origin"]=Json::array({center.x,center.y,center.z});result["tangent_basis"]="Lengyel, original mesh UV orientation; DirectX/OpenGL sign recorded separately";
    saveJson(directory/"preview.json",preview);record(directory/"preview.json");result["preview"]=(directory/"preview.json").lexically_relative(manifest.parent_path()).generic_string();result["warnings"].push_back("Bilinear material sampling; 16-bit scalar/normal and 8-bit color PNGs clamp to 0..1. Height is not applied to geometry. The original model is required.");saveJson(manifest,result);return result;
}
}
