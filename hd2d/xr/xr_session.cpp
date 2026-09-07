/*!
 * @file xr_session.cpp
 * @brief `xr_session.h` の実装。**OpenXR とプラットフォームに触るのはこのファイルだけ。**
 *
 * 基準は VR の設計 §2・§4・§6、Quest（Android）の島は
 *
 * ## 2 つのプラットフォームの分け方
 * 実装は**1 本**で、Windows と Quest で違うのは §3 の表の 7 つの島と、
 * 「綴りだけが違うもの」を集めた 8 つ目（下の別名の表）だけである。島は
 * `#if defined(_WIN32)` で分ける。**芯を複製しないこと**——複製した瞬間、
 * 片方だけ直された状態が生まれる。
 */
#include "xr/xr_session.h"

#include "render/gl_core.h"

/*
 * OpenXR を組み込む条件。**この傘の内と外で
 * 「本物」と「空のスタブ」が入れ替わる。** 電話版（`:hd2d`）は `HENGBAND_QUEST` を
 * 立てないので、今までどおりスタブのまま組まれる（電話に OpenXR ランタイムは無い）。
 */
#if defined(_WIN32) || defined(HENGBAND_QUEST)
#define HENGBAND_XR_OPENXR 1
#endif

#if defined(HENGBAND_XR_OPENXR)
/* ---- 島 1: include 節（§3） ---------------------------------------------- */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
/*
 * **`unknwn.h` が要る。** `XR_USE_PLATFORM_WIN32` を立てると `openxr_platform.h` は
 * D3D 系の相互運用（`IUnknown*` を取る口）まで宣言するので、`IUnknown` が見えていないと
 * 構文エラーになる。D3D は使わないが、宣言だけは通さないとヘッダが読めない。
 */
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32 1
#define XR_USE_GRAPHICS_API_OPENGL 1
#else /* Quest（Android） */
/*
 * **SDL が持っている口から JNI と EGL を引く。** 窓もコンテキストも SDL が作って
 * いるので、ここで作り直してはならない（Windows で WGL を「いま current なもの」から
 * 取るのと同じ思想。§3 島 2・島 5）。
 */
#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <jni.h>
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1
#endif /* _WIN32 */
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#endif /* HENGBAND_XR_OPENXR */

#include <algorithm>
#include <chrono> //!< vr-perf の 1 行ログ（`perf_tick`）。SDL に依らない時計が要る
#include <cmath>
#include <cstdio>
#include <cstring>

namespace hd2d::xr {

using namespace hd2d::gl;

#if !defined(HENGBAND_XR_OPENXR)

/*
 * この実行体に VR は無い（電話版の `:hd2d`）。`hd2d/` を丸ごと共用しているので、
 * **ここが空でも組めること**が要る。
 */
struct Session::Impl {
};
Session::~Session() = default;
bool Session::init(std::string &err)
{
    err = "VR はこの環境では組み込まれていません。";
    return false;
}
void Session::shutdown() {}
bool Session::begin_frame(Frame &) { return false; }
bool Session::bind_eye(int) { return false; }
unsigned int Session::eye_framebuffer() const { return 0u; }
void *Session::native_instance() const { return nullptr; }
void *Session::native_session() const { return nullptr; }
bool Session::create_ui_layer(int, int, std::string &) { return false; }
void Session::set_ui_panels(const UiPanel *, int) {}
unsigned int Session::bind_ui() { return 0u; }
void Session::end_frame() {}
//! **引数の数を宣言と合わせること。**ここが 3 個のままで Android の組み立てが止まっていた。
void Session::blit_mirror(int, int, int, int, int) {}
bool Session::poll_events() { return false; }
void Session::destroy_swapchains() {}

#else /* HENGBAND_XR_OPENXR */

namespace {

//! 目の数（プライマリステレオ）。
constexpr int kEyeCount = 2;

/*!
 * @name プラットフォームで綴りだけが違うもの（§3 島 1・3・4）
 * @details **中身の段取りは同じ**なので、名前だけここで揃えて芯は 1 本にする。
 * 増やすときはここに 1 行足すのが筋で、`init` の中を `#if` で割らないこと。
 * @{
 */
#if defined(_WIN32)
using SwapchainImage = XrSwapchainImageOpenGLKHR;
using GraphicsRequirements = XrGraphicsRequirementsOpenGLKHR;
using PFN_GetGraphicsRequirements = PFN_xrGetOpenGLGraphicsRequirementsKHR;
constexpr XrStructureType kSwapchainImageType = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
constexpr XrStructureType kGraphicsRequirementsType = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
constexpr const char *kGraphicsExtension = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
constexpr const char *kGraphicsRequirementsProc = "xrGetOpenGLGraphicsRequirementsKHR";
constexpr const char *kGraphicsApiName = "OpenGL";
constexpr const char *kNoGraphicsHint = "SteamVR か Virtual Desktop（VDXR）をお使いください。";
#else
using SwapchainImage = XrSwapchainImageOpenGLESKHR;
using GraphicsRequirements = XrGraphicsRequirementsOpenGLESKHR;
using PFN_GetGraphicsRequirements = PFN_xrGetOpenGLESGraphicsRequirementsKHR;
constexpr XrStructureType kSwapchainImageType = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
constexpr XrStructureType kGraphicsRequirementsType = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR;
constexpr const char *kGraphicsExtension = XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
constexpr const char *kGraphicsRequirementsProc = "xrGetOpenGLESGraphicsRequirementsKHR";
constexpr const char *kGraphicsApiName = "OpenGL ES";
constexpr const char *kNoGraphicsHint = "機内の OpenXR ランタイムを確かめてください。";
#endif
/*! @} */

//! `XrResult` を人が読める名前に（数字だけでは追えない）。
std::string result_name(XrInstance instance, XrResult result)
{
    char buffer[XR_MAX_RESULT_STRING_SIZE] = { 0 };
    if ((instance != XR_NULL_HANDLE) && XR_SUCCEEDED(xrResultToString(instance, result, buffer))) {
        return std::string(buffer);
    }
    return std::string("XrResult(") + std::to_string(static_cast<int>(result)) + ")";
}

//! セッション状態を人が読める名前に（起動行ログと `--vr-check` に出す）。
const char *state_name(XrSessionState state)
{
    switch (state) {
    case XR_SESSION_STATE_IDLE:
        return "IDLE";
    case XR_SESSION_STATE_READY:
        return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED:
        return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE:
        return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED:
        return "FOCUSED";
    case XR_SESSION_STATE_STOPPING:
        return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING:
        return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING:
        return "EXITING";
    default:
        return "UNKNOWN";
    }
}

#if defined(_WIN32)

/*!
 * @brief いま current な WGL の API を引く（§3 島 5）。
 * @details **`opengl32.lib` はリンクしない**（vcxproj 冒頭の方針）ので、
 * `GetModuleHandleW` + `GetProcAddress` で 2 つだけ引く。この 2 つは GL 1.1 の API ではなく
 * WGL の API なので、`opengl32.dll` から直接エクスポートされている（設計書 §2）。
 * SDL が既にコンテキストを作って current にしているから、モジュールは必ず載っている。
 */
bool current_wgl(HDC &dc, HGLRC &rc)
{
    const HMODULE gl = ::GetModuleHandleW(L"opengl32.dll");
    if (gl == nullptr) {
        return false;
    }
    using PFN_wglGetCurrentDC = HDC(WINAPI *)();
    using PFN_wglGetCurrentContext = HGLRC(WINAPI *)();
    const auto get_dc = reinterpret_cast<PFN_wglGetCurrentDC>(
        reinterpret_cast<void *>(::GetProcAddress(gl, "wglGetCurrentDC")));
    const auto get_rc = reinterpret_cast<PFN_wglGetCurrentContext>(
        reinterpret_cast<void *>(::GetProcAddress(gl, "wglGetCurrentContext")));
    if ((get_dc == nullptr) || (get_rc == nullptr)) {
        return false;
    }
    dc = get_dc();
    rc = get_rc();
    return (dc != nullptr) && (rc != nullptr);
}

#else /* Quest（Android） */

/*!
 * @brief いま current な EGL の API を引く（§3 島 5）。
 * @details **`EGLConfig` だけは current から直接取れない。** コンテキストに焼かれている
 * `EGL_CONFIG_ID` を聞いて、その番号で引き直す（罠 Q-5）。ここを nullptr のまま渡すと
 * `xrCreateSession` が通ってしまう機さえあり、後段の swapchain で意味の分からない
 * 失敗に化ける。
 */
bool current_egl(EGLDisplay &display, EGLConfig &config, EGLContext &context)
{
    display = eglGetCurrentDisplay();
    context = eglGetCurrentContext();
    if ((display == EGL_NO_DISPLAY) || (context == EGL_NO_CONTEXT)) {
        return false;
    }
    EGLint config_id = 0;
    if (eglQueryContext(display, context, EGL_CONFIG_ID, &config_id) == EGL_FALSE) {
        return false;
    }
    const EGLint attribs[] = { EGL_CONFIG_ID, config_id, EGL_NONE };
    EGLint count = 0;
    return (eglChooseConfig(display, attribs, &config, 1, &count) != EGL_FALSE) && (count > 0);
}

/*!
 * @brief Android の loader を起こす（§3 島 2）。**`xrCreateInstance` より前に 1 回だけ。**
 * @details 口は `xrGetInstanceProcAddr(XR_NULL_HANDLE, ...)` から引く——instance がまだ
 * 無い段階の例外的な引き方で、**取れなければ loader が古いか入っていない**
 * （AAR の版か packaging を疑う。罠 Q-11）。
 */
bool init_android_loader(JavaVM *vm, jobject activity, std::string &err)
{
    //! 2 度目以降は何もしない（VR を畳んで入り直すとここを通る。loader は 1 プロセス 1 回）。
    static bool done = false;
    if (done) {
        return true;
    }
    PFN_xrInitializeLoaderKHR initialize = nullptr;
    if (XR_FAILED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
            reinterpret_cast<PFN_xrVoidFunction *>(&initialize)))
        || (initialize == nullptr)) {
        err = "OpenXR の loader が古すぎます（xrInitializeLoaderKHR がありません）。";
        return false;
    }
    XrLoaderInitInfoAndroidKHR info{ XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR };
    info.applicationVM = vm;
    info.applicationContext = activity;
    const XrResult result = initialize(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR *>(&info));
    if (XR_FAILED(result)) {
        err = "OpenXR の loader を起こせませんでした（xrInitializeLoaderKHR が "
            + std::to_string(static_cast<int>(result)) + "）。";
        return false;
    }
    done = true;
    return true;
}

#endif /* _WIN32 */

/*!
 * @brief sRGB への自動変換を切る（設計書 §6・罠 4）。
 * @details いまの合成はガンマ済みの値を書いているので、ここで再変換すると白飛びする。
 * swapchain の形式が sRGB なのは「コンポジタがそう読む」ためであって、書き込み側の
 * 変換とは別の話である。
 *
 * **ES では話が逆で、変換は既定 ON。** しかも `GL_EXT_sRGB_write_control` を持たない機では
 * `glDisable` そのものが `GL_INVALID_ENUM` になり、溜まったエラーが後段の節目で
 * 別の失敗に化ける（罠 Q-4）。拡張の有無を見てから呼ぶ。
 */
void disable_srgb_write()
{
#if defined(_WIN32)
    glDisable(GL_FRAMEBUFFER_SRGB);
#else
    static const bool has_write_control = [] {
        constexpr GLenum kGlExtensions = 0x1F03;
        const bool found = gl_string(kGlExtensions).find("GL_EXT_sRGB_write_control") != std::string::npos;
        //! 無い機では絵が白っぽくなる。**黙って白くしない**（起動行に理由を残す）。
        std::fprintf(stderr, "[hd2d] XR_SRGB write_control=%s\n", found ? "yes" : "no（絵が明るく出ます）");
        return found;
    }();
    if (has_write_control) {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
#endif
}

} // namespace

//! 目 1 つぶんの swapchain。
struct EyeChain {
    XrSwapchain swapchain{ XR_NULL_HANDLE };
    std::vector<SwapchainImage> images;
    int width{ 0 };
    int height{ 0 };
    //! いま acquire している画像の番号（`begin_frame` で決まる）。
    std::uint32_t acquired{ 0 };
    //! 深度は自前で持つ（ランタイムからは色しか来ない）。目ごとに 1 枚。
    GLuint depth{ 0 };
};

struct Session::Impl {
#if !defined(_WIN32)
    /*!
     * @name Java 側の API（§3 島 2・3）
     * @details **`SDL_AndroidGetActivity()` が返すのは局所参照**で、そのフレームを
     * 抜ければ無効になる。loader も instance もこれを寿命ぶん握るので、`NewGlobalRef` で
     * 持ち替えて `shutdown` で返す。局所参照のまま渡すと「最初は動くのに後で落ちる」型に
     * なる（罠 Q-2）。
     * @{
     */
    JavaVM *vm{ nullptr };
    jobject activity{ nullptr };
    /*! @} */
#endif
    XrInstance instance{ XR_NULL_HANDLE };
    XrSystemId system{ XR_NULL_SYSTEM_ID };
    XrSession session{ XR_NULL_HANDLE };
    XrSpace space{ XR_NULL_HANDLE };
    XrSessionState state{ XR_SESSION_STATE_UNKNOWN };
    //! `xrBeginSession` を通したか（STOPPING で必ず `xrEndSession` を返すために持つ）。
    bool session_running{ false };
    //! ランタイムから「もう畳め」と言われたか。
    bool exiting{ false };
    //! `xrBeginFrame` と `xrEndFrame` の対の途中か（`end_frame` の二重呼びよけ）。
    bool in_frame{ false };
    //! この回に絵を積むか（`shouldRender` と姿勢が取れたかの両方）。
    bool submit_layer{ false };

    EyeChain eyes[kEyeCount];
    XrView views[kEyeCount]{};
    XrCompositionLayerProjectionView layer_views[kEyeCount]{};
    XrFrameState frame_state{ XR_TYPE_FRAME_STATE };

    /*!
     * @name 文字 UI のクワッドレイヤ（§7）
     * @{
     */
    XrSwapchain ui_swapchain{ XR_NULL_HANDLE };
    std::vector<SwapchainImage> ui_images;
    int ui_width{ 0 };
    int ui_height{ 0 };
    std::uint32_t ui_acquired{ 0 };
    bool ui_acquired_this_frame{ false };
    GLuint ui_fbo{ 0 };
    /*!
     * @brief 空中の板（設計書 §20）。**枚数ぶんのクワッドを積む。**
     * @details `quads` は `xrEndFrame` が読むまで生きていなければならないので、
     * フレームの中の局所変数にはできない（ここに置く理由がそれ）。
     */
    std::vector<UiPanel> panels;
    std::vector<XrCompositionLayerQuad> quads;
    std::vector<const XrCompositionLayerBaseHeader *> layers;
    /*! @} */

    //! swapchain の画像を結ぶための FBO（使い回す 1 枚）。
    GLuint fbo{ 0 };
    //! 鏡窓へ写すための読み出し FBO。
    GLuint mirror_fbo{ 0 };
    //! 選んだ色形式（`GL_SRGB8_ALPHA8` が第一候補。設計書 §6）。
    std::int64_t color_format{ 0 };
};

Session::~Session()
{
    this->shutdown();
}

bool Session::init(std::string &err)
{
    this->shutdown();
    //! **`shutdown` では消さない。**判定は畳んだ後に見るので、消すのは作り直すここ。
    this->focus_reached_ = false;
    this->state_log_.clear();
    auto *impl = new Impl();
    this->impl_ = impl;

#if !defined(_WIN32)
    /*
     * --- (0) 島 2: Android の loader を起こす。**拡張を数えるより前**である
     * （loader が起きていないと `xrEnumerateInstanceExtensionProperties` すら
     * ランタイムへ届かない）。
     */
    {
        auto *const env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
        if (env != nullptr) {
            if (auto *const local = static_cast<jobject>(SDL_AndroidGetActivity()); local != nullptr) {
                if (env->GetJavaVM(&impl->vm) == JNI_OK) {
                    impl->activity = env->NewGlobalRef(local);
                }
                //! **どの道でも局所参照は返す。**返さないと SDL の主スレッドに溜まり続ける。
                env->DeleteLocalRef(local);
            }
        }
        if ((impl->vm == nullptr) || (impl->activity == nullptr)) {
            err = "Java 側の API（JNIEnv / Activity / JavaVM）を引けませんでした。";
            this->shutdown();
            return false;
        }
        if (!init_android_loader(impl->vm, impl->activity, err)) {
            this->shutdown();
            return false;
        }
    }
#endif

    /* --- (1) 拡張の確認。GL バインドが無いランタイムでは何も始まらない --- */
    std::uint32_t ext_count = 0;
    if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &ext_count, nullptr))) {
        err = "OpenXR のランタイムが見つかりません（loader が ActiveRuntime を引けませんでした）。";
        this->shutdown();
        return false;
    }
    std::vector<XrExtensionProperties> exts(ext_count, { XR_TYPE_EXTENSION_PROPERTIES });
    if (ext_count > 0) {
        (void)xrEnumerateInstanceExtensionProperties(nullptr, ext_count, &ext_count, exts.data());
    }
    const auto has_extension = [&exts](const char *name) {
        return std::any_of(exts.begin(), exts.end(), [name](const XrExtensionProperties &e) {
            return std::strcmp(e.extensionName, name) == 0;
        });
    };
    if (!has_extension(kGraphicsExtension)) {
        err = std::string("このランタイムは ") + kGraphicsApiName + " に対応していません（"
            + kGraphicsExtension + " が無い）。\n" + kNoGraphicsHint;
        this->shutdown();
        return false;
    }
#if !defined(_WIN32)
    //! instance へ VM とアクティビティを渡す手段。これが無いと機内ランタイムに繋がらない（§3 島 3）。
    if (!has_extension(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) {
        err = "このランタイムには " XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME
              " がありません。\n" + std::string(kNoGraphicsHint);
        this->shutdown();
        return false;
    }
#endif

    /* --- (2) instance。**島 3 は「拡張の並び」と `next` だけ**（§3） --- */
#if defined(_WIN32)
    const char *const enabled[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
#else
    const char *const enabled[] = { XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME };
#endif
    XrInstanceCreateInfo instance_info{ XR_TYPE_INSTANCE_CREATE_INFO };
#if !defined(_WIN32)
    /*
     * **`next` に繋ぐ構造体は `xrCreateInstance` が読み終わるまで生きていること。**
     * 局所変数でよいのは、その呼び出しがこの関数を出ないからである。
     */
    XrInstanceCreateInfoAndroidKHR android_info{ XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    android_info.applicationVM = impl->vm;
    android_info.applicationActivity = impl->activity;
    instance_info.next = &android_info;
#endif
    instance_info.enabledExtensionCount = static_cast<std::uint32_t>(sizeof(enabled) / sizeof(enabled[0]));
    instance_info.enabledExtensionNames = enabled;
    std::strncpy(instance_info.applicationInfo.applicationName, "Hengband HD2D",
        XR_MAX_APPLICATION_NAME_SIZE - 1);
    instance_info.applicationInfo.applicationVersion = 1;
    std::strncpy(instance_info.applicationInfo.engineName, "hengband-voxel-hd2d",
        XR_MAX_ENGINE_NAME_SIZE - 1);
    instance_info.applicationInfo.engineVersion = 1;
    instance_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    XrResult result = xrCreateInstance(&instance_info, &impl->instance);
    if (XR_FAILED(result)) {
        err = "OpenXR の instance を作れませんでした: " + result_name(XR_NULL_HANDLE, result);
        this->shutdown();
        return false;
    }

    {
        XrInstanceProperties props{ XR_TYPE_INSTANCE_PROPERTIES };
        if (XR_SUCCEEDED(xrGetInstanceProperties(impl->instance, &props))) {
            char buffer[128] = { 0 };
            std::snprintf(buffer, sizeof(buffer), "%s %d.%d.%d", props.runtimeName,
                static_cast<int>(XR_VERSION_MAJOR(props.runtimeVersion)),
                static_cast<int>(XR_VERSION_MINOR(props.runtimeVersion)),
                static_cast<int>(XR_VERSION_PATCH(props.runtimeVersion)));
            this->runtime_name_ = buffer;
        }
    }
    /*
     * **どのランタイムに繋がったかを必ず出す**（設計書 罠 1）。Virtual Desktop を
     * 入れると ActiveRuntime が VDXR に変わっているので、SteamVR で試している
     * つもりが VDXR、の取り違えがよく起きる。
     */
    std::fprintf(stderr, "[hd2d] XR_RUNTIME %s\n",
        this->runtime_name_.empty() ? "(unknown)" : this->runtime_name_.c_str());

    /* --- (3) system（HMD） --- */
    XrSystemGetInfo system_info{ XR_TYPE_SYSTEM_GET_INFO };
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    result = xrGetSystem(impl->instance, &system_info, &impl->system);
    if (XR_FAILED(result)) {
        err = "HMD が見つかりません: " + result_name(impl->instance, result)
            + "\n（ランタイム: " + this->runtime_name_ + "）";
        this->shutdown();
        return false;
    }

    /*
     * レイヤの上限。**空中の板をこの数までしか積めない**（設計書 §20）。
     * 仕様の下限は 16 なので、取れなかったときはそれで通す。
     */
    {
        XrSystemProperties properties{ XR_TYPE_SYSTEM_PROPERTIES };
        if (XR_SUCCEEDED(xrGetSystemProperties(impl->instance, impl->system, &properties))) {
            this->max_layers_ = std::max(1, static_cast<int>(properties.graphicsProperties.maxLayerCount));
        }
    }

    /*
     * --- (4) GL の版の確認。**session を作る前に必ず呼ぶ**（仕様の要求）。
     * 島 4（§3）は「引く手段の名前と構造体の綴り」だけで、段取りは Windows も Quest も同じ。
     */
    {
        PFN_GetGraphicsRequirements get_req = nullptr;
        result = xrGetInstanceProcAddr(impl->instance, kGraphicsRequirementsProc,
            reinterpret_cast<PFN_xrVoidFunction *>(&get_req));
        if (XR_FAILED(result) || (get_req == nullptr)) {
            err = std::string(kGraphicsRequirementsProc) + " を引けませんでした。";
            this->shutdown();
            return false;
        }
        GraphicsRequirements req{ kGraphicsRequirementsType };
        result = get_req(impl->instance, impl->system, &req);
        if (XR_FAILED(result)) {
            err = std::string(kGraphicsApiName) + " の対応版を問い合わせられませんでした: "
                + result_name(impl->instance, result);
            this->shutdown();
            return false;
        }
        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        const XrVersion have = XR_MAKE_VERSION(major, minor, 0);
        if (have < req.minApiVersionSupported) {
            err = std::string("このランタイムは ") + kGraphicsApiName + " "
                + std::to_string(static_cast<int>(XR_VERSION_MAJOR(req.minApiVersionSupported))) + "."
                + std::to_string(static_cast<int>(XR_VERSION_MINOR(req.minApiVersionSupported)))
                + " 以上を要求しています（いま " + std::to_string(major) + "." + std::to_string(minor) + "）。";
            this->shutdown();
            return false;
        }
    }

    /* --- (5) session（いま current な GL コンテキストを渡す。島 5） --- */
#if defined(_WIN32)
    HDC dc = nullptr;
    HGLRC rc = nullptr;
    if (!current_wgl(dc, rc)) {
        err = "いまの OpenGL コンテキストを引けませんでした（WGL の APIが取れない）。";
        this->shutdown();
        return false;
    }
    XrGraphicsBindingOpenGLWin32KHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    binding.hDC = dc;
    binding.hGLRC = rc;
#else
    XrGraphicsBindingOpenGLESAndroidKHR binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    if (!current_egl(binding.display, binding.config, binding.context)) {
        err = "いまの OpenGL ES コンテキストを引けませんでした（EGL の APIが取れない）。";
        this->shutdown();
        return false;
    }
#endif
    XrSessionCreateInfo session_info{ XR_TYPE_SESSION_CREATE_INFO };
    session_info.next = &binding;
    session_info.systemId = impl->system;
    result = xrCreateSession(impl->instance, &session_info, &impl->session);
    if (XR_FAILED(result)) {
        err = "OpenXR の session を作れませんでした: " + result_name(impl->instance, result);
        this->shutdown();
        return false;
    }

    /* --- (6) 基準空間。STAGE（床の広さを知っている）が第一希望・無ければ LOCAL --- */
    {
        std::uint32_t space_count = 0;
        (void)xrEnumerateReferenceSpaces(impl->session, 0, &space_count, nullptr);
        std::vector<XrReferenceSpaceType> spaces(space_count);
        if (space_count > 0) {
            (void)xrEnumerateReferenceSpaces(impl->session, space_count, &space_count, spaces.data());
        }
        const bool has_stage = std::find(spaces.begin(), spaces.end(), XR_REFERENCE_SPACE_TYPE_STAGE) != spaces.end();
        XrReferenceSpaceCreateInfo space_info{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        space_info.referenceSpaceType = has_stage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
        space_info.poseInReferenceSpace.orientation.w = 1.f;
        result = xrCreateReferenceSpace(impl->session, &space_info, &impl->space);
        if (XR_FAILED(result)) {
            err = "基準空間を作れませんでした: " + result_name(impl->instance, result);
            this->shutdown();
            return false;
        }
        std::fprintf(stderr, "[hd2d] XR_SPACE %s\n", has_stage ? "STAGE" : "LOCAL");

    }

    /* --- (7) 目の寸法。**推奨値に従う**（自分で決めない。設計書 §6） --- */
    std::uint32_t view_count = 0;
    result = xrEnumerateViewConfigurationViews(impl->instance, impl->system,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, nullptr);
    if (XR_FAILED(result) || (view_count != static_cast<std::uint32_t>(kEyeCount))) {
        err = "両眼の構成が取れませんでした（目の数 " + std::to_string(view_count) + "）。";
        this->shutdown();
        return false;
    }
    std::vector<XrViewConfigurationView> conf(view_count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    (void)xrEnumerateViewConfigurationViews(impl->instance, impl->system,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view_count, &view_count, conf.data());

    /* --- (8) 色形式。sRGB を第一候補に、ランタイムが並べた順から選ぶ --- */
    {
        std::uint32_t format_count = 0;
        (void)xrEnumerateSwapchainFormats(impl->session, 0, &format_count, nullptr);
        std::vector<std::int64_t> formats(format_count);
        if (format_count > 0) {
            (void)xrEnumerateSwapchainFormats(impl->session, format_count, &format_count, formats.data());
        }
        const std::int64_t wanted[] = { static_cast<std::int64_t>(GL_SRGB8_ALPHA8),
            static_cast<std::int64_t>(GL_RGBA8) };
        for (const std::int64_t want : wanted) {
            if (std::find(formats.begin(), formats.end(), want) != formats.end()) {
                impl->color_format = want;
                break;
            }
        }
        if ((impl->color_format == 0) && !formats.empty()) {
            impl->color_format = formats.front(); //!< 望みのが無ければ先頭（ランタイムの第一希望）
        }
        if (impl->color_format == 0) {
            err = "swapchain の色形式が 1 つも取れませんでした。";
            this->shutdown();
            return false;
        }
    }

    /* --- (9) swapchain と深度 --- */
    /*
     * **推奨値に倍率を掛ける**。
     * Quest の推奨値（片目 1440×1584 級）で影・ブルーム込みの両眼 72Hz が出るかは
     * 実測でしか分からないので、重かったときに落とす口が要る。
     *
     * **1.0 のときは 1 ビットも触らない。**PC VR の絵はここで決着済みなので、
     * 丸めの都合で 1 画素でも動かしてはならない。
     */
    const auto scaled = [scale = this->render_scale_](std::uint32_t recommended) {
        if (scale >= 0.999f) {
            return static_cast<int>(recommended);
        }
        return std::max(1, static_cast<int>(std::lround(static_cast<double>(recommended) * scale)));
    };
    for (int eye = 0; eye < kEyeCount; ++eye) {
        EyeChain &chain = impl->eyes[eye];
        chain.width = scaled(conf[static_cast<std::size_t>(eye)].recommendedImageRectWidth);
        chain.height = scaled(conf[static_cast<std::size_t>(eye)].recommendedImageRectHeight);

        XrSwapchainCreateInfo chain_info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        chain_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        chain_info.format = impl->color_format;
        chain_info.sampleCount = 1; //!< MSAA は使わない（ポスト処理が受ける。設計書 §6）
        chain_info.width = static_cast<std::uint32_t>(chain.width);
        chain_info.height = static_cast<std::uint32_t>(chain.height);
        chain_info.faceCount = 1;
        chain_info.arraySize = 1;
        chain_info.mipCount = 1;
        result = xrCreateSwapchain(impl->session, &chain_info, &chain.swapchain);
        if (XR_FAILED(result)) {
            err = "swapchain を作れませんでした: " + result_name(impl->instance, result);
            this->shutdown();
            return false;
        }
        std::uint32_t image_count = 0;
        (void)xrEnumerateSwapchainImages(chain.swapchain, 0, &image_count, nullptr);
        chain.images.assign(image_count, { kSwapchainImageType });
        (void)xrEnumerateSwapchainImages(chain.swapchain, image_count, &image_count,
            reinterpret_cast<XrSwapchainImageBaseHeader *>(chain.images.data()));

        glGenRenderbuffers(1, &chain.depth);
        glBindRenderbuffer(GL_RENDERBUFFER, chain.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, chain.width, chain.height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
    }
    glGenFramebuffers(1, &impl->fbo);
    glGenFramebuffers(1, &impl->mirror_fbo);

    std::fprintf(stderr, "[hd2d] XR_VIEW %dx%d x2  format=0x%04X\n", impl->eyes[0].width,
        impl->eyes[0].height, static_cast<unsigned>(impl->color_format));

    this->valid_ = true;
    return true;
}

void Session::shutdown()
{
    /*
     * vr-perf の数え札を捨てる（§15 の 4）。ゲーム内から VR を入切できるので、
     * **立て直したときに前回の区間の間隔が混ざらないように**する
     * （残すと「畳んでいた時間」が max に化ける）。
     */
    this->perf_last_ns_ = 0;
    this->perf_window_ns_ = 0;
    this->perf_sum_ms_ = 0.0;
    this->perf_min_ms_ = 0.0;
    this->perf_max_ms_ = 0.0;
    this->perf_frames_ = 0;
    if (this->impl_ == nullptr) {
        this->valid_ = false;
        this->focused_ = false;
        return;
    }
    Impl *const impl = this->impl_;
    /*
     * **`xrBeginFrame` の途中で畳まない。**対を閉じずにセッションを終えると、
     * ランタイムによっては次の起動まで状態が残る。
     */
    if (impl->in_frame) {
        XrFrameEndInfo end_info{ XR_TYPE_FRAME_END_INFO };
        end_info.displayTime = impl->frame_state.predictedDisplayTime;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        (void)xrEndFrame(impl->session, &end_info);
        impl->in_frame = false;
    }
    if (impl->session_running) {
        (void)xrEndSession(impl->session);
        impl->session_running = false;
    }
    this->destroy_swapchains();
    if (impl->space != XR_NULL_HANDLE) {
        (void)xrDestroySpace(impl->space);
    }
    if (impl->session != XR_NULL_HANDLE) {
        (void)xrDestroySession(impl->session);
    }
    if (impl->instance != XR_NULL_HANDLE) {
        (void)xrDestroyInstance(impl->instance);
    }
#if !defined(_WIN32)
    /*
     * **大域参照は必ず返す**（罠 Q-2）。instance を消した後にするのは、instance が
     * 生きている間はランタイムがこのアクティビティを掴んでいるためである。
     */
    if (impl->activity != nullptr) {
        if (auto *const env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv()); env != nullptr) {
            env->DeleteGlobalRef(impl->activity);
        }
        impl->activity = nullptr;
    }
#endif
    delete impl;
    this->impl_ = nullptr;
    this->valid_ = false;
    this->focused_ = false;
}

void Session::destroy_swapchains()
{
    Impl *const impl = this->impl_;
    if (impl == nullptr) {
        return;
    }
    for (auto &chain : impl->eyes) {
        if (chain.depth != 0) {
            glDeleteRenderbuffers(1, &chain.depth);
            chain.depth = 0;
        }
        if (chain.swapchain != XR_NULL_HANDLE) {
            (void)xrDestroySwapchain(chain.swapchain);
            chain.swapchain = XR_NULL_HANDLE;
        }
        chain.images.clear();
    }
    if (impl->ui_swapchain != XR_NULL_HANDLE) {
        (void)xrDestroySwapchain(impl->ui_swapchain);
        impl->ui_swapchain = XR_NULL_HANDLE;
    }
    impl->ui_images.clear();
    impl->ui_acquired_this_frame = false;
    if (impl->ui_fbo != 0) {
        glDeleteFramebuffers(1, &impl->ui_fbo);
        impl->ui_fbo = 0;
    }
    if (impl->fbo != 0) {
        glDeleteFramebuffers(1, &impl->fbo);
        impl->fbo = 0;
    }
    if (impl->mirror_fbo != 0) {
        glDeleteFramebuffers(1, &impl->mirror_fbo);
        impl->mirror_fbo = 0;
    }
}

/*!
 * @brief 溜まったイベントを**空になるまで**吸う。
 * @return セッションが生きているか（false ならもう畳んでよい）。
 * @details 吸うのを怠るとランタイムがセッションを切ってくる（設計書 罠 7）。
 */
bool Session::poll_events()
{
    Impl *const impl = this->impl_;
    for (;;) {
        XrEventDataBuffer event{ XR_TYPE_EVENT_DATA_BUFFER };
        const XrResult result = xrPollEvent(impl->instance, &event);
        if (result == XR_EVENT_UNAVAILABLE) {
            break;
        }
        if (XR_FAILED(result)) {
            impl->exiting = true;
            break;
        }
        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto &changed = reinterpret_cast<const XrEventDataSessionStateChanged &>(event);
            impl->state = changed.state;
            this->state_log_.emplace_back(state_name(changed.state));
            this->focused_ = (changed.state == XR_SESSION_STATE_FOCUSED);
            this->focus_reached_ = this->focus_reached_ || this->focused_;
            if (changed.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin_info{ XR_TYPE_SESSION_BEGIN_INFO };
                begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (XR_SUCCEEDED(xrBeginSession(impl->session, &begin_info))) {
                    impl->session_running = true;
                }
            } else if (changed.state == XR_SESSION_STATE_STOPPING) {
                //! **STOPPING を受けたら必ず `xrEndSession`**（設計書 罠 7）。
                (void)xrEndSession(impl->session);
                impl->session_running = false;
            } else if ((changed.state == XR_SESSION_STATE_EXITING)
                || (changed.state == XR_SESSION_STATE_LOSS_PENDING)) {
                impl->exiting = true;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            impl->exiting = true;
            break;
        default:
            break;
        }
    }
    return !impl->exiting;
}

bool Session::begin_frame(Frame &out)
{
    Impl *const impl = this->impl_;
    if ((impl == nullptr) || (impl->session == XR_NULL_HANDLE)) {
        return false;
    }
    if (!this->poll_events()) {
        return false;
    }
    if (!impl->session_running) {
        return false; //!< IDLE。まだ `xrWaitFrame` を呼べる状態ではない
    }

    impl->frame_state = XrFrameState{ XR_TYPE_FRAME_STATE };
    XrFrameWaitInfo wait_info{ XR_TYPE_FRAME_WAIT_INFO };
    //! **歩調はここが握る**（設計書 §4）。vsync と `SDL_Delay` は VR 中は切ること（罠 5）。
    if (XR_FAILED(xrWaitFrame(impl->session, &wait_info, &impl->frame_state))) {
        return false;
    }
    /*
     * vr-perf（§15 の 4）。**待ちが返った直後に数える**——ここから次にここへ戻るまでが
     * 1 フレームの実測である。描かない回（`shouldRender == false`）も歩調のうちなので
     * 数に入れる（抜くと「重い区間だけ数が減る」という読みにくい数字になる）。
     */
    this->perf_tick(impl->eyes[0].width, impl->eyes[0].height);
    XrFrameBeginInfo begin_info{ XR_TYPE_FRAME_BEGIN_INFO };
    if (XR_FAILED(xrBeginFrame(impl->session, &begin_info))) {
        return false;
    }
    impl->in_frame = true;
    impl->submit_layer = false;

    out = Frame{};
    out.predicted_display_time = static_cast<std::int64_t>(impl->frame_state.predictedDisplayTime);
    if (impl->frame_state.shouldRender == XR_FALSE) {
        //! 描かない回。対はここで閉じる（呼ぶ側は `end_frame` を呼ばない）。
        this->end_frame();
        return false;
    }

    /* --- 姿勢と画角を `predictedDisplayTime` で引く（罠 8） --- */
    for (int eye = 0; eye < kEyeCount; ++eye) {
        impl->views[eye] = XrView{ XR_TYPE_VIEW };
    }
    XrViewLocateInfo locate{ XR_TYPE_VIEW_LOCATE_INFO };
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = impl->frame_state.predictedDisplayTime;
    locate.space = impl->space;
    XrViewState view_state{ XR_TYPE_VIEW_STATE };
    std::uint32_t located = 0;
    const XrResult located_result = xrLocateViews(impl->session, &locate, &view_state, kEyeCount, &located, impl->views);
    const bool have_pose = XR_SUCCEEDED(located_result)
        && ((view_state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0)
        && ((view_state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0)
        && (located == static_cast<std::uint32_t>(kEyeCount));

    out.view_count = kEyeCount;
    out.views_valid = have_pose;
    for (int eye = 0; eye < kEyeCount; ++eye) {
        EyeChain &chain = impl->eyes[eye];
        out.views[eye].width = chain.width;
        out.views[eye].height = chain.height;
        const XrView &view = impl->views[eye];
        out.views[eye].position[0] = view.pose.position.x;
        out.views[eye].position[1] = view.pose.position.y;
        out.views[eye].position[2] = view.pose.position.z;
        out.views[eye].orientation[0] = view.pose.orientation.x;
        out.views[eye].orientation[1] = view.pose.orientation.y;
        out.views[eye].orientation[2] = view.pose.orientation.z;
        out.views[eye].orientation[3] = view.pose.orientation.w;
        out.views[eye].fov_left = view.fov.angleLeft;
        out.views[eye].fov_right = view.fov.angleRight;
        out.views[eye].fov_up = view.fov.angleUp;
        out.views[eye].fov_down = view.fov.angleDown;
    }
    if (!have_pose) {
        //! 姿勢が無い回は絵を積まない（積むと前の絵が固まって見える）。
        this->end_frame();
        return false;
    }

    /* --- swapchain の画像を取る --- */
    for (int eye = 0; eye < kEyeCount; ++eye) {
        EyeChain &chain = impl->eyes[eye];
        XrSwapchainImageAcquireInfo acquire{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        if (XR_FAILED(xrAcquireSwapchainImage(chain.swapchain, &acquire, &chain.acquired))) {
            this->end_frame();
            return false;
        }
        XrSwapchainImageWaitInfo wait{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wait.timeout = XR_INFINITE_DURATION;
        if (XR_FAILED(xrWaitSwapchainImage(chain.swapchain, &wait))) {
            this->end_frame();
            return false;
        }
    }
    impl->submit_layer = true;
    return true;
}

bool Session::bind_eye(int eye)
{
    Impl *const impl = this->impl_;
    if ((impl == nullptr) || !impl->submit_layer || (eye < 0) || (eye >= kEyeCount)) {
        return false;
    }
    EyeChain &chain = impl->eyes[eye];
    if (chain.acquired >= chain.images.size()) {
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, impl->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        chain.images[chain.acquired].image, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, chain.depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glViewport(0, 0, chain.width, chain.height);
    //! **sRGB の自動変換は切ったまま**（設計書 §6・罠 4。ES の事情は `disable_srgb_write`）。
    disable_srgb_write();
    return true;
}

unsigned int Session::eye_framebuffer() const
{
    return (this->impl_ != nullptr) ? static_cast<unsigned int>(this->impl_->fbo) : 0u;
}

void *Session::native_instance() const
{
    return (this->impl_ != nullptr) ? static_cast<void *>(this->impl_->instance) : nullptr;
}

void *Session::native_session() const
{
    return (this->impl_ != nullptr) ? static_cast<void *>(this->impl_->session) : nullptr;
}

bool Session::create_ui_layer(int w, int h, std::string &err)
{
    Impl *const impl = this->impl_;
    if ((impl == nullptr) || (impl->session == XR_NULL_HANDLE)) {
        err = "セッションが立っていません。";
        return false;
    }
    if (impl->ui_swapchain != XR_NULL_HANDLE) {
        if ((impl->ui_width == std::max(1, w)) && (impl->ui_height == std::max(1, h))) {
            return true; //!< 同じ大きさで既にある
        }
        /*
         * **窓の大きさが変わった。作り直す**（罠 30）。呼ぶ側は `ui_layout` の矩形
         * （窓の座標）で板を指すので、ここが古いままだと板が別の場所を写す
         * ——全画面へ切り替えた瞬間に「サブパネルがちりぢり」「ミニマップが 2 つに
         * ちぎれる」という見え方になる（実機で踏んだ）。
         *
         * フレームの途中で消さないこと（`bind_ui` で取った画像を返す前に消すと
         * ランタイムが落ちる）。呼ぶ側はフレームの頭で呼んでいる。
         */
        if (impl->ui_acquired_this_frame) {
            XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            (void)xrReleaseSwapchainImage(impl->ui_swapchain, &release);
            impl->ui_acquired_this_frame = false;
        }
        impl->panels.clear();
        (void)xrDestroySwapchain(impl->ui_swapchain);
        impl->ui_swapchain = XR_NULL_HANDLE;
        impl->ui_images.clear();
        std::fprintf(stderr, "[hd2d] XR_UI 窓が %dx%d へ変わったので板を作り直します\n",
            std::max(1, w), std::max(1, h));
    }
    XrSwapchainCreateInfo info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = impl->color_format;
    info.sampleCount = 1;
    info.width = static_cast<std::uint32_t>(std::max(1, w));
    info.height = static_cast<std::uint32_t>(std::max(1, h));
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    const XrResult result = xrCreateSwapchain(impl->session, &info, &impl->ui_swapchain);
    if (XR_FAILED(result)) {
        err = "UI の板を作れませんでした: " + result_name(impl->instance, result);
        return false;
    }
    impl->ui_width = std::max(1, w);
    impl->ui_height = std::max(1, h);
    //! 外から見比べる用（呼ぶ側が窓と突き合わせる）。
    this->ui_width_ = impl->ui_width;
    this->ui_height_ = impl->ui_height;
    std::uint32_t count = 0;
    (void)xrEnumerateSwapchainImages(impl->ui_swapchain, 0, &count, nullptr);
    impl->ui_images.assign(count, { kSwapchainImageType });
    (void)xrEnumerateSwapchainImages(impl->ui_swapchain, count, &count,
        reinterpret_cast<XrSwapchainImageBaseHeader *>(impl->ui_images.data()));
    if (impl->ui_fbo == 0) {
        glGenFramebuffers(1, &impl->ui_fbo); //!< 作り直しのときは使い回す（結び先だけ替わる）
    }
    std::fprintf(stderr, "[hd2d] XR_UI %dx%d\n", impl->ui_width, impl->ui_height);
    return true;
}

void Session::set_ui_panels(const UiPanel *panels, int count)
{
    Impl *const impl = this->impl_;
    if (impl == nullptr) {
        return;
    }
    impl->panels.clear();
    if ((panels == nullptr) || (count <= 0)) {
        return;
    }
    /*
     * **上限はランタイムが言う枚数から 1 を引いた数**（シーンのプロジェクションレイヤに
     * 1 枚要る）。仕様の下限は 16 なので、ふつうは 15 枚まで積める。溢れたら黙って
     * 落とす——出ない板があるのは困るが、`xrEndFrame` が
     * `XR_ERROR_LAYER_LIMIT_EXCEEDED` で落ちて**全部消える**よりはよい。
     */
    const int room = std::max(0, this->max_layers_ - 1);
    const int take = std::min(count, room);
    impl->panels.assign(panels, panels + take);
}

unsigned int Session::bind_ui()
{
    Impl *const impl = this->impl_;
    if ((impl == nullptr) || !impl->submit_layer || (impl->ui_swapchain == XR_NULL_HANDLE)) {
        return 0u;
    }
    if (!impl->ui_acquired_this_frame) {
        XrSwapchainImageAcquireInfo acquire{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
        if (XR_FAILED(xrAcquireSwapchainImage(impl->ui_swapchain, &acquire, &impl->ui_acquired))) {
            return 0u;
        }
        XrSwapchainImageWaitInfo wait{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
        wait.timeout = XR_INFINITE_DURATION;
        if (XR_FAILED(xrWaitSwapchainImage(impl->ui_swapchain, &wait))) {
            return 0u;
        }
        impl->ui_acquired_this_frame = true;
    }
    if (impl->ui_acquired >= impl->ui_images.size()) {
        return 0u;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, impl->ui_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        impl->ui_images[impl->ui_acquired].image, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0u;
    }
    glViewport(0, 0, impl->ui_width, impl->ui_height);
    /*
     * **透明で消す。**描かなかった所はシーンが透ける（レイヤに
     * `BLEND_TEXTURE_SOURCE_ALPHA` を立ててある）。不透明で消すと板が
     * 世界を丸ごと覆い隠す。
     */
    disable_srgb_write();
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    return static_cast<unsigned int>(impl->ui_fbo);
}

void Session::blit_mirror(int eye, int dst_x0, int dst_y0, int dst_x1, int dst_y1)
{
    Impl *const impl = this->impl_;
    if ((impl == nullptr) || !impl->submit_layer || (eye < 0) || (eye >= kEyeCount)) {
        return;
    }
    EyeChain &chain = impl->eyes[eye];
    if (chain.acquired >= chain.images.size()) {
        return;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, impl->mirror_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        chain.images[chain.acquired].image, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, chain.width, chain.height, dst_x0, dst_y0, dst_x1, dst_y1,
        GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Session::end_frame()
{
    Impl *const impl = this->impl_;
    if ((impl == nullptr) || !impl->in_frame) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    XrCompositionLayerProjection layer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    impl->quads.clear();
    impl->layers.clear();
    if (impl->submit_layer) {
        for (int eye = 0; eye < kEyeCount; ++eye) {
            EyeChain &chain = impl->eyes[eye];
            XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            (void)xrReleaseSwapchainImage(chain.swapchain, &release);

            impl->layer_views[eye] = XrCompositionLayerProjectionView{ XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            impl->layer_views[eye].pose = impl->views[eye].pose;
            impl->layer_views[eye].fov = impl->views[eye].fov;
            impl->layer_views[eye].subImage.swapchain = chain.swapchain;
            impl->layer_views[eye].subImage.imageArrayIndex = 0;
            impl->layer_views[eye].subImage.imageRect.offset = { 0, 0 };
            impl->layer_views[eye].subImage.imageRect.extent = { chain.width, chain.height };
        }
        layer.space = impl->space;
        layer.viewCount = kEyeCount;
        layer.views = impl->layer_views;
        impl->layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&layer));

        /*
         * 空中の板（§7・§20）。**シーンの後に積む**（コンポジタは並び順に重ねる）。
         * 描かなかった所を透かすため `BLEND_TEXTURE_SOURCE_ALPHA` を立てる。
         * 片目ぶんではなく両目に同じ板を出すので `eyeVisibility` は BOTH。
         *
         * 板が何枚でも**テクスチャは 1 枚**で、それぞれが部分矩形を指す。
         */
        if (impl->ui_acquired_this_frame) {
            XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            (void)xrReleaseSwapchainImage(impl->ui_swapchain, &release);
            impl->ui_acquired_this_frame = false;

            impl->quads.reserve(impl->panels.size());
            for (const UiPanel &panel : impl->panels) {
                if ((panel.rect_w <= 0) || (panel.rect_h <= 0)) {
                    continue;
                }
                XrCompositionLayerQuad quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
                quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
                quad.space = impl->space;
                quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                quad.subImage.swapchain = impl->ui_swapchain;
                quad.subImage.imageArrayIndex = 0;
                /*
                 * **矩形の縦は引っくり返す**（罠 27）。呼ぶ側は `ui_layout` の座標
                 * （左上原点）で渡してくるが、GL のテクスチャは**下が 0 行目**である。
                 * ここを間違えると、板の中身が上下逆になるのではなく
                 * **別の板の中身が出る**（矩形ごと縦に鏡になるため）。
                 */
                /*
                 * **テクスチャの外へはみ出させない。**はみ出た矩形を渡すと、
                 * ランタイムによっては黙って別の場所を写す（何が出ているのか
                 * 分からない絵になる）。ここで締めておけば、窓の大きさと板の
                 * 大きさが 1 フレームずれた回も「端が切れる」で済む。
                 */
                const int x = std::clamp(panel.rect_x, 0, std::max(0, impl->ui_width - 1));
                const int y = std::clamp(panel.rect_y, 0, std::max(0, impl->ui_height - 1));
                const int w = std::clamp(panel.rect_w, 1, impl->ui_width - x);
                const int h = std::clamp(panel.rect_h, 1, impl->ui_height - y);
                quad.subImage.imageRect.offset = { x, impl->ui_height - (y + h) };
                quad.subImage.imageRect.extent = { w, h };
                quad.pose.position = { panel.center[0], panel.center[1], panel.center[2] };
                quad.pose.orientation = { panel.orientation[0], panel.orientation[1], panel.orientation[2],
                    panel.orientation[3] };
                quad.size = { panel.width_m, panel.height_m };
                impl->quads.push_back(quad);
            }
            //! **`push_back` が終わってから**アドレスを採る（途中で再確保が起きる）。
            for (const XrCompositionLayerQuad &quad : impl->quads) {
                impl->layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&quad));
            }
        }
    } else if (impl->ui_acquired_this_frame) {
        //! 絵を積まない回でも、取った画像は必ず返す。
        XrSwapchainImageReleaseInfo release{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        (void)xrReleaseSwapchainImage(impl->ui_swapchain, &release);
        impl->ui_acquired_this_frame = false;
    }

    XrFrameEndInfo end_info{ XR_TYPE_FRAME_END_INFO };
    end_info.displayTime = impl->frame_state.predictedDisplayTime;
    end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end_info.layerCount = static_cast<std::uint32_t>(impl->layers.size());
    end_info.layers = impl->layers.empty() ? nullptr : impl->layers.data();
    (void)xrEndFrame(impl->session, &end_info);
    impl->in_frame = false;
    impl->submit_layer = false;
}

#endif /* HENGBAND_XR_OPENXR */

/* ------------------------------------------------- vr-perf の 1 行ログ（§15 の 4） */

/*
 * **プラットフォームの分岐の外に 1 本だけ置く。**中で触るのはこのクラス自身の
 * 数え札だけで、`Impl` にも OpenXR にも触らない——だから VR の無い実行体
 * （電話版の `:hd2d`）でもそのまま組める（呼ばれないだけ）。
 */
void Session::perf_tick(int eye_w, int eye_h)
{
    //! 5 秒ごとに 1 行。**遊びの邪魔にならない粗さ**で、場面の切り替わりは追える。
    constexpr std::int64_t kWindowNs = 5000000000LL;
    const std::int64_t now = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
    if (this->perf_last_ns_ == 0) {
        //! 区間の最初の 1 本は「起きるまでの待ち」が混ざるので数えない（`--vr-check` と同じ）。
        this->perf_last_ns_ = now;
        this->perf_window_ns_ = now;
        return;
    }
    const double ms = static_cast<double>(now - this->perf_last_ns_) / 1000000.0;
    this->perf_last_ns_ = now;
    if (this->perf_frames_ == 0) {
        this->perf_min_ms_ = ms;
        this->perf_max_ms_ = ms;
    } else {
        this->perf_min_ms_ = std::min(this->perf_min_ms_, ms);
        this->perf_max_ms_ = std::max(this->perf_max_ms_, ms);
    }
    this->perf_sum_ms_ += ms;
    ++this->perf_frames_;
    if ((now - this->perf_window_ns_) < kWindowNs) {
        return;
    }
    /*
     * `xrWaitFrame` はランタイムの表示周期まで待つので、**間隔がそのまま歩調**である
     * （72Hz なら 13.9ms）。`max` が跳ねている区間が「重い場面」で、次の巡はそこを見る。
     */
    std::fprintf(stderr, "[hd2d] vr-perf: wait avg=%.1f min=%.1f max=%.1f ms frames=%d eye=%dx%d\n",
        this->perf_sum_ms_ / static_cast<double>(this->perf_frames_), this->perf_min_ms_, this->perf_max_ms_,
        this->perf_frames_, eye_w, eye_h);
    this->perf_window_ns_ = now;
    this->perf_sum_ms_ = 0.0;
    this->perf_min_ms_ = 0.0;
    this->perf_max_ms_ = 0.0;
    this->perf_frames_ = 0;
}

} // namespace hd2d::xr
