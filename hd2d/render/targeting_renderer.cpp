#include "render/targeting_renderer.h"
#include "render/gl_program.h"
#include <cstddef>

namespace hd2d {
using namespace gl;
namespace {
constexpr float corners[]={-1,-1,1,-1,1,1,-1,-1,1,1,-1,1};
const char *vertex=R"(#version 460 core
layout(location=0) in vec2 a_corner;
layout(location=1) in vec3 a_center;
layout(location=2) in vec3 a_right;
layout(location=3) in vec3 a_up;
layout(location=4) in vec4 a_color;
layout(location=5) in vec3 a_style;
uniform mat4 u_matrix;
out vec2 v_local;
out vec2 v_map;
out vec4 v_color;
flat out vec3 v_style;
void main(){vec3 world=a_center+a_right*a_corner.x+a_up*a_corner.y;gl_Position=u_matrix*vec4(world,1.0);v_local=a_corner;v_map=world.xy;v_color=a_color;v_style=a_style;}
)";
const char *fragment=R"(#version 460 core
in vec2 v_local;
in vec2 v_map;
in vec4 v_color;
flat in vec3 v_style;
out vec4 o_color;
void main(){
    float alpha=1.0;vec3 color=v_color.rgb;
    if(v_style.x<0.5){
        float d=length(v_local),soft=max(fwidth(d),0.005);
        float ring=(1.0-smoothstep(0.94-soft,0.94+soft,d))*smoothstep(0.81-soft,0.81+soft,d);
        if(v_style.z>0.5&&fract((atan(v_local.y,v_local.x)+3.14159265)*12.0/6.2831853)>0.57)ring=0.0;
        vec2 a=abs(v_local);float crosshair=(1.0-smoothstep(0.025,0.055,min(a.x,a.y)))*(1.0-smoothstep(0.68,0.73,max(a.x,a.y)));
        alpha=max(ring,crosshair);
        if(v_style.y>0.5&&max(a.x,a.y)>0.89&&min(a.x,a.y)>0.73){o_color=vec4(1.0,1.0,1.0,0.95);return;}
    }else if(v_style.x<1.5){alpha=1.0;}
    else if(v_style.x<2.5){alpha=fract((v_map.x+v_map.y)*6.0)<0.23?1.6:0.35;}
    else{float d=length(v_local);alpha=1.0-smoothstep(0.72,1.0,d);}
    if(alpha<0.01)discard;o_color=vec4(color,v_color.a*alpha);
}
)";
}
bool TargetingRenderer::init(std::string &error) {
    program_=compile_program(vertex,fragment,error);if(!program_)return false;
    matrix_=glGetUniformLocation(program_,"u_matrix");
    glGenVertexArrays(1,&vao_);glGenBuffers(1,&vertices_);glGenBuffers(1,&instances_);glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER,vertices_);glBufferData(GL_ARRAY_BUFFER,sizeof(corners),corners,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),nullptr);
    glBindBuffer(GL_ARRAY_BUFFER,instances_);
    const size_t offsets[]={offsetof(TargetingQuad,center),offsetof(TargetingQuad,right),offsetof(TargetingQuad,up),offsetof(TargetingQuad,r),offsetof(TargetingQuad,style)};
    for(GLuint i=0;i<5;++i){glEnableVertexAttribArray(i+1);glVertexAttribPointer(i+1,i==3?4:3,GL_FLOAT,GL_FALSE,sizeof(TargetingQuad),reinterpret_cast<const void*>(offsets[i]));glVertexAttribDivisor(i+1,1);}
    glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);return true;
}
void TargetingRenderer::shutdown(){if(instances_)glDeleteBuffers(1,&instances_);if(vertices_)glDeleteBuffers(1,&vertices_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);instances_=vertices_=vao_=program_=0;capacity_=0;}
void TargetingRenderer::draw(const Mat4 &matrix,const std::vector<TargetingQuad> &quads,bool depth_test) {
    if(!program_||quads.empty())return;
    if(depth_test)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);glUniformMatrix4fv(matrix_,1,GL_FALSE,matrix.m);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,instances_);
    const size_t bytes=quads.size()*sizeof(TargetingQuad);
    if(bytes>capacity_){glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(bytes),quads.data(),GL_STREAM_DRAW);capacity_=bytes;}else glBufferSubData(GL_ARRAY_BUFFER,0,GLsizeiptr(bytes),quads.data());
    glDrawArraysInstanced(GL_TRIANGLES,0,6,GLsizei(quads.size()));
    glBindVertexArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);glUseProgram(0);glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);
}
}
