/*!
 * @file ui_paint.cpp
 * @brief `ui_paint.h` の実装。
 */
#include "ui/ui_paint.h"

#include "render/gl_program.h"
#include "render/term_colors.h"

#include <algorithm>
#include <cmath>

namespace hd2d {

using namespace hd2d::gl;

namespace {

const char *const kVertexSource = R"(#version 460 core
layout(location = 0) in vec2 a_pos;    // 画素座標（左上原点）
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec4 a_surface;
layout(location = 3) in float a_material;
uniform vec2 u_screen;
out vec4 v_color;
out vec4 v_surface;
flat out float v_material;
void main()
{
    vec2 ndc = vec2(a_pos.x / u_screen.x * 2.0 - 1.0, 1.0 - a_pos.y / u_screen.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = a_color;
    v_surface = a_surface;
    v_material = a_material;
}
)";

const char *const kFragmentSource = R"(#version 460 core
in vec4 v_color;
in vec4 v_surface;
flat in float v_material;
uniform sampler2D u_backdrop;
uniform vec2 u_screen;
out vec4 o_color;
float grain(vec2 p)
{
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float clouds(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(grain(i), grain(i + vec2(1.0, 0.0)), f.x),
        mix(grain(i + vec2(0.0, 1.0)), grain(i + vec2(1.0)), f.x), f.y);
}
void main()
{
    // Flat UI remains exactly the original shader path.
    if (v_material < 0.5) { o_color = v_color; return; }
    vec2 p = v_surface.xy;
    vec2 uv = p / max(v_surface.zw, vec2(1.0));
    float edge = pow(max(abs(uv.x * 2.0 - 1.0), abs(uv.y * 2.0 - 1.0)), 8.0);
    float fine = grain(floor(p));
    float cloud = clouds(p / 65.0);
    vec3 color = v_color.rgb;
    if (v_material < 1.5) {
        // Fine slate grain, broad mineral variation, darker edges.
        float vein = smoothstep(0.87, 0.98, clouds(vec2(p.x / 160.0, p.y / 8.0)));
        color += vec3((fine - 0.5) * 0.019 + (cloud - 0.5) * 0.021 + vein * 0.010);
        color *= 1.0 - edge * 0.24;
    } else if (v_material < 2.5) {
        // Warm paper, mottling and narrow fibres. No time-dependent noise.
        float fibre = step(0.975, grain(floor(p / vec2(17.0, 1.0))));
        float fleck = step(0.996, fine);
        color += vec3(0.09, 0.075, 0.05) * (cloud - 0.5);
        color += (fine - 0.5) * 0.028;
        color -= vec3(0.030, 0.023, 0.014) * fibre;
        color -= vec3(0.14, 0.11, 0.065) * fleck;
        color -= vec3(0.105, 0.10, 0.075) * edge;
    } else if (v_material < 3.5) {
        // Indigo washi with uneven fibres in both directions.
        float warp = step(0.95, grain(floor(p / vec2(1.0, 29.0))));
        float weft = step(0.96, grain(floor(p / vec2(23.0, 1.0))));
        color += vec3(0.011, 0.017, 0.022) * (warp + weft);
        color += (fine - 0.5) * 0.014 + (cloud - 0.5) * 0.018;
        color *= 1.0 - edge * 0.18;
    } else if(v_material < 4.5) {
        // Dark timber used only on the outer Japanese frame.
        float line = sin(p.x * 0.29 + clouds(p / 30.0) * 4.0);
        color += vec3(0.035, 0.021, 0.009) * line;
        color += (fine - 0.5) * 0.018;
    }
    if(v_material > 4.5) {
        vec2 st=gl_FragCoord.xy/u_screen,stepUV=vec2(6.0)/u_screen;
        vec3 blurred=vec3(0.0);
        for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x) {
            float wx=x==0?6.0:(abs(x)==1?4.0:1.0);
            float wy=y==0?6.0:(abs(y)==1?4.0:1.0);
            blurred+=texture(u_backdrop,st+vec2(float(x),float(y))*stepUV).rgb*(wx*wy/256.0);
        }
        color=mix(blurred,v_color.rgb,0.88-edge*0.12);
        color+=vec3(0.020,0.048,0.085)*pow(1.0-uv.y,3.0);
        color+=vec3((fine-0.5)*0.004);
        if(v_material>5.5)color+=vec3(0.025,0.09,0.14)*(0.5+0.5*abs(uv.y*2.0-1.0));
        float radius=min(v_material>5.5?4.0:10.0,min(v_surface.z,v_surface.w)*0.5);
        vec2 q=abs(p-v_surface.zw*0.5)-(v_surface.zw*0.5-radius);
        float distance=length(max(q,0.0))+min(max(q.x,q.y),0.0)-radius;
        float mask=1.0-smoothstep(-0.7,0.7,distance);
        o_color=vec4(clamp(color,0.0,1.0),v_color.a*mask);return;
    }
    o_color = vec4(clamp(color, 0.0, 1.0), v_color.a);
}

)";

} // namespace

PaintColor paint_from_term_color(std::uint8_t color_index, float alpha)
{
    const RgbColor rgb = term_color_to_rgb(color_index);
    return PaintColor{ static_cast<float>(rgb.r) / 255.f, static_cast<float>(rgb.g) / 255.f,
        static_cast<float>(rgb.b) / 255.f, alpha };
}

namespace {
//! VR の間だけ真（`set_panel_transparent`）。**下敷きの有無を決めるのはここ 1 か所。**
bool g_panel_transparent = false;
} // namespace

void set_panel_transparent(bool on)
{
    g_panel_transparent = on;
}

bool panel_transparent()
{
    return g_panel_transparent;
}

PanelStyle overlay_panel_style()
{
    PanelStyle style;
    style.fill = PaintColor{ 0.04f, 0.05f, 0.08f, g_panel_transparent ? 0.f : 0.82f };
    style.border = PaintColor{ 0.42f, 0.48f, 0.58f, 0.80f };
    return style;
}

PanelStyle solid_panel_style()
{
    PanelStyle style;
    style.fill = PaintColor{ 0.06f, 0.07f, 0.10f, g_panel_transparent ? 0.f : 1.f };
    style.border = PaintColor{ 0.30f, 0.34f, 0.42f, 1.f };
    return style;
}

bool UiPaint::init(std::string &err)
{
    this->program_ = compile_program(kVertexSource, kFragmentSource, err);
    if (this->program_ == 0) {
        return false;
    }
    this->loc_screen_ = glGetUniformLocation(this->program_, "u_screen");
    this->loc_backdrop_ = glGetUniformLocation(this->program_, "u_backdrop");

    glGenVertexArrays(1, &this->vao_);
    glGenBuffers(1, &this->vbo_);
    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void *>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void *>(offsetof(Vertex, r)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void *>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<const void *>(offsetof(Vertex, material)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const std::string gl_err = drain_gl_errors();
    if (!gl_err.empty()) {
        err = "UI の四角を描く用意で GL エラー: " + gl_err;
        return false;
    }
    return true;
}

void UiPaint::shutdown()
{
    if(backdrop_){glDeleteTextures(1,&backdrop_);backdrop_=0;backdrop_w_=backdrop_h_=0;}
    if(backdrop_fbo_){glDeleteFramebuffers(1,&backdrop_fbo_);backdrop_fbo_=0;}
    if (this->vbo_ != 0) {
        glDeleteBuffers(1, &this->vbo_);
        this->vbo_ = 0;
    }
    if (this->vao_ != 0) {
        glDeleteVertexArrays(1, &this->vao_);
        this->vao_ = 0;
    }
    if (this->program_ != 0) {
        glDeleteProgram(this->program_);
        this->program_ = 0;
    }
}

void UiPaint::begin(int screen_w, int screen_h)
{
    layout_scale_=1.f;
    this->screen_w_ = (screen_w > 0) ? screen_w : 1;
    this->screen_h_ = (screen_h > 0) ? screen_h : 1;
    this->vertices_.clear();
    uses_glass_=false;
}

void UiPaint::push_rect(float x0, float y0, float x1, float y1, const PaintColor &color)
{
    if ((x1 <= x0) || (y1 <= y0) || (color.a <= 0.f)) {
        return;
    }
    x0*=layout_scale_;y0*=layout_scale_;x1*=layout_scale_;y1*=layout_scale_;
    const Vertex a{ x0, y0, color.r, color.g, color.b, color.a };
    const Vertex b{ x1, y0, color.r, color.g, color.b, color.a };
    const Vertex c{ x1, y1, color.r, color.g, color.b, color.a };
    const Vertex d{ x0, y1, color.r, color.g, color.b, color.a };
    this->vertices_.push_back(a);
    this->vertices_.push_back(b);
    this->vertices_.push_back(c);
    this->vertices_.push_back(a);
    this->vertices_.push_back(c);
    this->vertices_.push_back(d);
}

void UiPaint::triangle(float x0, float y0, float x1, float y1, float x2, float y2, const PaintColor &color)
{
    if (color.a <= 0.f) {
        return;
    }
    x0*=layout_scale_;y0*=layout_scale_;x1*=layout_scale_;y1*=layout_scale_;x2*=layout_scale_;y2*=layout_scale_;
    this->vertices_.push_back(Vertex{ x0, y0, color.r, color.g, color.b, color.a });
    this->vertices_.push_back(Vertex{ x1, y1, color.r, color.g, color.b, color.a });
    this->vertices_.push_back(Vertex{ x2, y2, color.r, color.g, color.b, color.a });
}

void UiPaint::rect(const RectPx &r, const PaintColor &color)
{
    this->push_rect(static_cast<float>(r.x), static_cast<float>(r.y),
        static_cast<float>(r.x + r.w), static_cast<float>(r.y + r.h), color);
}

void UiPaint::rect_f(float x0, float y0, float x1, float y1, const PaintColor &color)
{
    if ((x1 <= x0) || (y1 <= y0)) {
        return;
    }
    this->push_rect(x0, y0, x1, y1, color);
}

void UiPaint::surface(const RectPx &r, const PaintColor &color, UiMaterial material)
{
    if(r.empty()||color.a<=0.f)return;
    if(material==UiMaterial::Flat){rect(r,color);return;}
    if(material==UiMaterial::Glass||material==UiMaterial::GlassHighlight)uses_glass_=true;
    const float x=r.x*layout_scale_,y=r.y*layout_scale_,w=r.w*layout_scale_,h=r.h*layout_scale_;
    const float m=static_cast<float>(material);
    const Vertex a{x,y,color.r,color.g,color.b,color.a,0,0,w,h,m};
    const Vertex b{x+w,y,color.r,color.g,color.b,color.a,w,0,w,h,m};
    const Vertex c{x+w,y+h,color.r,color.g,color.b,color.a,w,h,w,h,m};
    const Vertex d{x,y+h,color.r,color.g,color.b,color.a,0,h,w,h,m};
    vertices_.insert(vertices_.end(),{a,b,c,a,c,d});
}

void UiPaint::frame(const RectPx &r, const PaintColor &color, int thickness)
{
    if (r.empty() || (thickness <= 0)) {
        return;
    }
    const auto t = static_cast<float>(std::min(thickness, std::min(r.w, r.h)));
    const auto x0 = static_cast<float>(r.x);
    const auto y0 = static_cast<float>(r.y);
    const auto x1 = static_cast<float>(r.x + r.w);
    const auto y1 = static_cast<float>(r.y + r.h);
    this->push_rect(x0, y0, x1, y0 + t, color); // 上
    this->push_rect(x0, y1 - t, x1, y1, color); // 下
    this->push_rect(x0, y0 + t, x0 + t, y1 - t, color); // 左
    this->push_rect(x1 - t, y0 + t, x1, y1 - t, color); // 右
}

void UiPaint::rounded_frame(const RectPx &r,const PaintColor &color,int radius,int thickness)
{
    if(r.empty()||thickness<=0)return;
    if(r.w<2||r.h<2){frame(r,color,1);return;}
    const float outer=static_cast<float>(std::clamp(radius,1,std::max(1,std::min(r.w,r.h)/2)));
    const float t=static_cast<float>(std::min(thickness,static_cast<int>(outer))),inner=outer-t;
    push_rect(r.x+outer,r.y,r.x+r.w-outer,r.y+t,color);
    push_rect(r.x+outer,r.y+r.h-t,r.x+r.w-outer,r.y+r.h,color);
    push_rect(r.x,r.y+outer,r.x+t,r.y+r.h-outer,color);
    push_rect(r.x+r.w-t,r.y+outer,r.x+r.w,r.y+r.h-outer,color);
    const float cx[]={r.x+r.w-outer,r.x+outer,r.x+outer,r.x+r.w-outer};
    const float cy[]={r.y+r.h-outer,r.y+r.h-outer,r.y+outer,r.y+outer};
    constexpr float half_pi=1.57079632679f;
    for(int corner=0;corner<4;++corner)for(int n=0;n<8;++n) {
        const float a=(corner+n/8.f)*half_pi,b=(corner+(n+1)/8.f)*half_pi;
        const float ox=cx[corner]+std::cos(a)*outer,oy=cy[corner]+std::sin(a)*outer;
        const float px=cx[corner]+std::cos(b)*outer,py=cy[corner]+std::sin(b)*outer;
        const float ix=cx[corner]+std::cos(a)*inner,iy=cy[corner]+std::sin(a)*inner;
        const float jx=cx[corner]+std::cos(b)*inner,jy=cy[corner]+std::sin(b)*inner;
        triangle(ox,oy,px,py,ix,iy,color);triangle(px,py,jx,jy,ix,iy,color);
    }
}

void UiPaint::panel(const RectPx &r, const PanelStyle &style)
{
    if (r.empty()) {
        return;
    }
    this->rect(r, style.fill);
    this->frame(r, style.border, style.border_px);
}

void UiPaint::gauge(const RectPx &r, float ratio, const PaintColor &fill, const PaintColor &back)
{
    if (r.empty()) {
        return;
    }
    this->rect(r, back);
    const float clamped = std::clamp(ratio, 0.f, 1.f);
    RectPx filled = r;
    filled.w = static_cast<int>(static_cast<float>(r.w) * clamped);
    this->rect(filled, fill);
}

void UiPaint::flush()
{
    if (this->vertices_.empty() || (this->program_ == 0)) {
        return;
    }

    /*
     * **自分で viewport を張る。**呼び出し側の状態に頼ってはいけない。
     * 直前に走るのはポスト処理で、あれは 3D の矩形へ出すために `glViewport` を
     * その矩形に設定して戻さない。頼っていた頃は `Split`（3D が窓の一部）のとき
     * **UI が丸ごと 3D の矩形の中へ押し込まれ、画面の右と下に空白が残った**。
     * `begin()` で受け取った実寸と同じものを張るので、頂点シェーダの割り算とも必ず一致する。
     */
    if (!ui_break_enabled("viewport")) {
        glViewport(0, 0, this->screen_w_, this->screen_h_);
    } //!< `HD2D_BREAK_UI=viewport`。**検査が FAIL になるのが正しい。**
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    if(uses_glass_) {
        glActiveTexture(GL_TEXTURE0);
        if(!backdrop_)glGenTextures(1,&backdrop_);
        glBindTexture(GL_TEXTURE_2D,backdrop_);
        const int bw=std::max(1,screen_w_/4),bh=std::max(1,screen_h_/4);
        if(backdrop_w_!=bw||backdrop_h_!=bh) {
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,bw,bh,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            backdrop_w_=bw;backdrop_h_=bh;
        }
        // Copy before drawing the UI to avoid sampling our own glass or text.
        GLint read_fbo=0,draw_fbo=0;glGetIntegerv(0x8CAA,&read_fbo);glGetIntegerv(0x8CA6,&draw_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(draw_fbo));
        if(!backdrop_fbo_)glGenFramebuffers(1,&backdrop_fbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,backdrop_fbo_);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,backdrop_,0);
        glBlitFramebuffer(0,0,screen_w_,screen_h_,0,0,bw,bh,GL_COLOR_BUFFER_BIT,GL_LINEAR);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(draw_fbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(read_fbo));
    }
    glUseProgram(this->program_);
    glUniform1i(loc_backdrop_,0);
    glUniform2f(this->loc_screen_, static_cast<float>(this->screen_w_), static_cast<float>(this->screen_h_));

    glBindVertexArray(this->vao_);
    glBindBuffer(GL_ARRAY_BUFFER, this->vbo_);
    const std::size_t bytes = this->vertices_.size() * sizeof(Vertex);
    if (bytes > this->vbo_capacity_) {
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(bytes), this->vertices_.data(), GL_STREAM_DRAW);
        this->vbo_capacity_ = bytes;
    } else {
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(bytes), this->vertices_.data());
    }
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(this->vertices_.size()));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    if(uses_glass_){glBindTexture(GL_TEXTURE_2D,0);uses_glass_=false;}
    this->vertices_.clear();
}

} // namespace hd2d
