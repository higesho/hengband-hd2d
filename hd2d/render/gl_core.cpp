/*!
 * @file gl_core.cpp
 * @brief `gl_core.h` の実体と読み込み。
 */
#include "render/gl_core.h"

#include <SDL2/SDL.h>

#if defined(HENGBAND_GLES)
#include <algorithm>
#include <cstdio>
#include <dlfcn.h>
#endif

namespace hd2d::gl {

#define HD2D_GL_DEFINE_PTR(ret, name, args) PFN_gl##name gl##name = nullptr;
HD2D_GL_FUNCTIONS(HD2D_GL_DEFINE_PTR)
#undef HD2D_GL_DEFINE_PTR

namespace {

/*!
 * @brief 1 本引く。取れなければ `missing` へ名前を積む。
 * @note `SDL_GL_GetProcAddress` は Windows では `wglGetProcAddress` を先に試し、
 * 取れなければ `opengl32.dll` の `GetProcAddress` へ落ちる。1.1 の関数（`glClear` 等）は
 * 後者でしか取れないので、この順序に依存している。
 */
void load_one(void **slot, const char *name, std::vector<std::string> &missing)
{
    void *address = SDL_GL_GetProcAddress(name);
#if defined(HENGBAND_GLES)
    if (address == nullptr) {
        // EGL 1.4 の eglGetProcAddress はコア関数へ nullptr を返しうる。
        // GLESv3 はリンク時依存なので、プロセスの表から直接引けば必ず居る。
        address = ::dlsym(RTLD_DEFAULT, name);
    }
#endif
    *slot = address;
    if (address == nullptr) {
        missing.emplace_back(name);
    }
}

//! 取れなくても致命的でない関数（GL 4.3 のデバッグ出力）。
bool is_optional(const std::string &name)
{
    return (name == "glDebugMessageCallback") || (name == "glDebugMessageControl");
}

#if defined(HENGBAND_GLES)
/*!
 * @name glDrawBuffer（単数）の穴埋め
 * @details ES に単数形は無い（複数形 `glDrawBuffers` だけ）。使い手は
 * `shadow_map.cpp` の `glDrawBuffer(GL_NONE)`（深度だけの FBO 宣言）1 か所なので、
 * 同義の複数形へ流すシムを立てる。
 * @{ */
using PFN_glDrawBuffers = void(HD2D_GLAPI *)(GLsizei n, const GLenum *bufs);
PFN_glDrawBuffers s_draw_buffers = nullptr;

void HD2D_GLAPI draw_buffer_shim(GLenum buf)
{
    if (s_draw_buffers != nullptr) {
        s_draw_buffers(1, &buf);
    }
}
/*! @} */
#endif

} // namespace

bool load_gl_functions(std::vector<std::string> &missing)
{
    missing.clear();

#define HD2D_GL_LOAD_PTR(ret, name, args) load_one(reinterpret_cast<void **>(&gl##name), "gl" #name, missing);
    HD2D_GL_FUNCTIONS(HD2D_GL_LOAD_PTR)
#undef HD2D_GL_LOAD_PTR

#if defined(HENGBAND_GLES)
    if (glDrawBuffer == nullptr) {
        s_draw_buffers = reinterpret_cast<PFN_glDrawBuffers>(SDL_GL_GetProcAddress("glDrawBuffers"));
        if (s_draw_buffers == nullptr) {
            s_draw_buffers = reinterpret_cast<PFN_glDrawBuffers>(::dlsym(RTLD_DEFAULT, "glDrawBuffers"));
        }
        if (s_draw_buffers != nullptr) {
            glDrawBuffer = &draw_buffer_shim;
            missing.erase(std::remove(missing.begin(), missing.end(), std::string("glDrawBuffer")), missing.end());
        }
    }
#endif

    for (const auto &name : missing) {
        if (!is_optional(name)) {
            return false;
        }
    }
    return true;
}

std::string gl_string(GLenum name)
{
    if (glGetString == nullptr) {
        return std::string();
    }
    const GLubyte *const text = glGetString(name);
    if (text == nullptr) {
        return std::string();
    }
    return std::string(reinterpret_cast<const char *>(text));
}

void probe_gl_errors(const char *stage)
{
    //! 段 × フレームぶんの持ち分。**エラーが出たときだけ減る**（無事なら無音のまま）。
    static int budget = 32;
    if (budget <= 0) {
        return;
    }
    const std::string errors = drain_gl_errors();
    if (errors.empty()) {
        return;
    }
    --budget;
    std::fprintf(stderr, "[hd2d] GL エラー @ %s: %s%s\n", (stage != nullptr) ? stage : "(不明)",
        errors.c_str(), (budget == 0) ? "（以降は報せない）" : "");
    std::fflush(stderr);
}

std::string drain_gl_errors()
{
    if (glGetError == nullptr) {
        return std::string();
    }
    std::string out;
    for (int guard = 0; guard < 32; ++guard) { //!< 壊れたドライバで無限に回らないための番人
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) {
            break;
        }
        if (!out.empty()) {
            out += ", ";
        }
        switch (error) {
        case GL_INVALID_ENUM:
            out += "GL_INVALID_ENUM";
            break;
        case GL_INVALID_VALUE:
            out += "GL_INVALID_VALUE";
            break;
        case GL_INVALID_OPERATION:
            out += "GL_INVALID_OPERATION";
            break;
        case GL_OUT_OF_MEMORY:
            out += "GL_OUT_OF_MEMORY";
            break;
        default:
            out += "0x" + std::to_string(static_cast<unsigned>(error));
            break;
        }
    }
    return out;
}

} // namespace hd2d::gl
