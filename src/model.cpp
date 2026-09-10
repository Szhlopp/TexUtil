#include "texutil/model.hpp"
#include <ufbx.h>
#include <xatlas.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace tex::model {
namespace {
void require(bool value, const std::string& message) { if(!value)throw std::runtime_error("model: "+message); }
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
Vec3 convert(ufbx_vec3 v) { return {float(v.x),float(v.y),float(v.z)}; }
void triangle(Mesh& out, Vertex a, Vertex b, Vertex c, uint32_t material) {
    require(out.triangles.size()<2000000,"limit is 2 million triangles");
    require(finite(a.position)&&finite(b.position)&&finite(c.position),"nonfinite vertex position");
    Vec3 n=normalized(cross(b.position-a.position,c.position-a.position));
    for(Vertex* v:{&a,&b,&c})if(!finite(v->normal)||length(v->normal)<1e-12f)v->normal=n;else v->normal=normalized(v->normal);
    uint32_t start=uint32_t(out.vertices.size());out.vertices.insert(out.vertices.end(),{a,b,c});out.triangles.push_back({{start,start+1,start+2},material});
}
Mesh loadFbx(const std::filesystem::path& path) {
    ufbx_load_opts opts{};opts.generate_missing_normals=true;opts.ignore_animation=true;
    opts.target_axes=ufbx_axes_right_handed_y_up;opts.target_unit_meters=1;
    ufbx_error error{};ufbx_scene* raw=ufbx_load_file(path.string().c_str(),&opts,&error);
    if(!raw){char message[2048];ufbx_format_error(message,sizeof(message),&error);throw std::runtime_error(message);}
    std::unique_ptr<ufbx_scene,decltype(&ufbx_free_scene)> scene(raw,ufbx_free_scene);
    Mesh out;out.materials.push_back("default");std::unordered_map<const ufbx_material*,uint32_t> materials;
    for(auto* m:scene->materials){materials[m]=uint32_t(out.materials.size());out.materials.emplace_back(m->name.data,m->name.length);}
    for(auto* node:scene->nodes)if(node->mesh) {
        auto* mesh=node->mesh;require(mesh->skin_deformers.count==0&&mesh->blend_deformers.count==0,"deformed FBX is unsupported; export a static posed mesh");require(mesh->num_triangles<=2000000,"mesh exceeds triangle limit");
        auto normalMatrix=ufbx_matrix_for_normals(&node->geometry_to_world);
        bool mirrored=ufbx_matrix_determinant(&node->geometry_to_world)<0;
        std::vector<uint32_t> indices(mesh->max_face_triangles*3);
        for(size_t f=0;f<mesh->faces.count;++f) {
            uint32_t material=0;
            if(f<mesh->face_material.count) {auto m=mesh->face_material.data[f];if(m<node->materials.count)material=materials[node->materials.data[m]];}
            size_t count=ufbx_triangulate_face(indices.data(),indices.size(),mesh,mesh->faces.data[f]);
            for(size_t t=0;t<count;++t) {
                Vertex v[3];
                for(int j=0;j<3;++j) {
                    auto ix=indices[t*3+j];v[j].position=convert(ufbx_transform_position(&node->geometry_to_world,ufbx_get_vertex_vec3(&mesh->vertex_position,ix)));
                    v[j].normal=convert(ufbx_transform_direction(&normalMatrix,ufbx_get_vertex_vec3(&mesh->vertex_normal,ix)));
                    if(mesh->vertex_uv.exists) {auto uv=ufbx_get_vertex_vec2(&mesh->vertex_uv,ix);v[j].uv={float(uv.x),float(uv.y)};v[j].hasUv=true;}
                }
                if(mirrored)std::swap(v[1],v[2]);triangle(out,v[0],v[1],v[2],material);
            }
        }
    }
    return out;
}
Mesh loadGltf(const std::filesystem::path& path) {
    cgltf_options opts{};cgltf_data* raw=nullptr;require(cgltf_parse_file(&opts,path.string().c_str(),&raw)==cgltf_result_success,"cannot parse glTF");
    std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
    require(cgltf_load_buffers(&opts,data.get(),path.string().c_str())==cgltf_result_success,"cannot load glTF buffers");
    require(cgltf_validate(data.get())==cgltf_result_success,"invalid glTF data");
    for(size_t i=0;i<data->buffer_views_count;++i)require(!data->buffer_views[i].has_meshopt_compression,"meshopt glTF is unsupported; export uncompressed geometry");
    Mesh out;out.materials.push_back("default");
    for(size_t i=0;i<data->materials_count;++i)out.materials.push_back(data->materials[i].name?data->materials[i].name:("material_"+std::to_string(i)));
    std::set<const cgltf_node*> active;
    std::function<void(cgltf_node*)> include=[&](cgltf_node* n){if(!active.insert(n).second)return;for(size_t i=0;i<n->children_count;++i)include(n->children[i]);};
    if(data->scene)for(size_t i=0;i<data->scene->nodes_count;++i)include(data->scene->nodes[i]);else for(size_t i=0;i<data->nodes_count;++i)include(&data->nodes[i]);
    for(const auto* node:active)if(node->mesh) {
        require(!node->skin,"skinned glTF is not supported; export a static posed mesh");
        float m[16];cgltf_node_transform_world(node,m);
        Vec3 x{m[0],m[1],m[2]},y{m[4],m[5],m[6]},z{m[8],m[9],m[10]};float det=dot(x,cross(y,z));
        require(std::isfinite(det)&&std::abs(det)>1e-20f,"singular glTF transform");
        for(size_t p=0;p<node->mesh->primitives_count;++p) {
            const auto& primitive=node->mesh->primitives[p];require(primitive.type==cgltf_primitive_type_triangles,"only glTF triangle primitives are supported");
            require(!primitive.has_draco_mesh_compression,"Draco glTF is unsupported; export uncompressed geometry");
            require(primitive.targets_count==0,"glTF morph targets are unsupported; export a static mesh");
            const cgltf_accessor *pos=nullptr,*normal=nullptr,*uv=nullptr;
            for(size_t a=0;a<primitive.attributes_count;++a) {
                auto& attr=primitive.attributes[a];
                if(attr.type==cgltf_attribute_type_position)pos=attr.data;
                if(attr.type==cgltf_attribute_type_normal)normal=attr.data;
                if(attr.type==cgltf_attribute_type_texcoord&&attr.index==0)uv=attr.data;
            }
            require(pos,"glTF primitive has no positions");
            size_t count=primitive.indices?primitive.indices->count:pos->count;require(count%3==0&&count/3<=2000000,"invalid glTF triangle count");
            uint32_t material=primitive.material?uint32_t(primitive.material-data->materials)+1:0;
            for(size_t t=0;t<count;t+=3) {
                Vertex v[3];
                for(int j=0;j<3;++j) {
                    size_t ix=primitive.indices?cgltf_accessor_read_index(primitive.indices,t+j):t+j;float b[4]{};
                    require(ix<pos->count&&cgltf_accessor_read_float(pos,ix,b,3),"cannot read glTF positions");v[j].position=x*b[0]+y*b[1]+z*b[2]+Vec3{m[12],m[13],m[14]};
                    if(normal) {require(cgltf_accessor_read_float(normal,ix,b,3),"cannot read glTF normals");v[j].normal=(cross(y,z)*b[0]+cross(z,x)*b[1]+cross(x,y)*b[2])*(1/det);}
                    if(uv){require(cgltf_accessor_read_float(uv,ix,b,2),"cannot read glTF UVs");v[j].uv={b[0],1-b[1]};v[j].hasUv=true;}
                }
                if(det<0)std::swap(v[1],v[2]);triangle(out,v[0],v[1],v[2],material);
            }
        }
    }
    return out;
}
float area(Vec2 a, Vec2 b, Vec2 c) { return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x); }
}
void bounds(Mesh& mesh) {
    require(!mesh.triangles.empty(),"no triangles found");
    mesh.minimum=mesh.maximum=mesh.vertices.front().position;
    for(const auto& v:mesh.vertices){mesh.minimum={std::min(mesh.minimum.x,v.position.x),std::min(mesh.minimum.y,v.position.y),std::min(mesh.minimum.z,v.position.z)};mesh.maximum={std::max(mesh.maximum.x,v.position.x),std::max(mesh.maximum.y,v.position.y),std::max(mesh.maximum.z,v.position.z)};}
    require(length(mesh.maximum-mesh.minimum)>1e-15f,"zero-sized mesh");
}
Mesh load(const std::filesystem::path& path) {
    require(std::filesystem::is_regular_file(path),"input file does not exist: "+path.string());
    require(std::filesystem::file_size(path)<=512ull*1024*1024,"input exceeds 512 MiB");
    auto ext=path.extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return char(std::tolower(c));});
    require(ext==".obj"||ext==".fbx"||ext==".gltf"||ext==".glb","supported formats are OBJ, FBX, glTF and GLB");
    Mesh mesh=(ext==".gltf"||ext==".glb")?loadGltf(path):loadFbx(path);bounds(mesh);return mesh;
}
Raster rasterize(const Mesh& mesh, int size) {
    require(size>=1&&size<=4096,"raster size must be 1..4096");
    Raster result{size,std::vector<int32_t>(size_t(size)*size,-1),std::vector<Vec2>(size_t(size)*size)};
    for(size_t f=0;f<mesh.triangles.size();++f) {
        auto t=mesh.triangles[f];Vec2 uv[3];bool valid=true;
        for(int j=0;j<3;++j){auto& v=mesh.vertices[t.vertices[j]];uv[j]={v.uv.x,1-v.uv.y};valid&=v.hasUv&&std::isfinite(uv[j].x)&&std::isfinite(uv[j].y);}
        if(!valid)continue;
        float d=area(uv[0],uv[1],uv[2]);if(std::abs(d)<1e-14f)continue;
        float minx=std::min({uv[0].x,uv[1].x,uv[2].x}),maxx=std::max({uv[0].x,uv[1].x,uv[2].x});
        float miny=std::min({uv[0].y,uv[1].y,uv[2].y}),maxy=std::max({uv[0].y,uv[1].y,uv[2].y});
        int x0=int(std::floor(std::clamp(minx,0.f,1.f)*size)),x1=std::min(size-1,int(std::ceil(std::clamp(maxx,0.f,1.f)*size)));
        int y0=int(std::floor(std::clamp(miny,0.f,1.f)*size)),y1=std::min(size-1,int(std::ceil(std::clamp(maxy,0.f,1.f)*size)));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            Vec2 p{(x+.5f)/size,(y+.5f)/size};float a=area(uv[1],uv[2],p)/d,b=area(uv[2],uv[0],p)/d,c=1-a-b;
            if(a<-1e-6f||b<-1e-6f||c<-1e-6f)continue;
            size_t i=size_t(y)*size+x;
            if(result.faces[i]>=0){auto old=result.barycentrics[i];if(std::min({a,b,c,old.x,old.y,1-old.x-old.y})>1e-5f)++result.overlaps;continue;}
            result.faces[i]=int32_t(f);result.barycentrics[i]={a,b};++result.covered;
        }
    }
    return result;
}
Json checkUvs(const Mesh& mesh, int size) {
    size_t missing=0,nonfinite=0,outside=0,degenerate=0,mirrored=0,geometryDegenerate=0;
    for(const auto& t:mesh.triangles) {
        auto a=mesh.vertices[t.vertices[0]],b=mesh.vertices[t.vertices[1]],c=mesh.vertices[t.vertices[2]];
        if(length(cross(b.position-a.position,c.position-a.position))<1e-16f)++geometryDegenerate;
        if(!a.hasUv||!b.hasUv||!c.hasUv){++missing;continue;}
        bool ok=true,out=false;for(auto v:{a,b,c}) {ok&=std::isfinite(v.uv.x)&&std::isfinite(v.uv.y);out|=v.uv.x<0||v.uv.y<0||v.uv.x>1||v.uv.y>1;}
        if(!ok){++nonfinite;continue;}if(out)++outside;
        float d=area(a.uv,b.uv,c.uv);if(std::abs(d)<1e-14f)++degenerate;else if(d<0)++mirrored;
    }
    auto raster=rasterize(mesh,size);
    return {{"triangles",mesh.triangles.size()},{"vertices",mesh.vertices.size()},{"materials",mesh.materials},{"bounds",{{"min",{mesh.minimum.x,mesh.minimum.y,mesh.minimum.z}},{"max",{mesh.maximum.x,mesh.maximum.y,mesh.maximum.z}}}},
        {"missing_uv_triangles",missing},{"nonfinite_uv_triangles",nonfinite},{"outside_0_1_triangles",outside},{"degenerate_uv_triangles",degenerate},{"mirrored_uv_triangles",mirrored},{"degenerate_geometry_triangles",geometryDegenerate},
        {"overlap_samples",raster.overlaps},{"coverage",double(raster.covered)/(size_t(size)*size)},{"check_resolution",size},{"overlap_check","sampled pixel centers; subpixel overlaps may be missed"},
        {"usable_for_preview",missing==0&&nonfinite==0&&degenerate==0},{"usable_for_baking",missing==0&&nonfinite==0&&outside==0&&degenerate==0&&geometryDegenerate==0&&raster.overlaps==0&&raster.covered>0}};
}
void unwrap(Mesh& mesh, int size, int padding) {
    require(size>=32&&size<=4096&&padding>=0&&padding<=64,"invalid atlas size or padding");
    std::unique_ptr<xatlas::Atlas,decltype(&xatlas::Destroy)> atlas(xatlas::Create(),xatlas::Destroy);
    std::vector<uint32_t> indices,materials;indices.reserve(mesh.triangles.size()*3);
    for(const auto& t:mesh.triangles){indices.insert(indices.end(),t.vertices,t.vertices+3);materials.push_back(t.material);}
    // xatlas has absolute geometric tolerances. Normalize only its working coordinates,
    // so centimeter- and meter-scale assets unwrap identically without losing small faces.
    std::vector<Vec3> atlasPositions;float atlasScale=100.f/length(mesh.maximum-mesh.minimum);
    for(const auto& v:mesh.vertices)atlasPositions.push_back((v.position-mesh.minimum)*atlasScale);
    xatlas::MeshDecl decl{};decl.vertexCount=uint32_t(mesh.vertices.size());decl.vertexPositionData=atlasPositions.data();decl.vertexPositionStride=sizeof(Vec3);decl.vertexNormalData=&mesh.vertices[0].normal;decl.vertexNormalStride=sizeof(Vertex);
    decl.indexCount=uint32_t(indices.size());decl.indexData=indices.data();decl.indexFormat=xatlas::IndexFormat::UInt32;decl.faceMaterialData=materials.data();
    require(xatlas::AddMesh(atlas.get(),decl)==xatlas::AddMeshError::Success,"xatlas rejected mesh");
    xatlas::PackOptions pack;pack.resolution=size;pack.padding=padding;pack.bilinear=true;
    xatlas::Generate(atlas.get(),xatlas::ChartOptions{},pack);
    require(atlas->atlasCount==1&&atlas->meshCount==1,"unwrap requires a single atlas");
    auto& output=atlas->meshes[0];std::vector<Vertex> vertices(output.vertexCount);
    for(size_t i=0;i<output.vertexCount;++i){auto& v=output.vertexArray[i];require(v.atlasIndex==0,"xatlas could not unwrap a face");vertices[i]=mesh.vertices[v.xref];vertices[i].uv={v.uv[0]/atlas->width,1-v.uv[1]/atlas->height};vertices[i].hasUv=true;}
    for(size_t i=0;i<mesh.triangles.size();++i)for(int j=0;j<3;++j)mesh.triangles[i].vertices[j]=output.indexArray[i*3+j];
    mesh.vertices=std::move(vertices);
    require(checkUvs(mesh,std::min(size,1024))["usable_for_baking"].get<bool>(),"generated atlas still overlaps or contains invalid UVs; inspect duplicate/intersecting geometry");
}
void saveObj(const Mesh& mesh, const std::filesystem::path& path) {
    require(path.extension()==".obj","unwrapped output must use .obj");
    auto materialPath=path;materialPath.replace_extension(".mtl");
    require(!std::filesystem::exists(path)&&!std::filesystem::exists(materialPath),"UV output already exists; choose a new filename");
    if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path),mtl(materialPath);require(bool(file)&&bool(mtl),"cannot create OBJ/MTL");
    file<<std::setprecision(9)<<"# TexUtil static mesh; positions in meters, Y up\nmtllib "<<materialPath.filename().string()<<'\n';
    for(const auto& v:mesh.vertices)file<<"v "<<v.position.x<<' '<<v.position.y<<' '<<v.position.z<<'\n';
    for(const auto& v:mesh.vertices)file<<"vt "<<v.uv.x<<' '<<v.uv.y<<'\n';
    for(const auto& v:mesh.vertices)file<<"vn "<<v.normal.x<<' '<<v.normal.y<<' '<<v.normal.z<<'\n';
    for(size_t i=0;i<mesh.materials.size();++i)mtl<<"# Original material: "<<Json(mesh.materials[i]).dump()<<"\nnewmtl material_"<<i<<"\nKd 0.5 0.5 0.5\n";
    uint32_t current=uint32_t(-1);
    for(const auto& t:mesh.triangles){if(t.material!=current){current=t.material;file<<"usemtl material_"<<current<<'\n';}file<<"f";for(auto i:t.vertices)file<<' '<<i+1<<'/'<<i+1<<'/'<<i+1;file<<'\n';}
    file.flush();mtl.flush();require(bool(file)&&bool(mtl),"failed writing OBJ/MTL");
}
}
