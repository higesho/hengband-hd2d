#include "model/model_asset.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>

namespace hd2d {
namespace {
using Json = nlohmann::json;
constexpr std::size_t max_file = 64 * 1024 * 1024, max_vertices = 600000;
void require(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
std::size_t number(const Json &v)
{
    require(v.is_number_unsigned() || (v.is_number_integer() && v.get<std::int64_t>() >= 0), "Expected a nonnegative integer");
    auto n = v.get<std::uint64_t>();
    require(n <= max_file, "Integer exceeds resource limit");
    return static_cast<std::size_t>(n);
}
float real(const Json &v)
{
    require(v.is_number(), "Expected a number");
    float f = v.get<float>();
    require(std::isfinite(f) && std::abs(f) <= 1000000.f, "Nonfinite or excessive numeric value");
    return f;
}
void range(std::size_t offset, std::size_t count, std::size_t size)
{ require(offset <= size && count <= size - offset, "Buffer range exceeds its container"); }
std::uint32_t u32(const std::uint8_t *p)
{ return p[0] | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
std::uint32_t be32(const std::uint8_t *p)
{ return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3]; }
const Json &entry(const Json &doc, const char *key, std::size_t index)
{
    const auto &a = doc.at(key);
    require(a.is_array() && index < a.size(), "Invalid array reference");
    require(a[index].is_object(), "Expected an object");
    return a[index];
}
std::size_t optional_number(const Json &j, const char *key, std::size_t fallback = 0)
{ return j.contains(key) ? number(j.at(key)) : fallback; }
void no_extensions(const Json &j)
{
    if (j.is_object()) {
        for (const auto &kv : j.items()) {
            if (kv.key() == "extras") continue;
            if (kv.key() == "extensions" || kv.key() == "extensionsRequired")
                require(kv.value().empty(), "glTF extensions are unsupported");
            no_extensions(kv.value());
        }
    } else if (j.is_array()) for (const auto &v : j) no_extensions(v);
}
struct View { const std::uint8_t *data; std::size_t size, stride; };
View view(const Json &doc, std::size_t i, const std::vector<std::uint8_t> &bin)
{
    const auto &v = entry(doc, "bufferViews", i);
    require(number(v.at("buffer")) == 0, "Only the embedded BIN buffer is supported");
    auto offset = optional_number(v, "byteOffset"), size = number(v.at("byteLength"));
    range(offset, size, bin.size());
    auto stride = optional_number(v, "byteStride");
    require(!stride || (stride >= 4 && stride <= 252 && stride % 4 == 0), "Invalid bufferView stride");
    return {bin.data() + offset, size, stride};
}
struct Accessor {
    const std::uint8_t *data{};
    std::size_t count{}, stride{}, components{}, component_size{};
    int type{};
    float scalar(std::size_t i, std::size_t c) const {
        const auto *p = data + i * stride + c * component_size;
        if (type == 5126) {
            std::uint32_t bits = u32(p); float f; std::memcpy(&f, &bits, sizeof(f));
            require(std::isfinite(f) && std::abs(f) <= 1000000.f, "Nonfinite or excessive vertex value");
            return f;
        }
        return static_cast<float>(index(i));
    }
    std::uint32_t index(std::size_t i) const {
        const auto *p = data + i * stride;
        return type == 5121 ? p[0] : type == 5123 ? (p[0] | (std::uint32_t(p[1]) << 8)) : u32(p);
    }
};
Accessor accessor(const Json &doc, std::size_t i, const std::vector<std::uint8_t> &bin,
    const char *shape, bool indices = false)
{
    const auto &a = entry(doc, "accessors", i);
    require(!a.contains("sparse"), "Sparse accessors are unsupported");
    require(a.at("type") == shape, "Unexpected accessor shape");
    require(!a.value("normalized", false), "Normalized accessors are unsupported");
    int type = static_cast<int>(number(a.at("componentType")));
    require(indices ? (type == 5121 || type == 5123 || type == 5125) : type == 5126, "Unsupported accessor component type");
    const auto v = view(doc, number(a.at("bufferView")), bin);
    std::size_t comp = std::strcmp(shape, "VEC3") == 0 ? 3 : std::strcmp(shape, "VEC2") == 0 ? 2 : 1;
    std::size_t bytes = type == 5121 ? 1 : type == 5123 ? 2 : 4;
    auto count = number(a.at("count")), offset = optional_number(a, "byteOffset");
    require(count > 0 && count <= max_vertices, "Accessor count exceeds limit or is zero");
    std::size_t stride = v.stride ? v.stride : bytes * comp;
    require(stride >= bytes * comp && offset % bytes == 0, "Invalid accessor stride or alignment");
    require(!indices || !v.stride, "Index buffer cannot be interleaved");
    range(offset, (count - 1) * stride + bytes * comp, v.size);
    return {v.data + offset, count, stride, comp, bytes, type};
}
Vec3 position(const Mat4 &m, Vec3 p)
{
    Vec3 v{m.m[0]*p.x+m.m[4]*p.y+m.m[8]*p.z+m.m[12],
        m.m[1]*p.x+m.m[5]*p.y+m.m[9]*p.z+m.m[13],
        m.m[2]*p.x+m.m[6]*p.y+m.m[10]*p.z+m.m[14]};
    require(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
        && std::abs(v.x)<1000000 && std::abs(v.y)<1000000 && std::abs(v.z)<1000000, "Transformed position exceeds limit");
    return v;
}
Vec3 normal(const Mat4 &inv, Vec3 n)
{
    Vec3 v{inv.m[0]*n.x+inv.m[1]*n.y+inv.m[2]*n.z,
        inv.m[4]*n.x+inv.m[5]*n.y+inv.m[6]*n.z,
        inv.m[8]*n.x+inv.m[9]*n.y+inv.m[10]*n.z};
    require(dot(v,v)>1e-20f && std::isfinite(dot(v,v)), "Invalid normal");
    return normalize(v);
}
Vec3 convert(Vec3 v) { return {v.x, -v.z, v.y}; }
float determinant(const Mat4 &m)
{ return m.m[0]*(m.m[5]*m.m[10]-m.m[9]*m.m[6])-m.m[4]*(m.m[1]*m.m[10]-m.m[9]*m.m[2])+m.m[8]*(m.m[1]*m.m[6]-m.m[5]*m.m[2]); }
void floats(const Json &j, float *out, std::size_t n)
{
    require(j.is_array() && j.size() == n, "Invalid vector size");
    for (std::size_t i = 0; i<n; ++i) out[i] = real(j[i]);
}
Mat4 node_matrix(const Json &j)
{
    Mat4 m = Mat4::identity();
    if (j.contains("matrix")) {
        require(!j.contains("translation") && !j.contains("rotation") && !j.contains("scale"), "Node mixes matrix and TRS");
        floats(j.at("matrix"), m.m, 16);
        require(m.m[3]==0 && m.m[7]==0 && m.m[11]==0 && m.m[15]==1, "Node matrix must be affine");
    } else {
        float t[3]{}, q[4]{0,0,0,1}, s[3]{1,1,1};
        if (j.contains("translation")) floats(j.at("translation"),t,3);
        if (j.contains("rotation")) floats(j.at("rotation"),q,4);
        if (j.contains("scale")) floats(j.at("scale"),s,3);
        float len=q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];
        require(std::abs(len-1)<0.001f, "Rotation quaternion must be normalized");
        float x=q[0], y=q[1], z=q[2], w=q[3];
        m.m[0]=(1-2*y*y-2*z*z)*s[0]; m.m[1]=(2*x*y+2*w*z)*s[0]; m.m[2]=(2*x*z-2*w*y)*s[0];
        m.m[4]=(2*x*y-2*w*z)*s[1]; m.m[5]=(1-2*x*x-2*z*z)*s[1]; m.m[6]=(2*y*z+2*w*x)*s[1];
        m.m[8]=(2*x*z+2*w*y)*s[2]; m.m[9]=(2*y*z-2*w*x)*s[2]; m.m[10]=(1-2*x*x-2*y*y)*s[2];
        m.m[12]=t[0]; m.m[13]=t[1]; m.m[14]=t[2];
    }
    require(std::abs(determinant(m))>1e-12f, "Singular node transform");
    return m;
}
ModelImage image(const Json &doc, std::size_t i, const std::vector<std::uint8_t> &bin, std::size_t encoded_remaining)
{
    const auto &j=entry(doc,"images",i);
    require(!j.contains("uri"), "External and data image URIs are unsupported; embed images in GLB");
    auto v=view(doc,number(j.at("bufferView")),bin);
    require(!v.stride && v.size>0, "Invalid image bufferView");
    require(v.size<=encoded_remaining, "Encoded images exceed 64 MiB");
    int width=0,height=0;
    if (j.at("mimeType")=="image/png") {
        const std::uint8_t sig[]{137,80,78,71,13,10,26,10};
        require(v.size>=33 && std::memcmp(v.data,sig,8)==0 && be32(v.data+8)==13
            && std::memcmp(v.data+12,"IHDR",4)==0, "Invalid PNG header");
        auto w=be32(v.data+16),h=be32(v.data+20);
        require(w>0 && h>0 && w<=4096 && h<=4096, "Image dimensions exceed 4096");
        width=static_cast<int>(w); height=static_cast<int>(h);
    } else {
        require(j.at("mimeType")=="image/jpeg", "Only PNG/JPEG base color images are supported");
        require(v.size>4 && v.data[0]==255 && v.data[1]==216, "Invalid JPEG header");
        std::size_t p=2;
        while (p+4<=v.size) {
            require(v.data[p++]==255, "Invalid JPEG marker");
            while (p<v.size && v.data[p]==255) ++p;
            require(p<v.size, "Truncated JPEG marker");
            auto marker=v.data[p++];
            if (marker==0xda || marker==0xd9) break;
            if (marker==1 || (marker>=0xd0 && marker<=0xd7)) continue;
            range(p,2,v.size); auto n=(std::size_t(v.data[p])<<8)|v.data[p+1];
            require(n>=2, "Invalid JPEG segment"); range(p,n,v.size);
            if (marker==0xc0 || marker==0xc1 || marker==0xc2) {
                require(n>=8, "Invalid JPEG frame");
                height=(int(v.data[p+3])<<8)|v.data[p+4]; width=(int(v.data[p+5])<<8)|v.data[p+6]; break;
            }
            p+=n;
        }
        require(width>0 && height>0 && width<=4096 && height<=4096, "Missing or excessive JPEG dimensions");
    }
    return {std::vector<std::uint8_t>(v.data,v.data+v.size),width,height};
}
ModelMaterial material(const Json &doc, const Json &prim, const std::vector<std::uint8_t> &bin, ModelAsset &out,
    std::vector<int> &image_map)
{
    ModelMaterial result;
    const auto read_texture=[&](const Json &info) {
        ModelTextureSlot slot;
        require(optional_number(info,"texCoord")==0, "Only TEXCOORD_0 is supported");
        const auto &tex=entry(doc,"textures",number(info.at("index")));
        auto index=number(tex.at("source"));
        require(index<image_map.size(), "Invalid image reference");
        if (image_map[index]<0) {
            std::size_t encoded_remaining=64*1024*1024;
            for (const auto &existing:out.images) encoded_remaining-=existing.bytes.size();
            auto im=image(doc,index,bin,encoded_remaining);
            std::size_t bytes=std::size_t(im.width)*im.height*4;
            for (const auto &existing:out.images) bytes+=std::size_t(existing.width)*existing.height*4;
            require(bytes<=64*1024*1024, "Decoded images exceed 64 MiB");
            image_map[index]=static_cast<int>(out.images.size()); out.images.push_back(std::move(im));
        }
        slot.image=image_map[index];
        if (tex.contains("sampler")) {
            const auto &sam=entry(doc,"samplers",number(tex.at("sampler")));
            slot.wrap_s=static_cast<int>(optional_number(sam,"wrapS",10497));
            slot.wrap_t=static_cast<int>(optional_number(sam,"wrapT",10497));
            slot.min_filter=static_cast<int>(optional_number(sam,"minFilter",9729));
            slot.mag_filter=static_cast<int>(optional_number(sam,"magFilter",9729));
            for (int wrap:{slot.wrap_s,slot.wrap_t}) require(wrap==10497 || wrap==33071 || wrap==33648, "Invalid texture wrapping");
            require(slot.mag_filter==9728 || slot.mag_filter==9729, "Invalid magnification filter");
            require(slot.min_filter==9728 || slot.min_filter==9729 || (slot.min_filter>=9984 && slot.min_filter<=9987), "Invalid minification filter");
        }
        return slot;
    };
    if (!prim.contains("material")) return result;
    const auto &m=entry(doc,"materials",number(prim.at("material")));
    require(m.value("alphaMode", std::string("OPAQUE"))=="OPAQUE", "Only OPAQUE materials are supported");
    require(!m.contains("normalTexture") && !m.contains("emissiveTexture"), "Normal/emissive textures are unsupported");
    if (m.contains("emissiveFactor")) {
        floats(m.at("emissiveFactor"),result.emissive,3);
        for(float value:result.emissive) require(value>=0 && value<=1,"Emissive factor outside [0,1]");
    }
    if (m.contains("occlusionTexture")) {
        const auto &info=m.at("occlusionTexture");
        if(info.contains("strength")) result.occlusion_strength=real(info.at("strength"));
        require(result.occlusion_strength>=0 && result.occlusion_strength<=1,"Occlusion strength outside [0,1]");
        result.occlusion=read_texture(info);
    }
    result.double_sided=m.value("doubleSided",false);
    if (!m.contains("pbrMetallicRoughness")) return result;
    const auto &p=m.at("pbrMetallicRoughness");
    require(p.is_object(), "Invalid PBR material");
    require(!p.contains("metallicRoughnessTexture"), "Metallic/roughness textures are unsupported");
    if (p.contains("baseColorFactor")) floats(p.at("baseColorFactor"),result.color,4);
    for (float c:result.color) require(c>=0 && c<=1, "Base color outside [0,1]");
    if (p.contains("metallicFactor")) result.metallic=real(p.at("metallicFactor"));
    if (p.contains("roughnessFactor")) result.roughness=real(p.at("roughnessFactor"));
    require(result.metallic>=0 && result.metallic<=1 && result.roughness>=0 && result.roughness<=1, "PBR factor outside [0,1]");
    if (p.contains("baseColorTexture")) {
        const auto slot=read_texture(p.at("baseColorTexture"));
        result.image=slot.image;result.wrap_s=slot.wrap_s;result.wrap_t=slot.wrap_t;
        result.min_filter=slot.min_filter;result.mag_filter=slot.mag_filter;
    }
    return result;
}
}

bool validate_terrain_model(const ModelAsset &asset, bool wall, std::string &error)
{
    const auto &lo=asset.bounds_min; const auto &hi=asset.bounds_max;
    const bool footprint=lo.x>=-0.501f && lo.y>=-0.501f && hi.x<=0.501f && hi.y<=0.501f
        && hi.x-lo.x>=0.95f && hi.y-lo.y>=0.95f;
    const bool height=wall ? (lo.z>=-0.2f && lo.z<=0.001f && hi.z>=0.1f && hi.z<=2.f)
        : (lo.z>=-0.5f && lo.z< -0.001f && std::abs(hi.z)<=0.001f);
    if(!footprint || !height || asset.vertex_count==0 || asset.vertex_count>30000 || asset.primitives.size()>32) {
        error=wall ? "Wall GLB needs a centered 1x1 footprint, base Z=0 and height <=2; <=10000 triangles/32 primitives"
                   : "Floor GLB needs a centered 1x1 footprint, top Z=0 and thickness <=0.5; <=10000 triangles/32 primitives";
        return false;
    }
    error.clear(); return true;
}

bool load_glb(const std::string &path, ModelAsset &out, std::string &error)
{
    error.clear();
    try {
        std::ifstream in(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
        require(bool(in), "Cannot open GLB file");
        auto length=in.tellg(); require(length>=20 && length<=static_cast<std::streamoff>(max_file), "GLB must be 20 bytes to 64 MiB");
        std::vector<std::uint8_t> file(static_cast<std::size_t>(length)); in.seekg(0); in.read(reinterpret_cast<char *>(file.data()),length);
        require(bool(in), "Cannot read GLB file");
        require(u32(file.data())==0x46546c67 && u32(file.data()+4)==2 && u32(file.data()+8)==file.size(), "Invalid GLB header/version/length");
        auto json_size=u32(file.data()+12);
        require(u32(file.data()+16)==0x4e4f534a && json_size>0 && json_size<=8*1024*1024 && json_size%4==0, "Invalid GLB JSON chunk");
        range(20,json_size,file.size());
        // Bound JSON nesting before the parser or recursive schema walk sees it.
        int depth=0; bool quoted=false,escaped=false;
        for (std::size_t i=20;i<20+json_size;++i) {
            char c=static_cast<char>(file[i]);
            if (quoted) { if (escaped) escaped=false; else if (c=='\\') escaped=true; else if (c=='"') quoted=false; }
            else if (c=='"') quoted=true;
            else if (c=='{' || c=='[') { require(++depth<=128,"JSON nesting exceeds 128"); }
            else if (c=='}' || c==']') --depth;
        }
        Json doc=Json::parse(file.begin()+20,file.begin()+20+json_size);
        require(doc.is_object() && doc.at("asset").at("version")=="2.0", "Expected glTF 2.0");
        no_extensions(doc);
        for (const char *key:{"animations","skins"}) if (doc.contains(key)) require(doc.at(key).empty(), "Animations and skins are unsupported");
        auto offset=std::size_t(20)+json_size; range(offset,8,file.size());
        auto bin_size=u32(file.data()+offset);
        require(u32(file.data()+offset+4)==0x004e4942 && bin_size%4==0, "Expected one GLB BIN chunk");
        range(offset+8,bin_size,file.size()); require(offset+8+bin_size==file.size(), "Additional GLB chunks are unsupported");
        require(doc.at("buffers").is_array() && doc.at("buffers").size()==1, "Expected one embedded buffer");
        const auto &buffer=entry(doc,"buffers",0);
        require(!buffer.contains("uri"), "External and data buffer URIs are unsupported");
        auto payload_size=number(buffer.at("byteLength"));
        require(payload_size<=bin_size && bin_size-payload_size<=3, "BIN length differs from declared buffer length");
        std::vector<std::uint8_t> bin(file.begin()+offset+8,file.begin()+offset+8+payload_size);
        const auto &nodes=doc.at("nodes"); require(nodes.is_array() && nodes.size()<=4096, "Invalid node list");
        ModelAsset result;
        if (doc.contains("images")) {
            require(doc.at("images").is_array() && doc.at("images").size()<=256, "Image count exceeds 256");
            for (const auto &im:doc.at("images")) require(im.is_object() && !im.contains("uri"), "External and data image URIs are unsupported; embed images in GLB");
        }
        std::vector<int> image_map(doc.contains("images")?doc.at("images").size():0,-1);
        std::vector<bool> visited(nodes.size(),false);
        std::size_t visits=0;
        std::function<void(std::size_t,const Mat4 &,int)> walk;
        walk=[&](std::size_t index,const Mat4 &parent,int level) {
            require(index<nodes.size() && level<=128 && ++visits<=4096, "Invalid node reference/depth/count");
            require(!visited[index], "Cyclic or multiply parented scene node"); visited[index]=true;
            const auto &node=nodes[index]; require(node.is_object(), "Invalid node");
            require(!node.contains("skin") && !node.contains("weights"), "Skinning and morph weights are unsupported");
            Mat4 world=parent*node_matrix(node); float det=determinant(world);
            require(std::isfinite(det) && std::abs(det)>1e-12f, "Singular composed node transform");
            Mat4 inv=inverse(world);
            if (node.contains("mesh")) {
                const auto &mesh=entry(doc,"meshes",number(node.at("mesh")));
                require(!mesh.contains("weights"), "Morph weights are unsupported");
                const auto &primitives=mesh.at("primitives"); require(primitives.is_array(), "Invalid primitive list");
                for (const auto &p:primitives) {
                    require(result.primitives.size()<4096, "Primitive count exceeds 4096");
                    require(optional_number(p,"mode",4)==4 && !p.contains("targets"), "Only static TRIANGLES are supported");
                    const auto &attrs=p.at("attributes"); require(attrs.is_object(), "Invalid attributes");
                    for (const auto &a:attrs.items()) require(a.key()=="POSITION" || a.key()=="NORMAL" || a.key()=="TEXCOORD_0" || a.key()=="TANGENT", "Unsupported vertex attribute");
                    auto pos=accessor(doc,number(attrs.at("POSITION")),bin,"VEC3");
                    Accessor normals,uv,indices;
                    if (attrs.contains("NORMAL")) { normals=accessor(doc,number(attrs.at("NORMAL")),bin,"VEC3"); require(normals.count==pos.count,"NORMAL count mismatch"); }
                    if (attrs.contains("TEXCOORD_0")) { uv=accessor(doc,number(attrs.at("TEXCOORD_0")),bin,"VEC2"); require(uv.count==pos.count,"UV count mismatch"); }
                    if (p.contains("indices")) indices=accessor(doc,number(p.at("indices")),bin,"SCALAR",true);
                    std::size_t count=indices.count?indices.count:pos.count;
                    require(count%3==0 && count<=max_vertices-result.vertex_count,"Triangle count exceeds limit or is incomplete");
                    ModelPrimitive primitive; primitive.material=material(doc,p,bin,result,image_map);
                    require((primitive.material.image<0 && primitive.material.occlusion.image<0) || uv.count>0, "Textured primitive requires TEXCOORD_0");
                    primitive.vertices.reserve(count);
                    for (std::size_t k=0;k<count;++k) {
                        // Mirrored nodes need winding reversal after their transform is baked.
                        auto source=det<0 ? (k/3)*3+(k%3==1?2:k%3==2?1:0) : k;
                        auto vertex=indices.count?indices.index(source):source;
                        require(vertex<pos.count, "Vertex index exceeds POSITION count");
                        ModelVertex v;
                        v.position=convert(position(world,{pos.scalar(vertex,0),pos.scalar(vertex,1),pos.scalar(vertex,2)}));
                        if (normals.count) v.normal=convert(normal(inv,{normals.scalar(vertex,0),normals.scalar(vertex,1),normals.scalar(vertex,2)}));
                        if (uv.count) { v.u=uv.scalar(vertex,0); v.v=uv.scalar(vertex,1); }
                        primitive.vertices.push_back(v);
                    }
                    if (!normals.count) for (std::size_t k=0;k<count;k+=3) {
                        auto &a=primitive.vertices[k]; auto &b=primitive.vertices[k+1]; auto &c=primitive.vertices[k+2];
                        Vec3 n=normalize(cross(b.position-a.position,c.position-a.position));
                        require(dot(n,n)>0.5f,"Degenerate triangle without normals"); a.normal=b.normal=c.normal=n;
                    }
                    for (const auto &v:primitive.vertices) {
                        if (result.vertex_count++==0) result.bounds_min=result.bounds_max=v.position;
                        auto &lo=result.bounds_min; auto &hi=result.bounds_max;
                        lo.x=std::min(lo.x,v.position.x); lo.y=std::min(lo.y,v.position.y); lo.z=std::min(lo.z,v.position.z);
                        hi.x=std::max(hi.x,v.position.x); hi.y=std::max(hi.y,v.position.y); hi.z=std::max(hi.z,v.position.z);
                    }
                    result.primitives.push_back(std::move(primitive));
                }
            }
            if (node.contains("children")) { require(node.at("children").is_array(),"Invalid children"); for (const auto &child:node.at("children")) walk(number(child),world,level+1); }
        };
        const auto &scene=entry(doc,"scenes",optional_number(doc,"scene"));
        require(scene.at("nodes").is_array(), "Invalid scene nodes");
        for (const auto &n:scene.at("nodes")) walk(number(n),Mat4::identity(),0);
        require(result.vertex_count>0, "Default scene contains no triangles");
        out=std::move(result); return true;
    } catch (const std::exception &e) { error=e.what(); return false; }
}
}
