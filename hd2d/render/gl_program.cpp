/*!
 * @file gl_program.cpp
 * @brief `gl_program.h` の実装。
 */
#include "render/gl_program.h"

#include <string>
#include <vector>

namespace hd2d {

using namespace hd2d::gl;

namespace {

#if defined(HENGBAND_GLES)
/*!
 * @brief いま張られているコンテキストの版から `#version` 行を選ぶ。
 * @details 実機は大抵 3.2 だが、エミュレータは 3.1 止まり（コンテキストは
 * `create_window` が 3.2 → 3.1 → 3.0 と下げながら作る）。**取れた版より高い
 * `#version` を書くとその場で全シェーダがコンパイルエラーになる**ので、ここで合わせる。
 * @note このシェーダ群が使う機能（インスタンス描画・sampler2DShadow・sampler3D・
 * usampler2D・textureLod）は GLSL ES 3.00 で全部そろっている。
 */
const char *es_version_line()
{
    static const std::string cached = []() {
        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if ((major > 3) || ((major == 3) && (minor >= 2))) {
            return std::string("#version 320 es");
        }
        if ((major == 3) && (minor == 1)) {
            return std::string("#version 310 es");
        }
        return std::string("#version 300 es");
    }();
    return cached.c_str();
}

/*!
 * @brief `#version 460 core` を GLSL ES の前置きへ読み替える（Android。
 * ）。
 * @details 全シェーダがこの TU（`compile_program`）を通るので、読み替えはここ 1 か所で済む。
 * ES は既定精度を持たない型（float / sampler3D / sampler2DShadow / usampler2D）が
 * あるので、版数の次で一括宣言する。
 * @note 前置きが 1 行 → 7 行に増えるぶん、info log の行番号は **+6** ずれる。
 */
std::string translate_to_es(const char *source)
{
    static constexpr std::string_view kDesktopVersion = "#version 460 core";
    std::string out(source);
    const auto pos = out.find(kDesktopVersion);
    if (pos == 0) {
        std::string preamble(es_version_line());
        preamble += "\n"
                    "precision highp float;\n"
                    "precision highp int;\n"
                    "precision highp sampler2D;\n"
                    "precision highp sampler3D;\n"
                    "precision highp sampler2DShadow;\n"
                    "precision highp usampler2D;";
        out.replace(pos, kDesktopVersion.size(), preamble);
    }
    return out;
}
#endif

std::string shader_info_log(GLuint shader)
{
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return std::string();
    }
    std::vector<char> buf(static_cast<std::size_t>(length));
    glGetShaderInfoLog(shader, length, nullptr, buf.data());
    return std::string(buf.data());
}

std::string program_info_log(GLuint program)
{
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 1) {
        return std::string();
    }
    std::vector<char> buf(static_cast<std::size_t>(length));
    glGetProgramInfoLog(program, length, nullptr, buf.data());
    return std::string(buf.data());
}

GLuint compile_one(GLenum type, const char *source, std::string &err)
{
    const GLuint shader = glCreateShader(type);
    if (shader == 0) {
        err = "glCreateShader failed";
        return 0;
    }
#if defined(HENGBAND_GLES)
    const std::string translated = translate_to_es(source);
    source = translated.c_str();
#endif
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == static_cast<GLint>(GL_TRUE)) {
        return shader;
    }
    err = ((type == GL_VERTEX_SHADER) ? "vertex shader: " : "fragment shader: ") + shader_info_log(shader);
    glDeleteShader(shader);
    return 0;
}

} // namespace

GLuint compile_program(const char *vertex_source, const char *fragment_source, std::string &err)
{
    const GLuint vs = compile_one(GL_VERTEX_SHADER, vertex_source, err);
    if (vs == 0) {
        return 0;
    }
    const GLuint fs = compile_one(GL_FRAGMENT_SHADER, fragment_source, err);
    if (fs == 0) {
        glDeleteShader(vs);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs); // program がリンク済みなので、ここで手放してよい
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != static_cast<GLint>(GL_TRUE)) {
        err = "link: " + program_info_log(program);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

} // namespace hd2d
