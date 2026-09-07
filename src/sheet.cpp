#include "texutil/texutil.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <set>

namespace tex {
namespace {
void require(bool condition, const std::string& message) { if (!condition) throw std::runtime_error("sheet: " + message); }
int integer(const Json& value, int lo, int hi, const std::string& name) { require(value.is_number_integer() && value.get<double>() >= lo && value.get<double>() <= hi, name + " must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi)); return value.get<int>(); }
std::string label(const Json& value) {
    require(value.is_string(), "labels and title must be strings"); std::string text = value;
    require(text.size() <= 128, "labels and title have a maximum of 128 characters");
    for (unsigned char ch : text) require((ch >= 32 && ch <= 126), "labels and title must use printable ASCII");
    return text;
}
using Glyph = std::array<unsigned char,7>;
Glyph glyph(char ch) {
    // Original compact 5x7 bitmap lettering. Lowercase is displayed as capitals.
    static const std::map<char,Glyph> font{
        {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}}, {'C',{14,17,16,16,16,17,14}},
        {'D',{30,17,17,17,17,17,30}}, {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
        {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}}, {'I',{14,4,4,4,4,4,14}},
        {'J',{7,2,2,2,18,18,12}}, {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
        {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,25,21,19,19,17}}, {'O',{14,17,17,17,17,17,14}},
        {'P',{30,17,17,30,16,16,16}}, {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
        {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}}, {'U',{17,17,17,17,17,17,14}},
        {'V',{17,17,17,17,17,10,4}}, {'W',{17,17,17,21,21,27,17}}, {'X',{17,17,10,4,10,17,17}},
        {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
        {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}}, {'2',{14,17,1,2,4,8,31}},
        {'3',{30,1,1,14,1,1,30}}, {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
        {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
        {' ',{0,0,0,0,0,0,0}}, {'.',{0,0,0,0,0,6,6}}, {',',{0,0,0,0,6,6,4}}, {':',{0,6,6,0,6,6,0}}, {';',{0,6,6,0,6,6,4}},
        {'-',{0,0,0,31,0,0,0}}, {'_',{0,0,0,0,0,0,31}}, {'/',{1,2,2,4,8,8,16}}, {'\\',{16,8,8,4,2,2,1}},
        {'(',{2,4,8,8,8,4,2}}, {')',{8,4,2,2,2,4,8}}, {'[',{14,8,8,8,8,8,14}}, {']',{14,2,2,2,2,2,14}},
        {'+',{0,4,4,31,4,4,0}}, {'=',{0,0,31,0,31,0,0}}, {'%',{25,25,2,4,8,19,19}}, {'#',{10,31,10,10,31,10,0}},
        {'!',{4,4,4,4,4,0,4}}, {'?',{14,17,1,2,4,0,4}}, {'&',{12,18,20,8,21,18,13}}, {'*',{0,21,14,31,14,21,0}},
        {'<',{2,4,8,16,8,4,2}}, {'>',{8,4,2,1,2,4,8}}, {'|',{4,4,4,4,4,4,4}}, {'"',{10,10,10,0,0,0,0}}, {'\'',{4,4,8,0,0,0,0}},
        {'@',{14,17,23,21,23,16,14}}, {'$',{4,15,20,14,5,30,4}}, {'^',{4,10,17,0,0,0,0}}, {'`',{8,4,0,0,0,0,0}},
        {'{',{2,4,4,8,4,4,2}}, {'}',{8,4,4,2,4,4,8}}, {'~',{0,0,9,22,0,0,0}}
    };
    auto found = font.find(static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))); return found == font.end() ? font.at('?') : found->second;
}
void drawText(Image& image, std::string text, int x, int y, int width, int scale, Pixel color) {
    size_t capacity = static_cast<size_t>(std::max(0,width) / (6 * scale));
    if (text.size() > capacity) text = capacity >= 3 ? text.substr(0,capacity-3) + "..." : text.substr(0,capacity);
    for (char ch : text) {
        auto rows = glyph(ch);
        for (int gy=0;gy<7;++gy) for (int gx=0;gx<5;++gx) if (rows[gy] & (1 << (4-gx))) for (int dy=0;dy<scale;++dy) for (int dx=0;dx<scale;++dx) image.set(x+gx*scale+dx,y+gy*scale+dy,color);
        x += 6 * scale;
    }
}
}
Sheet parseSheet(const Json& o) {
    Sheet s;
    require(o.contains("items") && o["items"].is_array() && !o["items"].empty() && o["items"].size() <= 256, "items must contain 1..256 node references or {node,label} objects");
    for (auto item : o["items"]) {
        if (item.is_string()) item = {{"node",item}};
        require(item.is_object() && item.contains("node") && item["node"].is_string() && !item["node"].get<std::string>().empty(), "each item requires a node name");
        for (auto it=item.begin();it!=item.end();++it) require(it.key()=="node" || it.key()=="label", "unknown item field '"+it.key()+"'");
        s.items.push_back({item["node"],label(item.value("label",item["node"]))});
    }
    s.columns=integer(o.value("columns",Json(3)),1,32,"columns");
    Json cell=o.value("cell",Json(256)); if (!cell.is_array()) cell=Json::array({cell,cell}); require(cell.size()==2,"cell must be a number or [width,height]");
    s.cellWidth=integer(cell[0],16,2048,"cell width");s.cellHeight=integer(cell[1],16,2048,"cell height");
    s.padding=integer(o.value("padding",Json(12)),0,128,"padding");s.fontScale=integer(o.value("font_scale",Json(2)),1,8,"font_scale");
    require(!o.contains("labels") || o["labels"].is_boolean(),"labels must be boolean");s.labels=o.value("labels",true);
    s.title=label(o.value("title",Json("")));s.background=color(o.value("background",Json("#171b20")));s.textColor=color(o.value("text_color",Json("#ebeff4")));
    require(s.background[3]==1 && s.textColor[3]==1,"background and text_color must be opaque");
    s.labelHeight=s.labels ? 7*s.fontScale+std::max(4,s.padding) : 0;
    s.titleHeight=s.title.empty() ? 0 : 7*(s.fontScale+1)+std::max(4,s.padding);
    int rows=(static_cast<int>(s.items.size())+s.columns-1)/s.columns;
    s.width=s.padding+s.columns*(s.cellWidth+s.padding);
    s.height=s.padding+s.titleHeight+rows*(s.cellHeight+s.labelHeight+s.padding);
    require(s.width<=16384 && s.height<=16384,"canvas exceeds 16384 pixels; reduce cell size or grid dimensions");
    return s;
}
ImagePtr createSheet(const Sheet& s, std::shared_ptr<Memory> memory) {
    auto canvas=std::make_shared<Image>(s.width,s.height,Kind::Color,std::move(memory));
    for(int y=0;y<s.height;++y)for(int x=0;x<s.width;++x)canvas->set(x,y,s.background);
    if(!s.title.empty())drawText(*canvas,s.title,s.padding,s.padding,s.width-2*s.padding,s.fontScale+1,s.textColor);
    for(size_t i=0;i<s.items.size();++i)if(s.labels) {int x=s.padding+int(i%s.columns)*(s.cellWidth+s.padding),y=s.padding+s.titleHeight+int(i/s.columns)*(s.cellHeight+s.labelHeight+s.padding)+s.cellHeight+std::max(4,s.padding)/2;drawText(*canvas,s.items[i].label,x,y,s.cellWidth,s.fontScale,s.textColor);}
    return canvas;
}
void drawSheetItem(Image& canvas, const Sheet& s, size_t index, const Image& source, Workers& workers) {
    float scale=std::min(float(s.cellWidth)/source.width,float(s.cellHeight)/source.height);
    int width=std::max(1,std::min(s.cellWidth,static_cast<int>(std::lround(source.width*scale)))),height=std::max(1,std::min(s.cellHeight,static_cast<int>(std::lround(source.height*scale))));
    int left=s.padding+int(index%s.columns)*(s.cellWidth+s.padding)+(s.cellWidth-width)/2;
    int top=s.padding+s.titleHeight+int(index/s.columns)*(s.cellHeight+s.labelHeight+s.padding)+(s.cellHeight-height)/2;
    workers.rows(height,[&](int y){for(int x=0;x<width;++x){
        Pixel p{};
        if(width>=source.width && height>=source.height) p=source.sample((x+.5f)/width,(y+.5f)/height,"clamp");
        else {
            // Exact box-area reduction suppresses aliasing in grain and fine normal maps.
            double x0=double(x)*source.width/width,x1=double(x+1)*source.width/width,y0=double(y)*source.height/height,y1=double(y+1)*source.height/height;
            std::array<double,4> sum{};
            for(int sy=int(std::floor(y0));sy<std::min(source.height,int(std::ceil(y1)));++sy)for(int sx=int(std::floor(x0));sx<std::min(source.width,int(std::ceil(x1)));++sx){
                double weight=(std::min(x1,double(sx+1))-std::max(x0,double(sx)))*(std::min(y1,double(sy+1))-std::max(y0,double(sy)));auto q=source.get(sx,sy);
                if(source.kind==Kind::Color)for(int k=0;k<3;++k)q[k]*=clamp(q[3]);
                for(int k=0;k<4;++k)sum[k]+=q[k]*weight;
            }
            double area=(x1-x0)*(y1-y0);for(int k=0;k<4;++k)p[k]=static_cast<float>(sum[k]/area);
            if(source.kind==Kind::Color)for(int k=0;k<3;++k)p[k]=p[3]>1e-8f?p[k]/p[3]:0;
        }
        for(float value:p)if(!std::isfinite(value))throw std::runtime_error("nonfinite pixels in sheet source");
        // Store data previews as linear equivalents of raw display values, so PNG
        // sRGB encoding does not brighten heightfields or encoded normal components.
        if(source.kind!=Kind::Color)for(int k=0;k<3;++k)p[k]=toLinear(clamp(p[k]));
        float alpha=clamp(p[3]);for(int k=0;k<3;++k)p[k]=p[k]*alpha+s.background[k]*(1-alpha);p[3]=1;
        canvas.set(left+x,top+y,p);
    }});
}
}
