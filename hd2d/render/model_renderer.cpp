#include "render/model_renderer.h"
#include "render/gl_program.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstddef>
#include <memory>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <limits>

namespace hd2d {
using namespace gl;
namespace {
const char *vertex_source=R"(#version 460 core
layout(location=0) in vec3 a_position;
layout(location=1) in vec3 a_normal;
layout(location=2) in vec2 a_uv;
layout(location=3) in vec3 a_offset;
layout(location=4) in vec3 a_scale;
layout(location=5) in float a_yaw;
layout(location=6) in vec3 a_tint;
uniform mat4 u_vp;
uniform mat4 u_instance;
out vec3 v_world;
flat out vec2 v_model_origin;
flat out vec3 v_model_tint;
out vec3 v_normal;
out vec2 v_uv;
void main() {
    mat2 turn=mat2(cos(a_yaw),sin(a_yaw),-sin(a_yaw),cos(a_yaw));
    vec3 local=vec3(turn*a_position.xy,a_position.z)*a_scale;
    vec4 world=u_instance*vec4(local,1.0);
    world.xyz += a_offset;
    v_world=world.xyz;
    v_model_tint=a_tint;
    v_model_origin=(u_instance*vec4(0.0,0.0,0.0,1.0)).xy+a_offset.xy;
    v_normal=mat3(u_instance)*(vec3(turn*a_normal.xy,a_normal.z)/a_scale);
    v_uv=a_uv;
    gl_Position=u_vp*world;
}
)";
const char *fragment_body=R"(
in vec3 v_world;
flat in vec2 v_model_origin;
flat in vec3 v_model_tint;
in vec3 v_normal;
in vec2 v_uv;
uniform sampler2D u_base;
uniform sampler2D u_occlusion;
uniform float u_occlusion_strength;
uniform int u_model_material;
uniform vec4 u_color;
uniform vec3 u_emissive;
uniform vec3 u_eye;
uniform float u_metallic;
uniform float u_roughness;
uniform int u_double_sided;
out vec4 o_color;
uniform int u_cut_count;
uniform vec4 u_cut_circle[12]; // screen xy, radius, depth
uniform vec4 u_cut_plane[12]; // horizontal normal xy, boundary, minimum height
uniform float u_cut_power[12];
float model_bayer(vec2 pixel) {
    const mat4 m=mat4(0.,8.,2.,10.,12.,4.,14.,6.,3.,11.,1.,9.,15.,7.,13.,5.);
    ivec2 p=ivec2(mod(pixel,4.));
    return (m[p.x][p.y]+0.5)/16.;
}
void main() {
    float keep=1.0;
    for(int i=0;i<12;++i) {
        if(i>=u_cut_count) break;
        vec4 c=u_cut_circle[i]; vec4 plane=u_cut_plane[i];
        if(c.z<=0.0 || gl_FragCoord.z>=c.w-0.00002
            || dot(v_model_origin,plane.xy)<=plane.z || v_world.z<=plane.w) continue;
        float d=length(gl_FragCoord.xy-c.xy)/c.z;
        keep=min(keep,pow(smoothstep(0.15,1.0,d),max(u_cut_power[i],0.05)));
    }
    if(model_bayer(gl_FragCoord.xy)>=keep) discard;
    vec3 n=normalize(v_normal);
    if (u_double_sided!=0 && !gl_FrontFacing) n=-n;
    vec3 linear_base=texture(u_base,v_uv).rgb*u_color.rgb;
    // glTF factors and sampled sRGB textures are linear. The existing voxel palette
    // and postprocess operate in display RGB without a final sRGB encoding pass.
    // Convert material color at this boundary so ambient light uses the same convention.
    vec3 base=mix(12.92*linear_base,1.055*pow(max(linear_base,vec3(0.0)),vec3(1.0/2.4))-0.055,
        step(vec3(0.0031308),linear_base));
    base *= v_model_tint;
    vec3 an=abs(n);
    vec2 face_uv=(an.z>=an.x && an.z>=an.y)?v_world.xy:((an.x>=an.y)?v_world.yz:v_world.xz);
    float ao=mix(1.0,texture(u_occlusion,v_uv).r,u_occlusion_strength);
    base=hd2d_wear_apply(base,ao,v_world,face_uv,n,u_model_material);
    float ndl=max(dot(n,u_light_dir),0.0);
    float shadow=hd2d_key_shadow(v_world,ndl);
    vec3 lit=hd2d_ambient(n)+hd2d_point_lights(v_world,n,0.10);
    vec3 direct=u_key_color*ndl*shadow;
    // Lit-range emphasis . Pass-through when no field is bound.
    vec3 range_light=hd2d_lit_range_surface(lit+direct,v_world,n,1.0);
    lit+=range_light-(lit+direct);
    vec3 view_dir=normalize(u_eye-v_world);
    vec3 h=normalize(view_dir+u_light_dir);
    float rough=max(u_roughness,0.08);
    float exponent=max(2.0,2.0/(rough*rough)-2.0);
    float spec=pow(max(dot(n,h),0.0),exponent)*(exponent+2.0)/8.0;
    vec3 f0=mix(vec3(0.04),base,u_metallic);
    // Initial material approximation; keep HDR output for the shared postprocess.
    o_color=vec4(base*(lit+direct)*(1.0-u_metallic)+f0*(direct*spec+lit*0.35)+u_emissive,1.0);
}
)";
const char *depth_fragment=R"(#version 460 core
void main() {}
)";
void matrices(GLuint program,const Mat4 &vp,const Mat4 &instance)
{
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program,"u_vp"),1,GL_FALSE,vp.m);
    glUniformMatrix4fv(glGetUniformLocation(program,"u_instance"),1,GL_FALSE,instance.m);
}
// Restore the draw state that differs from the other renderers. Texture units and
// vertex bindings follow the existing renderer convention: callers bind their own.
struct DrawState {
    GLint cull{},front{},blend{},cull_mode{};
    DrawState() {
        glGetIntegerv(GL_CULL_FACE,&cull); glGetIntegerv(0x0B46,&front);
        glGetIntegerv(GL_BLEND,&blend); glGetIntegerv(0x0B45,&cull_mode);
    }
    ~DrawState() {
        if(cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        if(blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glFrontFace(static_cast<GLenum>(front)); glCullFace(static_cast<GLenum>(cull_mode));
        glBindVertexArray(0); glUseProgram(0);
    }
};
}

bool ModelRenderer::init(const ModelAsset &asset,std::string &error)
{
    shutdown(); error.clear();
    program_=compile_program(vertex_source,(std::string("#version 460 core\n")+kLightingGlsl+kSurfaceWearGlsl+fragment_body).c_str(),error);
    if(!program_) return false;
    depth_program_=compile_program(vertex_source,depth_fragment,error);
    if(!depth_program_) { shutdown(); return false; }
    lights_.locate(program_);
    wear_uniforms_.locate(program_);
    glGenBuffers(1,&offsets_);
    auto fail=[&](const std::string &reason) { error=reason; shutdown(); return false; };
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1,&white_); glBindTexture(GL_TEXTURE_2D,white_);
    const std::uint8_t white[]{255,255,255,255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    GLint unpack=4; glGetIntegerv(GL_UNPACK_ALIGNMENT,&unpack); glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    std::vector<bool> needs_ao(asset.images.size(),false);
    for(const auto &primitive:asset.primitives)
        if(primitive.material.occlusion.image>=0) needs_ao[static_cast<std::size_t>(primitive.material.occlusion.image)]=true;
    for(const auto &image:asset.images) {
        using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_FreeSurface)>;
        SDL_RWops *rw=SDL_RWFromConstMem(image.bytes.data(),static_cast<int>(image.bytes.size()));
        Surface decoded(rw?IMG_Load_RW(rw,1):nullptr,SDL_FreeSurface);
        if(!decoded) { glPixelStorei(GL_UNPACK_ALIGNMENT,unpack); return fail(std::string("Cannot decode model image: ")+IMG_GetError()); }
        if(decoded->w!=image.width || decoded->h!=image.height) {
            glPixelStorei(GL_UNPACK_ALIGNMENT,unpack); return fail("Decoded image dimensions differ from header");
        }
        Surface rgba(SDL_ConvertSurfaceFormat(decoded.get(),SDL_PIXELFORMAT_RGBA32,0),SDL_FreeSurface);
        if(!rgba) { glPixelStorei(GL_UNPACK_ALIGNMENT,unpack); return fail(SDL_GetError()); }
        // SDL rows are top-to-bottom, matching glTF's UV image convention.
        GLuint texture=0; glGenTextures(1,&texture); textures_.push_back(texture); glBindTexture(GL_TEXTURE_2D,texture);
        glTexImage2D(GL_TEXTURE_2D,0,GL_SRGB8_ALPHA8,rgba->w,rgba->h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba->pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        GLuint ao_texture=0;
        if(needs_ao[textures_.size()-1]) {
            glGenTextures(1,&ao_texture);glBindTexture(GL_TEXTURE_2D,ao_texture);
            // AO is data, not sRGB color. Never gamma-decode it.
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,rgba->w,rgba->h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba->pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        }
        occlusion_textures_.push_back(ao_texture);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT,unpack);
    for(const auto &primitive:asset.primitives) {
        meshes_.push_back({}); auto &mesh=meshes_.back();
        mesh.material=primitive.material; mesh.count=static_cast<int>(primitive.vertices.size());
        glGenVertexArrays(1,&mesh.vao); glGenBuffers(1,&mesh.vbo);
        glBindVertexArray(mesh.vao); glBindBuffer(GL_ARRAY_BUFFER,mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(primitive.vertices.size()*sizeof(ModelVertex)),primitive.vertices.data(),GL_STATIC_DRAW);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(ModelVertex),reinterpret_cast<void *>(offsetof(ModelVertex,position)));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(ModelVertex),reinterpret_cast<void *>(offsetof(ModelVertex,normal)));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(ModelVertex),reinterpret_cast<void *>(offsetof(ModelVertex,u)));
    }
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER,0); glBindTexture(GL_TEXTURE_2D,0);
    if(glGetError()!=0) return fail("OpenGL rejected model resource upload");
    return true;
}
void ModelRenderer::upload_offsets(const Vec3 *offsets,std::size_t count,const ModelInstance *instances)
{
    if(!instances) {
        placement_buffer_.resize(count);
        for(std::size_t i=0;i<count;++i) {
            placement_buffer_[i]=ModelInstance{};
            if(offsets) placement_buffer_[i].position=offsets[i];
        }
        instances=placement_buffer_.data();
    }
    glBindBuffer(GL_ARRAY_BUFFER,offsets_);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(count*sizeof(ModelInstance)),instances,GL_STREAM_DRAW);
}
void ModelRenderer::bind_offsets(GLuint vao)
{
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,offsets_);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,sizeof(ModelInstance),reinterpret_cast<void *>(offsetof(ModelInstance,position)));
    glVertexAttribDivisor(3,1);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4,3,GL_FLOAT,GL_FALSE,sizeof(ModelInstance),reinterpret_cast<void *>(offsetof(ModelInstance,scale)));
    glVertexAttribDivisor(4,1);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5,1,GL_FLOAT,GL_FALSE,sizeof(ModelInstance),reinterpret_cast<void *>(offsetof(ModelInstance,yaw)));
    glVertexAttribDivisor(5,1);
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6,3,GL_FLOAT,GL_FALSE,sizeof(ModelInstance),reinterpret_cast<void *>(offsetof(ModelInstance,tint)));
    glVertexAttribDivisor(6,1);
}
void ModelRenderer::shutdown()
{
    for(auto &mesh:meshes_) { if(mesh.vbo)glDeleteBuffers(1,&mesh.vbo); if(mesh.vao)glDeleteVertexArrays(1,&mesh.vao); }
    meshes_.clear();
    if(offsets_) glDeleteBuffers(1,&offsets_); offsets_=0;
    for(auto texture:textures_) if(texture) glDeleteTextures(1,&texture);
    textures_.clear();
    for(auto texture:occlusion_textures_) if(texture) glDeleteTextures(1,&texture);
    occlusion_textures_.clear();
    if(white_)glDeleteTextures(1,&white_); white_=0;
    if(program_)glDeleteProgram(program_); program_=0;
    if(depth_program_)glDeleteProgram(depth_program_); depth_program_=0;
}
void ModelRenderer::draw(const Mat4 &vp,const Mat4 &instance,const SceneLighting &lighting,
    const Vec3 &eye,const Mat4 &light_vp,GLuint shadow,int shadow_side,
    const Vec3 *offsets,std::size_t count,const Cutaway *cutaways,std::size_t cutaway_count,const ModelInstance *instances)
{
    if(!ready() || count==0 || count>static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))return;
    if(!offsets && !instances) count=1;
    upload_offsets(offsets,count,instances);
    DrawState state; glDisable(GL_BLEND); glFrontFace(GL_CW); glCullFace(GL_BACK);
    matrices(program_,vp,instance);
    upload_wear(wear_uniforms_,wear_);
    glUniform1i(glGetUniformLocation(program_,"u_model_material"),static_cast<int>(wear_material_));
    const int cuts=static_cast<int>(cutaways ? std::min(cutaway_count,std::size_t(12)) : 0);
    if(cuts>0 && std::getenv("HD2D_MODEL_CUT_TRACE")) {
        static bool reported=false;
        if(!reported) {
            GLint viewport[4]{};glGetIntegerv(0x0BA2,viewport);
            const auto &c=cutaways[0];
            std::fprintf(stderr,"[model-cut] viewport=%d,%d,%d,%d center=%g,%g radius=%g depth=%g plane=%g,%g,%g cuts=%d\n",
                viewport[0],viewport[1],viewport[2],viewport[3],c.centre_x,c.centre_y,c.radius,c.depth,c.face_x,c.face_y,c.face_min,cuts);
            reported=true;
        }
    }
    glUniform1i(glGetUniformLocation(program_,"u_cut_count"),cuts);
    for(int i=0;i<cuts;++i) {
        const auto &c=cutaways[i];
        std::string suffix="["+std::to_string(i)+"]";
        glUniform4f(glGetUniformLocation(program_,("u_cut_circle"+suffix).c_str()),c.centre_x,c.centre_y,c.radius,c.depth);
        glUniform4f(glGetUniformLocation(program_,("u_cut_plane"+suffix).c_str()),c.face_x,c.face_y,c.face_min,c.roof_min_z);
        glUniform1f(glGetUniformLocation(program_,("u_cut_power"+suffix).c_str()),c.power);
    }
    upload_lighting(lights_,lighting,light_vp,shadow,shadow_side,1);
    glUniform1i(glGetUniformLocation(program_,"u_base"),0);
    glUniform3f(glGetUniformLocation(program_,"u_eye"),eye.x,eye.y,eye.z);
    glActiveTexture(GL_TEXTURE0);
    for(const auto &mesh:meshes_) {
        const auto &m=mesh.material;
        if(m.double_sided)glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
        glUniform1i(glGetUniformLocation(program_,"u_double_sided"),m.double_sided?1:0);
        glUniform4f(glGetUniformLocation(program_,"u_color"),m.color[0],m.color[1],m.color[2],m.color[3]);
        glUniform3f(glGetUniformLocation(program_,"u_emissive"),m.emissive[0],m.emissive[1],m.emissive[2]);
        glUniform1f(glGetUniformLocation(program_,"u_metallic"),m.metallic);
        glUniform1f(glGetUniformLocation(program_,"u_roughness"),m.roughness);
        glActiveTexture(GL_TEXTURE0+2);
        const auto &ao=m.occlusion;
        glBindTexture(GL_TEXTURE_2D,ao.image<0?white_:occlusion_textures_[static_cast<std::size_t>(ao.image)]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,ao.wrap_s);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,ao.wrap_t);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,ao.image<0?GL_LINEAR:ao.min_filter);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,ao.mag_filter);
        glUniform1i(glGetUniformLocation(program_,"u_occlusion"),2);
        glUniform1f(glGetUniformLocation(program_,"u_occlusion_strength"),ao.image<0?0.f:m.occlusion_strength);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,m.image<0?white_:textures_[static_cast<std::size_t>(m.image)]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,m.wrap_s); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,m.wrap_t);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,m.image<0?GL_LINEAR:m.min_filter);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,m.mag_filter);
        bind_offsets(mesh.vao); glDrawArraysInstanced(GL_TRIANGLES,0,mesh.count,static_cast<GLsizei>(count));
    }
}
void ModelRenderer::draw_depth(const Mat4 &vp,const Mat4 &instance,const Vec3 *offsets,std::size_t count,const ModelInstance *instances)
{
    if(!ready() || count==0 || count>static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))return;
    if(!offsets && !instances) count=1;
    upload_offsets(offsets,count,instances);
    DrawState state; glDisable(GL_BLEND); glFrontFace(GL_CW); glCullFace(GL_BACK);
    matrices(depth_program_,vp,instance);
    for(const auto &mesh:meshes_) {
        if(mesh.material.double_sided)glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
        bind_offsets(mesh.vao); glDrawArraysInstanced(GL_TRIANGLES,0,mesh.count,static_cast<GLsizei>(count));
    }
}
}
