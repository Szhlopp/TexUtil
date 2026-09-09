#include "texutil/texutil.hpp"
#include <fstream>
#include <set>
#include <stdexcept>

namespace tex {
namespace {
void require(bool ok, const std::string& message) { if(!ok)throw std::runtime_error("graph imports: "+message); }
bool identifier(const std::string& s) {
    auto letter=[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';};
    if(s.empty()||s.size()>128||!letter(s.front()))return false;
    for(char c:s)if(!letter(c)&&!(c>='0'&&c<='9'))return false;
    return true;
}
int integer(const Json& v, double low, double high, const std::string& field) {
    require(v.is_number_integer()&&v.get<double>()>=low&&v.get<double>()<=high,"invalid "+field);return v.get<int>();
}
// Imported output settings are not inherited, but malformed document metadata is still rejected.
void metadata(const Json& d) {
    require(d.is_object(),"document must be an object");
    static const std::set<std::string> allowed={"version","size","seed","tile","background","alpha","format","bits","srgb","nodes","outputs","imports"};
    for(auto it=d.begin();it!=d.end();++it)require(allowed.count(it.key()),"unknown document field '"+it.key()+"'");
    if(d.contains("version"))integer(d["version"],1,1,"version");
    if(d.contains("size")) {
        if(d["size"].is_array()){require(d["size"].size()==2,"size needs two dimensions");for(const auto& n:d["size"])integer(n,1,16384,"size");}
        else integer(d["size"],1,16384,"size");
    }
    if(d.contains("seed"))integer(d["seed"],-2147483648.0,2147483647.0,"seed");
    for(const auto* key:{"tile","alpha","srgb"})if(d.contains(key))require(d[key].is_boolean(),std::string(key)+" must be boolean");
    if(d.contains("background"))require(color(d["background"])[3]==1,"background must be opaque");
    if(d.contains("format"))require(d["format"]=="png"||d["format"]=="ppm"||d["format"]=="pgm"||d["format"]=="pfm","invalid format");
    if(d.contains("bits")){int b=integer(d["bits"],8,32,"bits");require(b==8||b==16||b==32,"invalid bits");}
    if(d.contains("outputs"))require(d["outputs"].is_object(),"outputs must be an object");
}
class Expander {
public:
    ImportedNodes result{Json::object(),{}};
    void expand(const Json& doc, const std::filesystem::path& base, const std::string& prefix, int seed, bool tile, unsigned depth) {
        require(depth<=16,"maximum import depth is 16");
        if(depth)metadata(doc);
        Json imports=doc.value("imports",Json::object());require(imports.is_object(),"imports must map aliases to JSON filenames");
        Json nodes=doc.value("nodes",Json::object());require(nodes.is_object(),"nodes must be an object");
        require(nodes.size()<=4096,"maximum 4096 nodes");
        for(auto it=imports.begin();it!=imports.end();++it) {
            require(identifier(it.key()),"alias '"+it.key()+"' must be an ASCII identifier, maximum 128 characters");
            require(it.value().is_string()&&!it.value().get<std::string>().empty(),"import '"+it.key()+"' needs a JSON filename");
            require(it.value().get<std::string>().find('\0')==std::string::npos,"import filenames cannot contain NUL characters");
            for(auto n=nodes.begin();n!=nodes.end();++n)require(n.key()!=it.key()&&n.key().rfind(it.key()+".",0)!=0,"local node '"+n.key()+"' collides with import namespace '"+it.key()+"'");
            require(++instances_<=128,"maximum 128 import instances");
            std::error_code pathError;
            auto file=std::filesystem::canonical(base/it.value().get<std::string>(),pathError);
            require(!pathError,"cannot resolve '"+it.value().get<std::string>()+"': "+pathError.message());
            require(active_.insert(file).second,"file cycle detected at '"+file.string()+"'");
            if(!documents_.count(file)) {
                require(std::filesystem::is_regular_file(file),"import must be a regular JSON file");
                auto bytes=std::filesystem::file_size(file);require(bytes<=8*1024*1024,"import file exceeds 8 MiB");
                bytes_+=bytes;require(bytes_<=32*1024*1024,"total imported JSON exceeds 32 MiB");
                std::ifstream stream(file);require(bool(stream),"cannot open '"+file.string()+"'");
                documents_[file]=Json::parse(stream);result.files.push_back(file);
            }
            const auto& child=documents_.at(file);
            try {
                metadata(child);
                int childSeed=child.contains("seed")?integer(child["seed"],-2147483648.0,2147483647.0,"seed"):42;
                expand(child,file.parent_path(),prefix+it.key()+".",childSeed,child.value("tile",false),depth+1);
            } catch(const std::exception& e){throw std::runtime_error("import '"+prefix+it.key()+"' ("+file.string()+"): "+e.what());}
            active_.erase(file);
        }
        nodes=expandPresets(std::move(nodes));
        require(result.nodes.size()+nodes.size()<=4096,"maximum 4096 nodes after imports and preset expansion");
        for(auto it=nodes.begin();it!=nodes.end();++it) {
            require(!it.key().empty(),"node names cannot be empty");
            const auto id=prefix+it.key();auto n=normalizedNode(id,it.value());
            const auto& parameters=specs_.at(n["op"].get<std::string>())["parameters"];
            for(auto p=parameters.begin();p!=parameters.end();++p) {
                if(p.key()=="seed"&&!n.contains("seed"))n["seed"]=seed;
                if(p.key()=="tile"&&!n.contains("tile"))n["tile"]=tile;
                if(!n.contains(p.key()))continue;
                if(p.value()["type"]=="reference")n[p.key()]=prefix+n[p.key()].get<std::string>();
                if(p.value()["type"]=="references")for(auto& ref:n[p.key()])ref=prefix+ref.get<std::string>();
            }
            if(n["op"]=="image")n["path"]=std::filesystem::absolute(base/n["path"].get<std::string>()).lexically_normal().string();
            require(!result.nodes.contains(id),"duplicate qualified node '"+id+"'");result.nodes[id]=std::move(n);
        }
    }
private:
    Json specs_=catalog();
    unsigned instances_=0;
    uintmax_t bytes_=0;
    std::set<std::filesystem::path> active_;
    std::map<std::filesystem::path,Json> documents_;
};
}
ImportedNodes expandImports(const Json& document, const std::filesystem::path& base, int seed, bool tile) {
    Expander expander;expander.expand(document,base,"",seed,tile,0);return std::move(expander.result);
}
}
