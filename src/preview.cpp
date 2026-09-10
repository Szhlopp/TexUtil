#include "texutil/model.hpp"
#include "texutil/material_recipe.hpp"
#include <algorithm>
#include <cstdlib>
#include <set>
#include <stdexcept>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace tex {
namespace {
void require(bool ok, const std::string& message) { if(!ok)throw std::runtime_error("preview: "+message); }
void fields(const Json& value, const std::set<std::string>& allowed) { require(value.is_object(),"settings must be an object");for(auto it=value.begin();it!=value.end();++it)require(allowed.count(it.key()),"unknown field '"+it.key()+"'"); }
float number(const Json& value, float low, float high, const std::string& name) { require(value.is_number(),name+" must be numeric");float x=value.get<float>();require(std::isfinite(x)&&x>=low&&x<=high,name+" out of range");return x; }
int integer(const Json& value, int low, int high, const std::string& name) { require(value.is_number_integer(),name+" must be an integer");return int(number(value,float(low),float(high),name)); }
void rotation(const Json& value) { require(value.is_array()&&value.size()==3,"rotation must be [pitch,yaw,roll] in degrees");for(const auto& v:value)number(v,-36000,36000,"rotation"); }
std::filesystem::path assetPath(const std::string& name) {
    std::vector<std::filesystem::path> roots;
    if(const char* path=std::getenv("TEXUTIL_HDRI_DIR"))roots.emplace_back(path);
#ifdef TEXUTIL_ASSET_DIR
    roots.emplace_back(TEXUTIL_ASSET_DIR);
#endif
    // Locate installed assets independently of the caller's working directory.
    std::filesystem::path executable;
#ifdef __APPLE__
    uint32_t length=0;_NSGetExecutablePath(nullptr,&length);std::vector<char> exeBuffer(length);if(_NSGetExecutablePath(exeBuffer.data(),&length)==0)executable=exeBuffer.data();
#elif defined(_WIN32)
    std::vector<wchar_t> exeBuffer(32768);DWORD length=GetModuleFileNameW(nullptr,exeBuffer.data(),DWORD(exeBuffer.size()));if(length&&length<exeBuffer.size())executable=std::wstring(exeBuffer.data(),length);
#else
    std::vector<char> exeBuffer(4096);auto length=readlink("/proc/self/exe",exeBuffer.data(),exeBuffer.size());if(length>0&&size_t(length)<exeBuffer.size())executable=std::string(exeBuffer.data(),size_t(length));
#endif
    if(!executable.empty()){auto bin=std::filesystem::weakly_canonical(executable).parent_path();roots.push_back(bin/"../share/texutil/hdri");roots.push_back(bin/"assets/hdri");}
    roots.emplace_back("assets/hdri");roots.emplace_back("../share/texutil/hdri");
    for(const auto& root:roots)if(std::filesystem::is_regular_file(root/(name+".hdr")))return std::filesystem::absolute(root/(name+".hdr"));
    throw std::runtime_error("preview: standard HDRI missing; set TEXUTIL_HDRI_DIR to the installed hdri directory");
}
}
bool filamentAvailable() {
#ifdef TEXUTIL_HAS_FILAMENT
    return true;
#else
    return false;
#endif
}
Json parsePreview(const Json& input, const std::filesystem::path& base) {
    fields(input,{"type","material","materials","model","environment","rotation","views","size","columns","exposure","intensity","environment_rotation","background","show_environment","thickness","projection","projection_scale","projection_blend","render_order"});
    require(filamentAvailable(),"Filament is not enabled; configure with TEXUTIL_FILAMENT_ROOT (docs/MODELS.md)");
    Json result=input;
    require(result.contains("model")&&result["model"].is_string(),"model is required");
    require(result.contains("material")||result.contains("materials"),"material or materials is required");
    if(result.contains("material"))result["material"]=parseMaterialBinding(result["material"],base);
    if(result.contains("materials")){require(result["materials"].is_object()&&!result["materials"].empty()&&result["materials"].size()<=64,"materials needs 1..64 named bindings");for(auto& binding:result["materials"].items())binding.value()=parseMaterialBinding(binding.value(),base);}
    result["model"]=std::filesystem::absolute(base/result["model"].get<std::string>()).lexically_normal().string();require(std::filesystem::is_regular_file(result["model"].get<std::string>()),"model file does not exist");
    auto environment=result.value("environment",Json("studio"));require(environment.is_string(),"environment must be studio, outdoor, or an HDR filename");
    std::string env=environment.get<std::string>();auto path=(env=="studio"||env=="outdoor")?assetPath(env):std::filesystem::absolute(base/env).lexically_normal();
    require(std::filesystem::is_regular_file(path),"HDR environment does not exist");require(path.extension()==".hdr","custom environment must be a Radiance .hdr image");result["environment"]=path.string();
    result["render_order"]=result.value("render_order",Json("default"));require(result["render_order"]=="default"||result["render_order"]=="center_out"||result["render_order"]=="outside_in","render_order must be default, center_out or outside_in");
    result["size"]=integer(result.value("size",Json(512)),64,2048,"size");result["columns"]=integer(result.value("columns",Json(3)),1,8,"columns");
    result["exposure"]=number(result.value("exposure",Json(0)), -16,16,"exposure");result["intensity"]=number(result.value("intensity",Json(30000)),0,1000000,"intensity");
    result["environment_rotation"]=number(result.value("environment_rotation",Json(0)),-36000,36000,"environment_rotation");result["thickness"]=number(result.value("thickness",Json(.1)),0,10,"thickness");
    result["projection"]=result.value("projection",Json("uv"));require(result["projection"]=="uv"||result["projection"]=="triplanar","projection must be uv or triplanar");
    result["projection_scale"]=number(result.value("projection_scale",Json(1)),.0001f,10000,"projection_scale");
    result["projection_blend"]=number(result.value("projection_blend",Json(4)),1,16,"projection_blend");
    result["rotation"]=result.value("rotation",Json::array({0,0,0}));rotation(result["rotation"]);
    require(!(result.contains("views")&&input.contains("rotation")),"use rotation or views, not both");
    if(result.contains("views")){require(result["views"].is_array()&&!result["views"].empty()&&result["views"].size()<=12,"views needs 1..12 rotations");for(const auto& v:result["views"])rotation(v);}else result["views"]=Json::array({result["rotation"]});
    result["background"]=result.value("background",Json("#24282d"));require(color(result["background"])[3]==1,"background must be opaque");
    result["show_environment"]=result.value("show_environment",Json(false));require(result["show_environment"].is_boolean(),"show_environment must be boolean");
    return result;
}
void validatePreviewReferences(std::vector<Output>& outputs) {
    for(auto& o:outputs)if(o.preview) {
        auto& warnings=(*o.preview)["warnings"]=Json::array();
        std::vector<Json> bindings;if(o.preview->contains("material"))bindings.push_back(o.preview->at("material"));
        if(o.preview->contains("materials"))for(const auto& binding:o.preview->at("materials"))bindings.push_back(binding);
        for(const auto& binding:bindings){if(binding.contains("graph"))continue;
        auto name=binding.at("material").get<std::string>();auto it=std::find_if(outputs.begin(),outputs.end(),[&](const Output& other){return other.name==name&&other.material.has_value();});require(it!=outputs.end(),"material must reference a MaterialX output filename: "+name);
        const std::set<std::string> textured={"base_color","specular_roughness","metalness","transmission","transmission_color","emission_color"};
        const std::set<std::string> constants={"specular_IOR","coat","coat_roughness","emission","transmission_depth","thin_walled","base"};
        for(auto input=it->material->at("inputs").begin();input!=it->material->at("inputs").end();++input) {
            if(!textured.count(input.key())&&!constants.count(input.key()))warnings.push_back("Filament preview does not implement Standard Surface input: "+input.key());
            if(constants.count(input.key()))require(!input.value().is_object(),input.key()+" only supports a constant in previews");
        }
        if(it->material->contains("displacement"))warnings.push_back("Preview uses the original mesh silhouette; MaterialX displacement is not applied");
        if(it->material->at("inputs").contains("base"))warnings.push_back("Standard Surface base weight is approximated through the Filament metalness/transmission model");
        }
    }
}
}

#ifdef TEXUTIL_HAS_FILAMENT
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_HDR
#define STBI_NO_STDIO
#include <stb_image.h>
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <filament/Camera.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/IndexBuffer.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/IndirectLight.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <filament/ColorGrading.h>
#include <filament/ToneMapper.h>
#include <filament-iblprefilter/IBLPrefilterContext.h>
#include <geometry/SurfaceOrientation.h>
#include <utils/EntityManager.h>
#include <utils/Log.h>
#include <cstdio>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
extern "C" void* MTLCreateSystemDefaultDevice(void);
#endif
#include <math/mat4.h>
#include <fstream>
#include "preview_solid.h"
#include "preview_thin.h"
#include "preview_opaque.h"
#include "preview_cube_solid.h"
#include "preview_cube_thin.h"

namespace tex {
namespace {
namespace fm=filament::math;
struct Gpu {
    filament::Engine* engine=nullptr;
    filament::Scene* scene=nullptr;filament::View* view=nullptr;filament::Renderer* renderer=nullptr;filament::SwapChain* swap=nullptr;
    filament::Material* material=nullptr;filament::MaterialInstance* instance=nullptr;filament::VertexBuffer* vb=nullptr;filament::IndexBuffer* ib=nullptr;
    filament::IndirectLight* light=nullptr;filament::Skybox* sky=nullptr;filament::ColorGrading* grading=nullptr;
    std::vector<filament::Material*> materials;std::vector<filament::MaterialInstance*> instances;
    utils::Entity cameraEntity{};std::vector<utils::Entity> meshEntities;std::vector<filament::Texture*> textures;
    ~Gpu() {
        if(!engine)return;engine->flushAndWait();
        if(view)engine->destroy(view);if(scene)engine->destroy(scene);for(auto entity:meshEntities){engine->destroy(entity);utils::EntityManager::get().destroy(entity);}
        if(cameraEntity){engine->destroyCameraComponent(cameraEntity);utils::EntityManager::get().destroy(cameraEntity);}
        for(auto* i:instances)engine->destroy(i);for(auto* m:materials)engine->destroy(m);if(vb)engine->destroy(vb);if(ib)engine->destroy(ib);
        if(light)engine->destroy(light);if(sky)engine->destroy(sky);if(grading)engine->destroy(grading);for(auto* t:textures)engine->destroy(t);
        if(renderer)engine->destroy(renderer);if(swap)engine->destroy(swap);filament::Engine::destroy(&engine);
    }
};
filament::Texture* texture(Gpu& gpu, int w, int h, const float* data) {
    using T=filament::Texture;auto* result=T::Builder().width(w).height(h).levels(0xff).sampler(T::Sampler::SAMPLER_2D).format(T::InternalFormat::RGBA16F).usage(T::Usage::DEFAULT | T::Usage::GEN_MIPMAPPABLE).build(*gpu.engine);require(result,"cannot allocate GPU texture");gpu.textures.push_back(result);
    result->setImage(*gpu.engine,0,T::PixelBufferDescriptor(data,size_t(w)*h*16,T::Format::RGBA,T::Type::FLOAT));result->generateMipmaps(*gpu.engine);gpu.engine->flushAndWait();return result;
}
float scalar(const Json& inputs, const std::string& key, float fallback) { if(!inputs.contains(key))return fallback;require(inputs[key].is_number(),key+" preview requires a scalar constant");return number(inputs[key],-1000000,1000000,key); }
fm::float3 rgb(const Json& inputs, const std::string& key, Pixel fallback) { auto p=inputs.contains(key)?color(inputs[key]):fallback;return {p[0],p[1],p[2]}; }
void bind(Gpu& gpu, const Json& inputs, const std::string& key, const std::string& samplerName, const std::filesystem::path& out, std::shared_ptr<Memory> memory, bool scalarMap, Pixel fallback, bool repeat, const Json& encodings) {
    ImagePtr image;
    if(inputs.contains(key)&&inputs[key].is_object()){auto path=out/inputs[key].at("texture").get<std::string>();image=readPng(path,scalarMap?Kind::Scalar:Kind::Color,encodings.value(inputs[key].at("texture").get<std::string>(),true),memory);}
    else {image=std::make_shared<Image>(1,1,scalarMap?Kind::Scalar:Kind::Color,memory);image->set(0,0,fallback);}
    std::vector<float> rgba;if(image->channels==1){rgba.resize(size_t(image->width)*image->height*4);for(size_t i=0;i<image->pixels.size();++i){rgba[i*4]=rgba[i*4+1]=rgba[i*4+2]=image->pixels[i];rgba[i*4+3]=1;}}
    auto* tex=texture(gpu,image->width,image->height,image->channels==1?rgba.data():image->pixels.data());
    filament::TextureSampler sampler(filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,filament::TextureSampler::MagFilter::LINEAR,repeat?filament::TextureSampler::WrapMode::REPEAT:filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);gpu.instance->setParameter(samplerName.c_str(),tex,sampler);
}
void environment(Gpu& gpu, const Json& settings) {
    auto path=settings.at("environment").get<std::string>();std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"cannot open HDRI");auto size=file.tellg();require(size>0&&size<=128*1024*1024,"HDR file exceeds 128 MiB");std::vector<unsigned char> bytes(size_t(size),0);file.seekg(0);file.read(reinterpret_cast<char*>(bytes.data()),size);require(bool(file),"cannot read HDRI");
    int w=0,h=0,n=0;require(stbi_info_from_memory(bytes.data(),int(bytes.size()),&w,&h,&n)!=0,"invalid HDR header");require(w==2*h&&w>=4&&w<=8192,"HDRI must be 2:1 equirectangular, maximum width 8192");
    std::unique_ptr<float,decltype(&stbi_image_free)> data(stbi_loadf_from_memory(bytes.data(),int(bytes.size()),&w,&h,&n,4),stbi_image_free);require(bool(data),"HDR decode failed");
    for(size_t i=0;i<size_t(w)*h*4;++i)require(std::isfinite(data.get()[i])&&data.get()[i]>=0,"HDR values must be finite and nonnegative");
    // Rotate the source panorama so sky, diffuse illumination and reflections agree.
    float shift=settings.at("environment_rotation").get<float>()*w/360.f;
    if(shift!=0){std::vector<float> rotated(size_t(w)*h*4);for(int y=0;y<h;++y)for(int x=0;x<w;++x){float u=x-shift;int ix=int(std::floor(u));float t=u-std::floor(u);int a=(ix%w+w)%w,b=(a+1)%w;for(int c=0;c<4;++c)rotated[(size_t(y)*w+x)*4+c]=data.get()[(size_t(y)*w+a)*4+c]*(1-t)+data.get()[(size_t(y)*w+b)*4+c]*t;}std::copy(rotated.begin(),rotated.end(),data.get());}
    auto* equi=texture(gpu,w,h,data.get());data.reset();
    IBLPrefilterContext context(*gpu.engine);IBLPrefilterContext::EquirectangularToCubemap convert(context);auto* cube=convert(equi);gpu.textures.push_back(cube);
    IBLPrefilterContext::SpecularFilter specular(context);auto* reflections=specular(cube);gpu.textures.push_back(reflections);
    IBLPrefilterContext::IrradianceFilter diffuse(context);auto* irradiance=diffuse(cube);gpu.textures.push_back(irradiance);
    gpu.light=filament::IndirectLight::Builder().reflections(reflections).irradiance(irradiance).intensity(settings.at("intensity").get<float>()).build(*gpu.engine);
    gpu.scene->setIndirectLight(gpu.light);
    if(settings.at("show_environment").get<bool>()){gpu.sky=filament::Skybox::Builder().environment(cube).build(*gpu.engine);gpu.scene->setSkybox(gpu.sky);}
}
}
ImagePtr renderPreview(const Json& settings, const Json& materialOutputs, const std::filesystem::path& outputDirectory, std::shared_ptr<Memory> memory, Json* report) {
    using namespace filament;
    Json feedback={{"materials",Json::array()},{"warnings",Json::array()},{"files",Json::array()}};
    auto mesh=model::load(settings.at("model").get<std::string>());auto check=model::checkUvs(mesh,64);bool uvFrame=check["usable_for_preview"].get<bool>();
    struct Prepared { Json material,settings;std::filesystem::path folder;uint32_t slot; };
    std::vector<Prepared> prepared;std::set<uint32_t> used;for(const auto& t:mesh.triangles)used.insert(t.material);
    std::set<std::string> matched;
    for(auto slot:used){Json binding;auto names=settings.value("materials",Json::object());auto key=mesh.materials.at(slot);if(names.contains(key)){binding=names[key];matched.insert(key);}else if(names.contains("#"+std::to_string(slot))){key="#"+std::to_string(slot);binding=names[key];matched.insert(key);}else {require(settings.contains("material"),"no binding for mesh material: "+key);binding=settings.at("material");}
        Json local=settings;for(const char* field:{"projection","projection_scale","projection_blend","thickness","refraction","render_order","render_channel","culling"})if(binding.contains(field))local[field]=binding[field];
        require(local.at("projection")=="triplanar"||uvFrame,"UV binding requires usable UVs; run model uv or choose triplanar");
        Json material;auto folder=outputDirectory;
        if(binding.contains("graph")){
            auto recipe=loadMaterialRecipe(binding);auto doc=recipe.document;material=recipe.material;auto name=recipe.name;auto path=std::filesystem::path(binding["graph"].get<std::string>());
            local["texture_srgb"]=Json::object();for(auto it=doc["outputs"].begin();it!=doc["outputs"].end();++it)if(it.key()!=name){auto o=it.value();local["texture_srgb"][it.key()]=o.is_object()?o.value("srgb",doc.value("srgb",true)):doc.value("srgb",true);}
            local["repeat"]=doc.value("tile",false);Options options;options.threads=settings.value("threads",0u);options.memoryMb=std::max<size_t>(1,(memory->limit-memory->current)/(1024*1024));folder=settings.at("asset_directory").get<std::string>();folder/=std::to_string(slot);options.out=folder;for(auto it=doc["outputs"].begin();it!=doc["outputs"].end();++it)require(std::filesystem::weakly_canonical(folder/it.key())!=std::filesystem::weakly_canonical(path),"material export would overwrite its recipe");
            auto rendered=Graph(doc,path.parent_path(),options).render();for(const auto& file:rendered["files"])feedback["files"].push_back(file);
            Output materialOutput;materialOutput.name=name;materialOutput.material=material;Output previewOutput;previewOutput.preview=Json{{"material",{{"material",name}}}};std::vector<Output> checkOutputs={materialOutput,previewOutput};validatePreviewReferences(checkOutputs);for(const auto& warning:checkOutputs.back().preview->at("warnings")){feedback["warnings"].push_back(key+": "+warning.get<std::string>());std::fprintf(stderr,"preview warning (%s): %s\n",key.c_str(),warning.get<std::string>().c_str());}
        }else{auto name=binding.at("material").get<std::string>();require(materialOutputs.contains(name),"unknown material output: "+name);material=materialOutputs[name];}
        feedback["materials"].push_back({{"slot",slot},{"name",mesh.materials[slot]},{"binding",binding},{"projection",local["projection"]},{"asset_directory",folder.string()},{"refraction",local.value("refraction",std::string("auto"))}});
        if(local.value("refraction",std::string("auto"))=="opaque"&&material["inputs"].contains("transmission")){auto warning=key+": preview disables transmission; exported MaterialX remains unchanged";feedback["warnings"].push_back(warning);std::fprintf(stderr,"preview warning: %s\n",warning.c_str());}
        if(local.value("refraction",std::string("auto"))=="cubemap"&&material["inputs"].contains("transmission")){auto warning=key+": cubemap transmission refracts the HDR environment only, not scene objects; exported MaterialX remains unchanged";feedback["warnings"].push_back(warning);std::fprintf(stderr,"preview warning: %s\n",warning.c_str());}
        prepared.push_back({material,local,folder,slot});
    }
    if(settings.contains("materials"))for(auto it=settings["materials"].begin();it!=settings["materials"].end();++it)require(matched.count(it.key()),"binding does not match any used material slot: "+it.key());
    auto log=[](void*,const char* message){std::fputs(message,stderr);};for(auto* stream:{&utils::slog.d,&utils::slog.e,&utils::slog.w,&utils::slog.i,&utils::slog.v})stream->setConsumer(log,nullptr);
    Gpu gpu;
#ifdef __APPLE__
    void* device=MTLCreateSystemDefaultDevice();require(device,"no Metal device available; GPU access may be restricted in this environment");CFRelease(device);
    gpu.engine=Engine::create(Engine::Backend::METAL);
#else
    gpu.engine=Engine::create(Engine::Backend::VULKAN);
#endif
    require(gpu.engine,"cannot create Filament graphics engine");
    auto& engine=*gpu.engine;int size=settings.at("size");gpu.scene=engine.createScene();gpu.view=engine.createView();gpu.renderer=engine.createRenderer();gpu.swap=engine.createSwapChain(size,size,SwapChain::CONFIG_READABLE);
    require(gpu.swap,"cannot create offscreen swapchain");gpu.view->setScene(gpu.scene);gpu.view->setViewport({0,0,uint32_t(size),uint32_t(size)});gpu.view->setAntiAliasing(View::AntiAliasing::FXAA);
    PBRNeutralToneMapper mapper;gpu.grading=ColorGrading::Builder().toneMapper(&mapper).build(engine);gpu.view->setColorGrading(gpu.grading);
    auto background=color(settings.at("background"));Renderer::ClearOptions clear;clear.clear=true;clear.clearColor={background[0],background[1],background[2],1};gpu.renderer->setClearOptions(clear);
    gpu.cameraEntity=utils::EntityManager::get().create();auto* camera=engine.createCamera(gpu.cameraEntity);camera->setProjection(40,1,.05,20);camera->lookAt({0,0,3.2},{0,0,0});camera->setExposure(16,1.f/125,100*std::pow(2.f,settings.at("exposure").get<float>()));gpu.view->setCamera(camera);
    for(const auto& item:prepared){const auto& material=item.material;const auto& settings=item.settings;const auto& outputDirectory=item.folder;bool triplanar=settings.at("projection")=="triplanar";
    auto inputs=material.at("inputs");bool thin=inputs.value("thin_walled",false);
    bool refractive=settings.value("refraction",std::string("auto"))!="opaque"&&inputs.contains("transmission")&&(inputs["transmission"].is_object()||inputs["transmission"].get<float>()>0);
    bool cubemap=settings.value("refraction",std::string("auto"))=="cubemap";
    const auto* package=!refractive?preview_opaque:cubemap?(thin?preview_cube_thin:preview_cube_solid):(thin?preview_thin:preview_solid);
    size_t packageSize=!refractive?sizeof(preview_opaque):cubemap?(thin?sizeof(preview_cube_thin):sizeof(preview_cube_solid)):(thin?sizeof(preview_thin):sizeof(preview_solid));
    gpu.material=Material::Builder().package(package,packageSize).build(engine);require(gpu.material,"cannot create preview material");gpu.materials.push_back(gpu.material);gpu.instance=gpu.material->createInstance();gpu.instances.push_back(gpu.instance);
    auto culling=settings.value("culling",std::string("none"));if(culling!="none"){gpu.instance->setDoubleSided(false);gpu.instance->setCullingMode(culling=="back"?backend::CullingMode::BACK:backend::CullingMode::FRONT);}
    bool repeat=triplanar||settings.value("repeat",true);
    gpu.instance->setParameter("triplanar",triplanar);gpu.instance->setParameter("projectionBlend",settings.at("projection_blend").get<float>());
    for(const auto& entry:std::vector<std::pair<std::string,std::string>>{{"base_color","colorMap"},{"specular_roughness","roughnessMap"},{"metalness","metalnessMap"},{"transmission","transmissionMap"},{"emission_color","emissionMap"},{"transmission_color","transmissionColorMap"}})bind(gpu,inputs,entry.first,entry.second,outputDirectory,memory,entry.first!="base_color"&&entry.first!="emission_color"&&entry.first!="transmission_color",{1,1,1,1},repeat,settings.at("texture_srgb"));
    auto paramFloat=[&](const char* source,const char* dest,float fallback){gpu.instance->setParameter(dest,inputs.contains(source)&&inputs[source].is_object()?1.f:scalar(inputs,source,fallback));};
    gpu.instance->setParameter("baseColor",inputs.contains("base_color")&&inputs["base_color"].is_object()?fm::float3{1}:rgb(inputs,"base_color",{.8,.8,.8,1}));
    gpu.instance->setParameter("emissionColor",inputs.contains("emission_color")&&inputs["emission_color"].is_object()?fm::float3{1}:rgb(inputs,"emission_color",{1,1,1,1}));
    paramFloat("specular_roughness","roughness",.2);paramFloat("metalness","metalness",0);paramFloat("transmission","transmission",0);paramFloat("emission","emission",0);paramFloat("specular_IOR","ior",1.5);paramFloat("coat","coat",0);paramFloat("coat_roughness","coatRoughness",.1);
    auto tint=inputs.contains("transmission_color")&&inputs["transmission_color"].is_object()?fm::float3{1}:rgb(inputs,"transmission_color",{1,1,1,1});float depth=std::max(.0001f,scalar(inputs,"transmission_depth",1));gpu.instance->setParameter("transmissionColor",tint);gpu.instance->setParameter("transmissionDepth",depth);gpu.instance->setParameter("thickness",settings.at("thickness").get<float>());
    ImagePtr normal;if(material.contains("normal")){normal=readPng(outputDirectory/material["normal"]["texture"].get<std::string>(),Kind::Normal,false,memory);gpu.instance->setParameter("normalScale",material["normal"].value("scale",1.f));gpu.instance->setParameter("normalSign",material["normal"].value("convention",std::string("directx"))=="directx"?-1.f:1.f);}else{normal=std::make_shared<Image>(1,1,Kind::Normal,memory);normal->set(0,0,{.5,.5,1,1});gpu.instance->setParameter("normalScale",1.f);gpu.instance->setParameter("normalSign",1.f);}
    auto* normalTexture=texture(gpu,normal->width,normal->height,normal->pixels.data());gpu.instance->setParameter("normalMap",normalTexture,TextureSampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,TextureSampler::MagFilter::LINEAR,repeat?TextureSampler::WrapMode::REPEAT:TextureSampler::WrapMode::CLAMP_TO_EDGE));normal.reset();
    }
    struct GpuVertex { fm::float3 position;fm::quatf tangent;fm::float2 uv; };
    std::vector<fm::float3> positions,normals;std::vector<fm::float2> uvs;std::vector<fm::uint3> triangles;auto center=(mesh.minimum+mesh.maximum)*.5f;float factor=2/model::length(mesh.maximum-mesh.minimum);
    for(size_t i=0;i<prepared.size();++i)gpu.instances[i]->setParameter("projectionScale",prepared[i].settings.at("projection_scale").get<float>()/factor);
    // Filament flipUV converts mesh V to top-down image V once, inside the vertex shader.
    // Tangent generation must retain the original mesh UV orientation.
    for(auto v:mesh.vertices){auto p=(v.position-center)*factor;positions.push_back({p.x,p.y,p.z});normals.push_back({v.normal.x,v.normal.y,v.normal.z});uvs.push_back(uvFrame?fm::float2{v.uv.x,v.uv.y}:fm::float2{0});}
    std::vector<size_t> offsets,counts;for(const auto& item:prepared){offsets.push_back(triangles.size()*3);for(auto t:mesh.triangles)if(t.material==item.slot)triangles.push_back({t.vertices[0],t.vertices[1],t.vertices[2]});counts.push_back(triangles.size()*3-offsets.back());}
    geometry::SurfaceOrientation::Builder orientationBuilder;orientationBuilder.vertexCount(positions.size()).normals(normals.data());
    if(uvFrame)orientationBuilder.positions(positions.data()).uvs(uvs.data()).triangleCount(triangles.size()).triangles(triangles.data());
    std::unique_ptr<geometry::SurfaceOrientation> orientation(orientationBuilder.build());require(bool(orientation),"cannot generate mesh tangent frames");
    std::vector<fm::quatf> tangents(positions.size());orientation->getQuats(tangents.data(),tangents.size());std::vector<GpuVertex> vertices;for(size_t i=0;i<positions.size();++i)vertices.push_back({positions[i],tangents[i],uvs[i]});
    gpu.vb=VertexBuffer::Builder().vertexCount(uint32_t(vertices.size())).bufferCount(1).attribute(VertexAttribute::POSITION,0,VertexBuffer::AttributeType::FLOAT3,offsetof(GpuVertex,position),sizeof(GpuVertex)).attribute(VertexAttribute::TANGENTS,0,VertexBuffer::AttributeType::FLOAT4,offsetof(GpuVertex,tangent),sizeof(GpuVertex)).attribute(VertexAttribute::UV0,0,VertexBuffer::AttributeType::FLOAT2,offsetof(GpuVertex,uv),sizeof(GpuVertex)).build(engine);
    gpu.ib=IndexBuffer::Builder().indexCount(uint32_t(triangles.size()*3)).bufferType(IndexBuffer::IndexType::UINT).build(engine);
    gpu.vb->setBufferAt(engine,0,VertexBuffer::BufferDescriptor(vertices.data(),vertices.size()*sizeof(GpuVertex)));gpu.ib->setBuffer(engine,IndexBuffer::BufferDescriptor(triangles.data(),triangles.size()*sizeof(fm::uint3)));
    // Radius about the shared bounds center is a heuristic for concentric shells, not nesting detection.
    std::vector<float> radii(prepared.size());std::vector<size_t> order;
    for(size_t i=0;i<prepared.size();++i){order.push_back(i);for(auto t:mesh.triangles)if(t.material==prepared[i].slot)for(auto index:t.vertices)radii[i]=std::max(radii[i],model::length(mesh.vertices[index].position-center));}
    auto ordering=settings.value("render_order",std::string("default"));
    if(ordering!="default")std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return ordering=="center_out"?radii[a]<radii[b]:radii[a]>radii[b];});
    for(size_t rank=0;rank<order.size();++rank){size_t i=order[rank];auto entity=utils::EntityManager::get().create();gpu.meshEntities.push_back(entity);
        int priority=ordering=="default"?4:order.size()==1?0:int(rank*7/(order.size()-1));if(prepared[i].settings.contains("render_order")&&prepared[i].settings["render_order"].is_number_integer())priority=prepared[i].settings["render_order"].get<int>();
        int channel=prepared[i].settings.value("render_channel",2);
        RenderableManager::Builder renderable(1);renderable.boundingBox({{0,0,0},{1,1,1}}).culling(false).priority(uint8_t(priority)).channel(uint8_t(channel));
        renderable.material(0,gpu.instances[i]).geometry(0,RenderableManager::PrimitiveType::TRIANGLES,gpu.vb,gpu.ib,offsets[i],counts[i]);renderable.build(engine,entity);gpu.scene->addEntity(entity);
        feedback["materials"][i]["render_order"]=priority;feedback["materials"][i]["render_channel"]=channel;feedback["materials"][i]["radius"]=radii[i];feedback["materials"][i]["culling"]=prepared[i].settings.value("culling",std::string("none"));
    }
    environment(gpu,settings);
    const auto& views=settings.at("views");Sheet sheet;ImagePtr result;
    if(views.size()>1){Json items=Json::array();for(const auto& v:views)items.push_back({{"node","view"},{"label","Rotation "+v.dump()}});sheet=parseSheet({{"items",items},{"columns",std::min(settings.at("columns").get<int>(),int(views.size()))},{"cell",size},{"title","Filament / HDR material preview"}});result=createSheet(sheet,memory);}
    Workers workers(1);
    for(size_t i=0;i<views.size();++i){const auto& v=views[i];auto& tm=engine.getTransformManager();auto rotation=fm::mat4f::rotation(v[2].get<float>()*.01745329252f,fm::float3{0,0,1})*fm::mat4f::rotation(v[1].get<float>()*.01745329252f,fm::float3{0,1,0})*fm::mat4f::rotation(v[0].get<float>()*.01745329252f,fm::float3{1,0,0});for(auto entity:gpu.meshEntities)tm.setTransform(tm.getInstance(entity),rotation);for(auto* materialInstance:gpu.instances)materialInstance->setParameter("objectRotation",fm::mat3f(rotation[0].xyz,rotation[1].xyz,rotation[2].xyz));
        std::vector<uint8_t> pixels(size_t(size)*size*4);for(int frame=0;frame<3;++frame){require(gpu.renderer->beginFrame(gpu.swap),"cannot begin preview frame");gpu.renderer->render(gpu.view);if(frame==2)gpu.renderer->readPixels(0,0,size,size,backend::PixelBufferDescriptor(pixels.data(),pixels.size(),backend::PixelDataFormat::RGBA,backend::PixelDataType::UBYTE));gpu.renderer->endFrame();engine.flushAndWait();}
        // Filament readPixels returns top-down rows, already matching our image layout.
        auto view=std::make_shared<Image>(size,size,Kind::Color,memory);for(int y=0;y<size;++y)for(int x=0;x<size;++x){size_t ix=(size_t(y)*size+x)*4;view->set(x,y,{toLinear(pixels[ix]/255.f),toLinear(pixels[ix+1]/255.f),toLinear(pixels[ix+2]/255.f),1});}
        if(views.size()>1)drawSheetItem(*result,sheet,i,*view,workers);else result=view;
    }
    if(report)*report=std::move(feedback);
    return result;
}
}
#else
namespace tex {
ImagePtr renderPreview(const Json&, const Json&, const std::filesystem::path&, std::shared_ptr<Memory>, Json*) { throw std::runtime_error("Filament support is not enabled"); }
}
#endif
