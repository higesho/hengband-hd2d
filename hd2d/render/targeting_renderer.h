#pragma once
#include "render/targeting_visual.h"
#include "render/gl_core.h"
#include <string>

namespace hd2d {
class TargetingRenderer {
public:
    bool init(std::string &error);
    bool ready()const{return program_!=0;}
    void shutdown();
    void draw(const Mat4 &,const std::vector<TargetingQuad> &,bool depth_test);
private:
    gl::GLuint program_{},vao_{},vertices_{},instances_{};
    gl::GLint matrix_{-1};
    size_t capacity_{};
};
}
