/*!
 * @file gl_core.h
 * @brief OpenGL 4.6 core の型・定数・関数ポインタ（自前ローダ）。
 *
 * 基準は （OpenGL 4.6 core）。
 *
 * ## なぜ自前なのか
 * は GLAD（生成済みの 1 ファイル）を挙げていた。狙いは
 * **「外部依存にならないこと」**で、`opengl32.lib` が 1.1 の API しか出さない以上、
 * 関数ポインタを自分で引く仕掛けが要る、という一点にある。ここではその仕掛けを
 * 直接書いた。生成器を回さずに済み、増やしたい関数は下の一覧に 1 行足すだけになる。
 * 引き換えに、使う関数は**明示して並べる必要がある**（GLAD の「全部入り」ではない）。
 *
 * ## 約束
 * - **GL のヘッダを 1 つも include しない。**型も定数も全部ここで定義する
 *   （`<GL/gl.h>` を混ぜると 1.1 の宣言と衝突するし、Windows のそれは 1.1 で止まっている）
 * - 名前は本家のまま（`glClear` 等）だが **`hd2d::gl` 名前空間の中**に置く。
 *   使う側は `using namespace hd2d::gl;` と書けば普通の GL コードに見える
 * - 取得は `SDL_GL_GetProcAddress`（Windows では wgl → opengl32.dll の順で SDL が面倒を見る）
 *
 * @note ここに無い関数を使いたくなったら `HD2D_GL_FUNCTIONS` に 1 行足すこと。
 * 綴りを間違えれば `load_gl_functions` が「取れなかった名前」として返すので、
 * 黙って nullptr を呼ぶことにはならない。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hd2d::gl {

/* ============================================================================ 型 */

#if defined(_WIN32)
#define HD2D_GLAPI __stdcall
#else
#define HD2D_GLAPI
#endif

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLbyte = signed char;
using GLshort = short;
using GLint = int;
using GLsizei = int;
using GLubyte = unsigned char;
using GLushort = unsigned short;
using GLuint = unsigned int;
using GLfloat = float;
using GLclampf = float;
using GLdouble = double;
using GLchar = char;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;
using GLuint64 = std::uint64_t;

//! `glDebugMessageCallback` に渡す受け口（GL 4.3）。
using GLDEBUGPROC = void(HD2D_GLAPI *)(GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const GLchar *message, const void *user_param);

/* ========================================================================== 定数 */

inline constexpr GLenum GL_FALSE = 0;
inline constexpr GLenum GL_TRUE = 1;

inline constexpr GLenum GL_DEPTH_BUFFER_BIT = 0x00000100;
inline constexpr GLenum GL_STENCIL_BUFFER_BIT = 0x00000400;
inline constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;

inline constexpr GLenum GL_POINTS = 0x0000;
inline constexpr GLenum GL_LINES = 0x0001;
inline constexpr GLenum GL_TRIANGLES = 0x0004;
inline constexpr GLenum GL_TRIANGLE_STRIP = 0x0005;

inline constexpr GLenum GL_NEVER = 0x0200;
inline constexpr GLenum GL_LESS = 0x0201;
inline constexpr GLenum GL_LEQUAL = 0x0203;

inline constexpr GLenum GL_SRC_ALPHA = 0x0302;
inline constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr GLenum GL_ONE = 1;
inline constexpr GLenum GL_ZERO = 0;

inline constexpr GLenum GL_CULL_FACE = 0x0B44;
inline constexpr GLenum GL_CW = 0x0900;
inline constexpr GLenum GL_CCW = 0x0901;
inline constexpr GLenum GL_FRONT = 0x0404;
inline constexpr GLenum GL_BACK = 0x0405;
inline constexpr GLenum GL_NONE = 0;
inline constexpr GLenum GL_DEPTH_TEST = 0x0B71;
inline constexpr GLenum GL_BLEND = 0x0BE2;
/*!
 * @brief 頂点シェーダが `gl_PointSize` を決めてよい（デスクトップ GL のみ）。
 * @details GLSL ES は**常に有効**なので、`HENGBAND_GLES` では立てない
 * （立てると `glEnable` が `GL_INVALID_ENUM` を出す）。埃（`render/dust_motes.h`）が使う。
 */
inline constexpr GLenum GL_PROGRAM_POINT_SIZE = 0x8642;
inline constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
inline constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
inline constexpr GLenum GL_MULTISAMPLE = 0x809D;
inline constexpr GLenum GL_FRAMEBUFFER_SRGB = 0x8DB9;

inline constexpr GLenum GL_BYTE = 0x1400;
inline constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
inline constexpr GLenum GL_SHORT = 0x1402;
inline constexpr GLenum GL_UNSIGNED_SHORT = 0x1403;
inline constexpr GLenum GL_INT = 0x1404;
inline constexpr GLenum GL_UNSIGNED_INT = 0x1405;
inline constexpr GLenum GL_FLOAT = 0x1406;

inline constexpr GLenum GL_RED = 0x1903;
inline constexpr GLenum GL_RG = 0x8227;
inline constexpr GLenum GL_RG8 = 0x822B;
//! 整数テクスチャ（`usampler2D`）。**索引を丸めずに読める**ので面アトラスに使う。
inline constexpr GLenum GL_RG8UI = 0x8238;
inline constexpr GLenum GL_RG_INTEGER = 0x8228;
inline constexpr GLenum GL_RGB = 0x1907;
inline constexpr GLenum GL_RGBA = 0x1908;
inline constexpr GLenum GL_R8 = 0x8229;
inline constexpr GLenum GL_RGB8 = 0x8051; //!< VR の部屋の柄（`hd2d/xr/xr_room.cpp` が焼く）
inline constexpr GLenum GL_RGBA8 = 0x8058;
/*!
 * @name 4 成分の整数テクスチャ
 * @details R に文字コード・GBA に前景色を入れて `usampler2D` で読む。8bit の正規化
 * （`GL_RGBA8`）だと文字コードが 0〜1 に潰れて丸めが要るので、整数形式で持つ。
 * @{
 */
inline constexpr GLenum GL_RGBA8UI = 0x8D7C;
inline constexpr GLenum GL_RGBA_INTEGER = 0x8D99;
/*! @} */
inline constexpr GLenum GL_SRGB8_ALPHA8 = 0x8C43;

/*! @name ポスト処理（P7）
 * @details **画面の色は 16bit 浮動小数で持つ**（`GL_RGBA16F`）。ブルームの閾値抽出は
 * トーンマップ**前**の値を相手にする（設計書 §13）ので、1.0 で頭打ちになる 8bit では
 * 「白く飛んでいる所」と「本当に明るい所」を区別できない。
 *
 * 3D テクスチャはカラーグレーディングの LUT（32³）に使う。**`GL_TEXTURE_WRAP_R` を
 * 忘れると青の端が巻き込む**（R は 3 つ目の軸で、S/T と別に指定しなければならない）。
 * @{ */
inline constexpr GLenum GL_RGBA16F = 0x881A;
inline constexpr GLenum GL_RGB16F = 0x881B;
inline constexpr GLenum GL_HALF_FLOAT = 0x140B;
inline constexpr GLenum GL_COLOR_ATTACHMENT0 = 0x8CE0;
inline constexpr GLenum GL_TEXTURE_3D = 0x806F;
inline constexpr GLenum GL_TEXTURE_WRAP_R = 0x8072;
/*! @} */

/*! @name シャドウマップ（P5 ①）
 * @details 深度だけのテクスチャを `sampler2DShadow` で読む。**比較モードを立てておくと**
 * `texture()` が「その深度より手前か」の 0/1 を返し、周りの 4 点をハードウェアが混ぜてくれる
 * （＝タダで縁がなめらかになる）。立て忘れると生の深度が返るので、影が全部 0 か 1 になる。
 * @{ */
inline constexpr GLenum GL_DEPTH_COMPONENT = 0x1902;
inline constexpr GLenum GL_DEPTH_COMPONENT24 = 0x81A6;
inline constexpr GLenum GL_TEXTURE_COMPARE_MODE = 0x884C;
inline constexpr GLenum GL_TEXTURE_COMPARE_FUNC = 0x884D;
inline constexpr GLenum GL_COMPARE_REF_TO_TEXTURE = 0x884E;
inline constexpr GLenum GL_FRAMEBUFFER = 0x8D40;
inline constexpr GLenum GL_DEPTH_ATTACHMENT = 0x8D00;
inline constexpr GLenum GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
/*! @} */

/*! @name VR（`hd2d/xr/`）
 * @details ランタイムが寄越した swapchain の画像へ描き、鏡窓へ写すのに要る。
 * 深度は自前のレンダバッファ（ランタイムからは色しか来ない）。
 * @{ */
inline constexpr GLenum GL_READ_FRAMEBUFFER = 0x8CA8;
inline constexpr GLenum GL_DRAW_FRAMEBUFFER = 0x8CA9;
inline constexpr GLenum GL_RENDERBUFFER = 0x8D41;
inline constexpr GLenum GL_DEPTH_STENCIL_ATTACHMENT = 0x821A;
inline constexpr GLenum GL_DEPTH24_STENCIL8 = 0x88F0;
//! @note sRGB の自動変換（`GL_FRAMEBUFFER_SRGB`）は上に既にある。VR では切ったまま使う。
/*! @} */

inline constexpr GLenum GL_NEAREST = 0x2600;
inline constexpr GLenum GL_LINEAR = 0x2601;
inline constexpr GLenum GL_LINEAR_MIPMAP_LINEAR = 0x2703; //!< 被写界深度のピラミッド（P10 レビュー 9）
inline constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
inline constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
inline constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
inline constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
inline constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
inline constexpr GLenum GL_REPEAT = 0x2901; //!< 天球の星（経度が回り込む。`render/sky_dome.cpp`）
inline constexpr GLenum GL_TEXTURE_BASE_LEVEL = 0x813C; //!< 同上（段の範囲を宣言しないとミップ不完全）
inline constexpr GLenum GL_TEXTURE_MAX_LEVEL = 0x813D;
inline constexpr GLenum GL_TEXTURE0 = 0x84C0;
//! 天球は 3 枚（星・太陽・月）を同時に結ぶ（`render/sky_dome.cpp`）。
inline constexpr GLenum GL_TEXTURE1 = 0x84C1;
inline constexpr GLenum GL_TEXTURE2 = 0x84C2;

inline constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
inline constexpr GLenum GL_STREAM_DRAW = 0x88E0;
inline constexpr GLenum GL_STATIC_DRAW = 0x88E4;
inline constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;

inline constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
inline constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
inline constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
inline constexpr GLenum GL_LINK_STATUS = 0x8B82;
inline constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;

inline constexpr GLenum GL_VENDOR = 0x1F00;
inline constexpr GLenum GL_RENDERER = 0x1F01;
inline constexpr GLenum GL_VERSION = 0x1F02;
inline constexpr GLenum GL_SHADING_LANGUAGE_VERSION = 0x8B8C;
inline constexpr GLenum GL_MAJOR_VERSION = 0x821B;
inline constexpr GLenum GL_MINOR_VERSION = 0x821C;
inline constexpr GLenum GL_MAX_TEXTURE_SIZE = 0x0D33;
//! いまの viewport（`glGetIntegerv` で 4 個。P8 の「描く側が自分で張る」を検査するのに要る）。
inline constexpr GLenum GL_VIEWPORT = 0x0BA2;

inline constexpr GLenum GL_NO_ERROR = 0;
inline constexpr GLenum GL_INVALID_ENUM = 0x0500;
inline constexpr GLenum GL_INVALID_VALUE = 0x0501;
inline constexpr GLenum GL_INVALID_OPERATION = 0x0502;
inline constexpr GLenum GL_OUT_OF_MEMORY = 0x0505;

//! デバッグ出力（GL 4.3）。**同期にすると壊れた呼び出しの場所がそのまま取れる。**
inline constexpr GLenum GL_DEBUG_OUTPUT = 0x92E0;
inline constexpr GLenum GL_DEBUG_OUTPUT_SYNCHRONOUS = 0x8242;
inline constexpr GLenum GL_DEBUG_SEVERITY_HIGH = 0x9146;
inline constexpr GLenum GL_DEBUG_SEVERITY_MEDIUM = 0x9147;
inline constexpr GLenum GL_DEBUG_SEVERITY_LOW = 0x9148;
inline constexpr GLenum GL_DEBUG_SEVERITY_NOTIFICATION = 0x826B;
inline constexpr GLenum GL_DONT_CARE = 0x1100;

/* ================================================================== 関数の一覧 */

/*!
 * @brief 使う GL 関数の全部。`X(戻り値, 名前（gl を除く）, 引数リスト)`。
 * @details ここに載っているものだけが引かれる。足すときはこの表に 1 行足すだけでよい
 * （宣言・実体・読み込みの 3 か所が自動で揃う）。
 */
#define HD2D_GL_FUNCTIONS(X)                                                                                                        \
    /* --- 状態と問い合わせ（1.x 相当だが opengl32.lib を使わないので同じ経路で引く） --- */                                        \
    X(const GLubyte *, GetString, (GLenum name))                                                                                    \
    X(void, GetIntegerv, (GLenum pname, GLint * data))                                                                              \
    X(GLenum, GetError, ())                                                                                                         \
    X(void, Viewport, (GLint x, GLint y, GLsizei width, GLsizei height))                                                            \
    X(void, ClearColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha))                                                  \
    X(void, Clear, (GLbitfield mask))                                                                                               \
    X(void, Enable, (GLenum cap))                                                                                                   \
    X(void, Disable, (GLenum cap))                                                                                                  \
    X(void, BlendFunc, (GLenum sfactor, GLenum dfactor))                                                                            \
    X(void, FrontFace, (GLenum mode))                                                                                               \
    X(void, CullFace, (GLenum mode))                                                                                               \
    X(void, DepthFunc, (GLenum func))                                                                                               \
    X(void, PixelStorei, (GLenum pname, GLint param))                                                                               \
    X(void, DepthMask, (GLboolean flag))                                                                                            \
    X(void, Finish, ())                                                                                                            \
    X(void, ReadPixels, (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels))                \
    /* --- テクスチャ --- */                                                                                                        \
    X(void, GenTextures, (GLsizei n, GLuint * textures))                                                                            \
    X(void, DeleteTextures, (GLsizei n, const GLuint *textures))                                                                    \
    X(void, BindTexture, (GLenum target, GLuint texture))                                                                           \
    X(void, ActiveTexture, (GLenum texture))                                                                                        \
    X(void, TexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)) \
    X(void, TexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)) \
    X(void, TexParameteri, (GLenum target, GLenum pname, GLint param))                                                              \
    /* --- 3D テクスチャ（カラーグレーディングの LUT。P7） --- */                                                                   \
    X(void, TexImage3D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels)) \
    /* --- フレームバッファ（シャドウマップ。P5 ①） --- */                                                                         \
    X(void, GenFramebuffers, (GLsizei n, GLuint * framebuffers))                                                                    \
    X(void, DeleteFramebuffers, (GLsizei n, const GLuint *framebuffers))                                                            \
    X(void, BindFramebuffer, (GLenum target, GLuint framebuffer))                                                                   \
    X(void, FramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level))                \
    X(GLenum, CheckFramebufferStatus, (GLenum target))                                                                              \
    X(void, DrawBuffer, (GLenum buf))                                                                                               \
    X(void, ReadBuffer, (GLenum src))                                                                                               \
    /* --- レンダバッファと転送（VR。`hd2d/xr/xr_session.cpp`。GLES 3.0 にもある） --- */                                           \
    X(void, GenRenderbuffers, (GLsizei n, GLuint * renderbuffers))                                                                  \
    X(void, DeleteRenderbuffers, (GLsizei n, const GLuint *renderbuffers))                                                          \
    X(void, BindRenderbuffer, (GLenum target, GLuint renderbuffer))                                                                 \
    X(void, RenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height))                             \
    X(void, FramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer))            \
    X(void, BlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)) \
    /* --- バッファと頂点配列 --- */                                                                                                \
    X(void, GenBuffers, (GLsizei n, GLuint * buffers))                                                                              \
    X(void, DeleteBuffers, (GLsizei n, const GLuint *buffers))                                                                      \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                                                             \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage))                                           \
    X(void, BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data))                                     \
    X(void, GenVertexArrays, (GLsizei n, GLuint * arrays))                                                                          \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint *arrays))                                                                  \
    X(void, BindVertexArray, (GLuint array))                                                                                        \
    X(void, EnableVertexAttribArray, (GLuint index))                                                                                \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)) \
    X(void, VertexAttribDivisor, (GLuint index, GLuint divisor))                                                                    \
    X(void, DisableVertexAttribArray, (GLuint index))                                                                               \
    X(void, VertexAttrib3f, (GLuint index, GLfloat v0, GLfloat v1, GLfloat v2))                                                     \
    X(void, DrawArrays, (GLenum mode, GLint first, GLsizei count))                                                                  \
    X(void, DrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei instancecount))                                  \
    X(void, DrawElements, (GLenum mode, GLsizei count, GLenum type, const void *indices))                                           \
    X(void, DrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount))           \
    /* --- シェーダ --- */                                                                                                          \
    X(GLuint, CreateShader, (GLenum type))                                                                                          \
    X(void, DeleteShader, (GLuint shader))                                                                                          \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length))                         \
    X(void, CompileShader, (GLuint shader))                                                                                         \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint * params))                                                             \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog))                                   \
    X(GLuint, CreateProgram, ())                                                                                                    \
    X(void, DeleteProgram, (GLuint program))                                                                                        \
    X(void, AttachShader, (GLuint program, GLuint shader))                                                                          \
    X(void, LinkProgram, (GLuint program))                                                                                          \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint * params))                                                           \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog))                                 \
    X(void, UseProgram, (GLuint program))                                                                                           \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar *name))                                                              \
    X(void, Uniform1i, (GLint location, GLint v0))                                                                                  \
    X(void, Uniform2i, (GLint location, GLint v0, GLint v1))                                                                        \
    X(void, Uniform1f, (GLint location, GLfloat v0))                                                                                \
    X(void, Uniform2f, (GLint location, GLfloat v0, GLfloat v1))                                                                    \
    X(void, Uniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2))                                                        \
    X(void, Uniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))                                            \
    X(void, Uniform1fv, (GLint location, GLsizei count, const GLfloat *value))                                                      \
    X(void, Uniform2fv, (GLint location, GLsizei count, const GLfloat *value))                                                      \
    X(void, Uniform3fv, (GLint location, GLsizei count, const GLfloat *value))                                                      \
    X(void, Uniform4fv, (GLint location, GLsizei count, const GLfloat *value))                                                      \
    X(void, UniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))                           \
    /* --- デバッグ出力（4.3。無い環境では取れないので任意扱い。§4 を読むこと） --- */                                              \
    X(void, DebugMessageCallback, (GLDEBUGPROC callback, const void *userParam))                                                    \
    X(void, DebugMessageControl, (GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled))

#define HD2D_GL_DECLARE_PTR(ret, name, args)   \
    using PFN_gl##name = ret(HD2D_GLAPI *) args; \
    extern PFN_gl##name gl##name;

HD2D_GL_FUNCTIONS(HD2D_GL_DECLARE_PTR)

#undef HD2D_GL_DECLARE_PTR

/* ================================================================== 読み込み */

/*!
 * @brief 上の表の関数を `SDL_GL_GetProcAddress` で引く。**GL コンテキストを作った後に呼ぶこと。**
 * @param[out] missing 取れなかった関数の名前（呼び出し前の中身は消される）。
 * @return 必須のものが全部取れたら true。
 * @details `DebugMessage*` の 2 つだけは**取れなくてもよい**（GL 4.3 未満のドライバ・
 * デバッグコンテキストでない場合に落ちる）。`missing` には載るが戻り値は true のままなので、
 * 呼び出し側は「載っていた＝その機能は使えない」とだけ受け取ればよい。
 * @note nullptr の関数を呼ぶのは即死なので、**false のときは絶対に描き始めないこと。**
 */
bool load_gl_functions(std::vector<std::string> &missing);

//! `glGetString` の結果を `std::string` で（nullptr を空文字列に潰す）。
std::string gl_string(GLenum name);

/*!
 * @brief `glGetError` を吸って、溜まっていたものを文字列で返す。
 * @return 空文字列ならエラーは無かった。
 * @details デバッグ出力が使えない環境のための保険。**節目でだけ呼ぶこと**
 * （毎回呼ぶとドライバによっては同期して遅くなる）。
 */
std::string drain_gl_errors();

/*!
 * @brief 段の名前つきで GL エラーを吸い、**出どころが分かる形で**記録する。
 *
 * @param stage 「3D の描画」「ポスト処理」など、いま終えた段の名前
 *
 * @details `drain_gl_errors()` の行列は**プロセスで 1 本**なので、節目で吸わないと
 * 「どこで出たか」が分からなくなる。実際 2026-08-21 に、描画の側で出たエラーが
 * `PostChain::resize()` の失敗として計上され、**3D が丸ごと出ない**ところまで行った
 *
 * **報せる回数に上限がある。** 毎フレーム同じ行を吐くとログが流れて他が読めないので、
 * 数十回で黙る。エラーが無いときは何も書かないし、`glGetError` 以上のことはしない。
 */
void probe_gl_errors(const char *stage);

} // namespace hd2d::gl
