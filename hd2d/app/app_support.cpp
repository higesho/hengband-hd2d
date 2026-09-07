/*!
 * @file app_support.cpp
 * @brief `run()` と検査モードの**両方**が使う土台（窓・GL・画面の保存・町の入口）。
 *
 * @details `hd2d/app/hd2d_app.cpp` から切り出したもの（2026-09-06）。
 * 振る舞いが変わっていないことは `python tools/hd2d_verify/golden.py --check` で見る。
 */
#include "app/app_support.h"

#include "i18n/lang.h"        //!< `i18n::current()`（表示の言語）
#include "render/camera.h"
#include "render/term_colors.h" //!< `term_color_to_rgb()`
#include "render/gl_core.h"
#include "world/prefab_library.h"
#include "frame/cell_feature_bits.h"
#include "frame/game_frame.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace hd2d {

using namespace hd2d::gl; //!< GL の型と関数は `hd2d::gl` に居る（gl_core.h）

namespace {

void HD2D_GLAPI on_gl_debug_message(GLenum, GLenum type, GLuint, GLenum severity,
    GLsizei, const GLchar *message, const void *)
{
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) {
        return; // ドライバの世間話は要らない
    }
    std::fprintf(stderr, "[hd2d][gl] type=0x%04x severity=0x%04x %s\n",
        static_cast<unsigned>(type), static_cast<unsigned>(severity), (message != nullptr) ? message : "");
    (void)std::fflush(stderr);
}

} // namespace

/*!
 * @brief GL 4.6 core のコンテキストを持った窓を作る。
 * @details **代替経路は用意しない。** 4.6 が取れない機械ではこの実行体は成立しない
 * （設計書 §3。旧 HD2D の「1.1 まで落ちる」互換層は引き継がない）ので、
 * 取れなかったら理由を出して素直に終わる。
 */
bool create_window(const AppOptions &options, Window &out, std::string &err)
{
    /*
     * **IME の窓を出させる**（2026-08-13 に気づいた:「Win 版で日本語は通ったが、
     * 変換前、変換中の文字が表示されない。Android 版は問題なく表示された」）。
     *
     * SDL は既定で IME の見た目を**抑止**し、変換中の文字を `SDL_TEXTEDITING` として
     * アプリへ回す（＝「未確定の文字は自分で描け」）。こちらはそれを描いていないので、
     * Windows では変換中の文字がどこにも出ないまま、確定したぶんだけが現れていた。
     * Android で見えていたのは、あちらの IME が**自分の画面の中で**変換を見せるためで、
     * 同じ絵が出ていたわけではない。
     *
     * 未確定の文字は**素の IME に描かせる**。自前で描くとフォント・候補一覧・
     * 変換中の下線まで作ることになり、しかも OS ごとに作法が違う。出る場所は
     * `SDL_SetTextInputRect` で入力欄の桁に寄せる（主ループが毎フレーム渡す）。
     *
     * @note 窓を作る前に立てること。Android では効かない（あちらは元から自前で見せる）。
     */
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
#if defined(HENGBAND_GLES)
    // Android は GLES。
    // 版はコンテキスト生成のところで 3.2 → 3.1 → 3.0 と下げながら試す
    // （エミュレータは 3.1 止まり。シェーダの #version は取れた版から gl_program が選ぶ）。
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
#if defined(_DEBUG)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#endif

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined(__ANDROID__)
    /*
     * **Android は最初からフルスクリーンで作る**（2026-08-12 に気づいた
     * 「上部がスマホ側のノッチ部分に被って見えない部分が出る」）。
     *
     * SDL は窓が**フルスクリーンのときだけ**没入モードにする
     * （`Android_SetWindowFullscreen` → `SDLActivity.setWindowStyle`）。素の窓では
     * 逆に `FLAG_FULLSCREEN` を**外して**ステータスバーを出す（`SDLActivity.java`）ので、
     * 時計と電池が地図の上へ重なっていた。窓の中身は端まで描かれる（テーマが
     * `Theme.NoTitleBar.Fullscreen`）ため、隠れた帯のぶんだけ絵が食われる。
     *
     * `FULLSCREEN` ではなく `FULLSCREEN_DESKTOP`。前者は表示モードの切り替えを伴い、
     * 端末の解像度を選び直す意味がここには無い。
     */
    flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
    if (options.gl_probe_only) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    out.window = SDL_CreateWindow("変愚蛮怒 HD2D (voxel)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, options.window_w, options.window_h, flags);
    if (out.window == nullptr) {
        err = std::string("窓を作れませんでした（OpenGL 4.6 core を要求しています）: ") + SDL_GetError();
        return false;
    }
#if defined(HENGBAND_GLES)
    // 3.2 が第一希望（デバッグ出力と RGBA16F 必須化）。エミュレータ（ES 3.1 上限）や
    // 古めの機で EGL_BAD_CONFIG になるので、3.1 → 3.0 と下げながら試す。
    // 3.2 未満で要る EXT_color_buffer_float（HDR 中間バッファ）は大半の実機と
    // エミュレータが持っている（無ければ post 処理の FBO 検査が理由つきで落ちる）。
    static constexpr int kEsVersions[][2] = { { 3, 2 }, { 3, 1 }, { 3, 0 } };
    for (const auto &v : kEsVersions) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v[0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v[1]);
        out.context = SDL_GL_CreateContext(out.window);
        if (out.context != nullptr) {
            break;
        }
        std::fprintf(stderr, "[hd2d] GLES %d.%d のコンテキストは作れませんでした（次を試します）: %s\n",
            v[0], v[1], SDL_GetError());
    }
    if (out.context == nullptr) {
        err = std::string("OpenGL ES 3.0 のコンテキストすら作れませんでした: ") + SDL_GetError()
            + "\n\nこの端末では動かせません。";
        return false;
    }
#else
    out.context = SDL_GL_CreateContext(out.window);
    if (out.context == nullptr) {
        err = std::string("OpenGL 4.6 core のコンテキストを作れませんでした: ") + SDL_GetError()
            + "\n\nこの実行体は OpenGL 4.6 を必須にしています（設計書 §3）。"
              "\nドライバを更新してください。";
        return false;
    }
#endif
    SDL_GL_MakeCurrent(out.window, out.context);
    SDL_GL_SetSwapInterval(1); //!< vsync。present は 1 フレーム 1 回（設計書 §14-5）

    std::vector<std::string> missing;
    if (!load_gl_functions(missing)) {
        err = "OpenGL の関数を引けませんでした:";
        for (const auto &name : missing) {
            err += "\n  " + name;
        }
        return false;
    }
    for (const auto &name : missing) {
        std::fprintf(stderr, "[hd2d] optional GL entry point missing: %s\n", name.c_str());
    }

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    std::fprintf(stderr, "[hd2d] GL %d.%d | %s | %s | GLSL %s\n", major, minor,
        gl_string(GL_VENDOR).c_str(), gl_string(GL_RENDERER).c_str(), gl_string(GL_SHADING_LANGUAGE_VERSION).c_str());

#if defined(HENGBAND_GLES)
    /*
     * デバッグ出力は ES では 3.2 から。3.1 以下では glDebugMessageCallback の
     * シンボル自体は libGLESv3 から引けてしまう（dlsym）が、enable した瞬間に
     * GL_INVALID_ENUM が残り、次の節目（drain_gl_errors）で初期化失敗に化ける
     * ——エミュレータ（3.1 上限）で実際に踏んだ。
     */
    const bool debug_output_available = (major > 3) || ((major == 3) && (minor >= 2));
#else
    const bool debug_output_available = true;
#endif
    if (debug_output_available && (glDebugMessageCallback != nullptr)) {
        glEnable(GL_DEBUG_OUTPUT);
#if defined(_DEBUG)
        /*
         * 同期にすると壊れた呼び出しの場所がそのまま取れる。**ただし全部の GL 呼び出しが
         * 同期する**ので Release では入れない（P3 のフレーム時間の実測で、これが
         * 効いていないかを確かめるために分けた）。
         */
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif
        glDebugMessageCallback(&on_gl_debug_message, nullptr);
        if (glDebugMessageControl != nullptr) {
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
        }
    }
    return true;
}

//! 表示倍率（§5「1 マス 98〜130 px」）から、カメラまでの水平距離を出す。
float distance_for_cell_px(const Camera &camera, float cell_px)
{
    // 注視点での 1 マスの画面上の大きさ ≒ 幅 / (2 · 視線距離 · tan(水平画角/2))。
    const float view_distance = static_cast<float>(camera.viewport_w) / (2.f * cell_px * camera.tan_half_x());
    return view_distance * std::cos(camera.pitch);
}

/*!
 * @brief いま出来上がっているフレームを BMP で書き出す（`--shot=`）。
 *
 * @details **記憶 `hengband-hd2d-screenshot-is-stale` の罠への備え**である。
 * GL の窓は外部キャプチャ（`CopyFromScreen` / PrintWindow）が黒いまま返ることがあり、
 * 「黒く見えたが実機は正常だった」を過去に踏んでいる。`glReadPixels` で自分の
 * フレームバッファから直接読めば、その系統の疑いが丸ごと消える。
 *
 * @note `SDL_SaveBMP` を使うのは、この exe が `SDL2_image` をリンクしていないから
 * （PNG は要らない。見るのは人と AI で、変換はいくらでも後で効く）。
 * GL は左下原点なので、書き出すときに上下を返す。
 */
bool save_framebuffer_bmp(const std::string &path, int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    SDL_Surface *const surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ABGR8888);
    if (surface == nullptr) {
        std::fprintf(stderr, "[hd2d] SDL_CreateRGBSurfaceWithFormat failed: %s\n", SDL_GetError());
        return false;
    }
    for (int y = 0; y < height; ++y) {
        const unsigned char *const src = pixels.data() + (static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) * 4);
        auto *const dst = static_cast<unsigned char *>(surface->pixels) + (static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch));
        std::memcpy(dst, src, static_cast<std::size_t>(width) * 4);
    }
    const bool ok = SDL_SaveBMP(surface, path.c_str()) == 0;
    if (!ok) {
        std::fprintf(stderr, "[hd2d] SDL_SaveBMP failed: %s\n", SDL_GetError());
    } else {
        std::fprintf(stderr, "[hd2d] wrote %s (%dx%d)\n", path.c_str(), width, height);
    }
    SDL_FreeSurface(surface);
    return ok;
}

/*!
 * @brief **データの入口の印**を作る（`rebuild_town_plan` の (4c)）。
 *
 * @details 見分け方は**地形の対応表がそのマスに看板（`sign`）を持つか**だけである。
 * 表で看板を持つのは店（`GENERAL_STORE` ほか 10 種）と施設（`BUILDING_0`〜`BUILDING_31`）
 * だけなので、**コアも町も選ばない**——`terrain_id` の意味は対応表が知っている。
 *
 * 町の読み取りは壁の塊の形から門を決めるので、塊が敷地にならないと戸口が門にならず、
 * アーチも看板も立たなかった（Frox のアナンバール 13 か所ほか）。この印があるマスは
 * 形が何であれ門になる（2026-09-06 に決めた）。
 */
std::vector<std::uint8_t> town_entrance_mask(const GameFrame &frame, const PrefabLibrary &library)
{
    std::vector<std::uint8_t> mask;
    const int w = frame.minimap.width;
    const int h = frame.minimap.height;
    if ((w <= 0) || (h <= 0) || !library.ready()) {
        return mask;
    }
    mask.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0u);
    for (const auto &cell : frame.cells) {
        if ((cell.gx < 0) || (cell.gy < 0) || (cell.gx >= w) || (cell.gy >= h)) {
            continue;
        }
        if (cell.terrain_id == 0) {
            continue;
        }
        const TerrainRule *const rule = library.rule_for_terrain(cell.terrain_id, true);
        if ((rule != nullptr) && !rule->sign.empty()) {
            mask[(static_cast<std::size_t>(cell.gy) * static_cast<std::size_t>(w))
                + static_cast<std::size_t>(cell.gx)]
                = 1u;
        }
    }
    return mask;
}

/*!
 * @brief 壁に隠れている実体のぶんも抜く（P10 第 4 期。2026-08-10 に気づいた）。
 *
 * > 視界の範囲内にあるモンスター、アイテムが壁ブロック等の 1 マス北にある場合
 * > （隠れて見えない場合）は透過して見えるように。
 *
 * @details 判定はプレイヤのカットアウェイと**同じ関数**（`line_of_sight_blocked`）で行う。
 * カメラは南から見下ろすので、実体の 1 つ南に高い壁があると視線が切れる——「1 マス北」は
 * その一例で、地形の高さから決まる一般の遮蔽として扱えばよい（斜めや 2 マス先の塔も拾える）。
 *
 * 穴はプレイヤのものより**小さくする**。実体は 1 マスなので、同じ半径で抜くと
 * 部屋の壁が丸ごと消える。
 *
 * @param out 先頭にプレイヤのぶんが入っている前提で**後ろへ足す**。
 * `VoxelRenderer::kMaxCutaways` を超えたら足さない（近い順に並べてはいない
 * ——実体は数が少ないので、そこまでの作り込みは後回しにした）。
 */
void append_entity_cutaways(const Camera &camera, const TerrainView &terrain,
    const EntityView &entities, float radius, std::vector<Cutaway> &out, bool camera_side_only)
{
    if (radius <= 0.f) {
        return;
    }
    const Vec3 eye = camera.eye();
    /*
     * 1 体ぶんの穴。**板でも文字でも同じ扱い**——
     * 抜くのは「実体を隠している壁」のほうなので、実体の描き方には依らない。
     * ここを板の列だけで回していると、**アスキー実体では壁の裏の敵が見えなくなる。**
     */
    const auto add = [&](float cx, float cy, float mid_z) {
        if (out.size() >= VoxelRenderer::kMaxCutaways) {
            return;
        }
        const int gx = static_cast<int>(std::floor(cx));
        const int gy = static_cast<int>(std::floor(cy));
        if (!line_of_sight_blocked(terrain, eye, gx, gy)) {
            return; // 見えている。抜く必要は無い
        }
        //! 実体の**中ほど**を狙う（足元だと穴が下へずれて、頭が壁に残る）。
        const Cutaway cut = make_point_cutaway(camera, Vec3{ cx, cy, mid_z }, radius, camera_side_only);
        if (cut.radius > 0.f) {
            out.push_back(cut);
        }
    };
    for (const SlabInstance &entity : entities.slabs) {
        add(entity.cx, entity.cy, entity.inst.z + (entity.height * 0.5f));
    }
    for (const BillboardInstance &glyph : entities.glyphs) {
        add(glyph.x, glyph.y, glyph.z + (glyph.height * 0.5f));
    }
}

/*!
 * @brief いまのカメラから `ui_state` を組む（v1 §7）。
 * @details 可視窓の導出は `derive_view_window()`（§14-1 の台形・§14-2 の非対称の罠はそちら）。
 * ここは**その結果をプロトコルの上限で切る**だけ。切ったことは呼び出し側が
 * 画面に出す（黙って狭めると奥が欠けたまま気づけない）。
 */
presentation::UiStateMessage build_ui_state(
    const ViewWindow &window, const UiLayout &layout, const Hd2dSettings &settings, bool screen_audio)
{
    presentation::UiStateMessage state;
    /*
     * リアルタイム進行。**設定はこちら、時計はコア側**。
     * ここで送らないと、メニューの値だけ変わって何も起きない。
     */
    state.realtime_enabled = settings.realtime_enabled;
    state.realtime_speed_index = settings.realtime_speed_index;
    state.realtime_prompt_live = settings.realtime_prompt_live;
    state.realtime_self_span_index = settings.realtime_self_span_index;
    /*
     * 音（2026-08-21 に決めた）。**設定はこちら、鳴らすのはコア側**。同じく
     * ここで送らないと、メニューの値だけ変わって何も起きない。
     *
     * **音量 0 は入切のほうを落として届ける。** プロトコルの音量は 10 段で
     * （`SdlUiOptions::kVolumeLevels`。添字 0 が 100%・9 が 10%）**0% の段が無い**ので、
     * 「入のままつまみを 0 まで絞った」は「切」として伝えるしかない。設定の側は
     * 入のまま・段は 0 のままなので、上げれば元の入へ戻る。
     */
    /*
     * **コアが曲を鳴らすのは `Music` のときだけ。**`Ambience` では画面側がベッドを鳴らすので、
     * ここで切っておかないと**両方が同時に鳴る**。
     */
    state.music_on = (settings.bgm_mode == Hd2dSettings::BgmMode::Music) && (settings.music_volume > 0);
    state.sound_on = settings.sound_enabled && (settings.sound_volume > 0);
    /*
     * **効果音を誰が鳴らすか**。画面側の装置が開けていれば
     * こちらが鳴らす——マスで定位できるのは画面側だけだから。開けていなければ（音の装置が
     * 無い機・Android の空実装）**コアに任せる**。切り替えは自動で、設定の項目は増やさない。
     * @note `sound_on` は「そもそも効果音を鳴らすか」で、こことは別。コアの `sound()` は
     * `use_sound` が偽だと `Term_xtra` すら呼ばないので、**切ると出来事ごと消える**。
     */
    state.sound_events = screen_audio && state.sound_on;
    state.music_volume_index = Hd2dSettings::volume_step_to_index(settings.music_volume);
    state.sound_volume_index = Hd2dSettings::volume_step_to_index(settings.sound_volume);
    /*
     * コア側の言語（実行時多言語化の第 5 段）。**画面のつまみをコアまで届かせる。**
     * 機能メニューで言語を変えると次の `ui_state` で降りていき、コアの文言も切り替わる
     * （いま切り替わるのはコア側で `_F()` を通した文言だけ）。
     */
    state.lang = std::string(i18n::current());
    state.view_w = std::clamp(window.cols, 20, kMaxViewW);
    state.view_h = std::clamp(window.rows, 10, kMaxViewH);
    /*
     * サブパネル 1 枚に入る**文字のマスの数**（v1 §7。K-26 相当）。
     * コアの表示関数は Term の大きさを見て行数・桁数を決めるので、実寸を渡さないと
     * 行が折れる／余る。**見出し 1 行ぶんを差し引く**（描く側が種類名に 1 行使う）。
     * 0 は「申告なし」なので、置いていない枚は 0 のままでよい。
     */
    /*
     * どのサブウインドウを映すか（v1 §7）。**送らないとコア側の設定が残る**ので、
     * 機械を変えると別のものが出る。この exe の既定を明示して、どこでも同じ並びにする。
     */
    state.has_sub_panel_kinds = true;
    for (int i = 0; i < presentation::kProtocolSubPanelCount; ++i) {
        state.sub_panel_kinds[i] = settings.sub_panel_kind[i];
    }
    for (int i = 0; i < presentation::kProtocolSubPanelCount; ++i) {
        const RectPx &area = layout.sub[i];
        if (area.empty() || (layout.cell_w <= 0) || (layout.cell_h <= 0)) {
            continue;
        }
        const int pad = std::max(2, layout.cell_w / 2) * 2;
        const int cols = (area.w - pad) / layout.cell_w;
        const int rows = ((area.h - pad) / layout.cell_h) - 1;
        if ((cols < 8) || (rows < 2)) {
            continue; // 狭すぎる枠に「入る」と申告しない
        }
        state.sub_panel_cells[i].cols = cols;
        state.sub_panel_cells[i].rows = rows;
    }
    state.camera_follow_player = true; //!< フロア端でプレイヤが泳がないように（設計書 §14-3）
    /*!
     * P8 でカーソル操作が入った。**これを立てるとコアが `use_menu` / `command_menu` を持つ**
     * （`presentation_bridge.cpp::apply_cursor_mode`）ので、コマンドメニュー・呪文・持ち物は
     * **コアが自分のカーソル（`》`）を動かす**。UI はその位置に枠を描くだけでよい。
     * 店と建物はコアにカーソルの仕組みが無いので、そこだけ `UiCursors` が受け持つ。
     */
    state.cursor_mode = true;
    state.map_style_graf_px = 0; //!< タイル面は使わない（P4 で既存スプライトを直接読む）
    state.map_style_graf_tag = "ascii";
    return state;
}

/*!
 * @brief 溶岩・発光地形の「**面として光る**床」を集める（P7。積み残し #6）。
 *
 * @details P5 で溶岩は**点光源としては**効くようになったが、面は暗いままだった
 * （溶岩の池のそばは明るいのに、池そのものが黒い）。ここがブルームの相手でもある:
 * トーンマップ後の値では「白い床」と「燃えている溶岩」が区別できないので、
 * **1 を超える線形の値**を自発光として持たせる。
 *
 * @note 意味づけ層（`FloorMeaning`）はミニマップ由来で地形の性質を持たないので、
 * ここは**可視窓のマス（`frame.cells`）から拾う**（`collect_point_lights()` と同じ理由・同じ元）。
 * 画面の外の溶岩は光らなくてよい。
 *
 * @note 床の板の上に **0.01 マスだけ浮かせる**。通路の床は種でわずかに沈めてあるので
 * （`terrain_view.cpp`）、同じ高さに置くと深度が競って面がちらつく。
 *
 * @param glow_scale 発光地形（`CELL_FEAT_GLOW`）の強さに掛ける。**溶岩には掛けない。**
 * @note 地上では `lamp_night_factor()` を渡す（P10 第 2 期）。店の入口は地形に `GLOW` を
 * 持っているので、掛けないと**真昼の店先に白く飛んだ四角が出る**（実機の絵で見つけた）。
 * 灯りは夜に点くものなので、昼は 0 にするのが正しい。地下は常に 1（洞窟に昼夜は無い）。
 */
void collect_emissive_slabs(const GameFrame &frame, std::vector<InstanceData> &out, bool skip_lava,
    float glow_scale)
{
    out.clear();
    for (const auto &cell : frame.cells) {
        if ((cell.feature_flags & CELL_FEAT_KNOWN) == 0u) {
            continue;
        }
        const bool lava = (cell.feature_flags & CELL_FEAT_LAVA) != 0u;
        const bool glow = (cell.feature_flags & CELL_FEAT_GLOW) != 0u;
        if (!lava && !glow) {
            continue;
        }
        if (!lava && (glow_scale <= 0.01f)) {
            continue; // 昼の地上。灯りは点いていない
        }
        if (lava && skip_lava) {
            /*
             * P10: ライブラリが生きているとき、溶岩は `lava_crust`（皮と亀裂の模様つき）が
             * 自発光ごと置かれる。この平板を上へ重ねると模様が塗り潰されるので出さない。
             * **発光地形（GLOW）はまだ素材が無い**ので、従来どおりここで光らせる。
             */
            continue;
        }
        InstanceData slab;
        slab.x = static_cast<float>(cell.gx);
        slab.y = static_cast<float>(cell.gy);
        slab.z = 0.01f;
        // 下地の色。自発光と同系にしておくと、光が弱い場所でも溶岩に見える。
        slab.r = lava ? 0.55f : 0.70f;
        slab.g = lava ? 0.20f : 0.66f;
        slab.b = lava ? 0.10f : 0.52f;
        // **1 を超える値。**ここが閾値を越えないとブルームは掴めない。
        const float scale = lava ? 1.f : glow_scale;
        slab.er = (lava ? 2.10f : 1.15f) * scale;
        slab.eg = (lava ? 0.72f : 1.02f) * scale;
        slab.eb = (lava ? 0.22f : 0.74f) * scale;
        out.push_back(slab);
    }
}

/*!
 * @brief **決定（A / Enter）でコマンドメニューを開いてよいか**（2026-08-23 に決めた）。
 *
 * @details 「A ボタンに、決定と同じく既定でコマンドメニューを置きたい。コアは改変しない。
 * 中間層で対応できないか」への答え。変愚では `\r` を受けたコアが自分の通常メニューを開く
 * （`src/io/input-key-requester.cpp:117`）が、Sil-Q にその仕組みは無いので画面側が開く。
 *
 * **いつ開いてよいかはコアが言う**（`frame.awaiting_command`）。画面が「遊んでいる最中か」を
 * 自分で決めると `-more-` の待ちでも開いてしまい、メッセージが進まなくなる。
 * 旗を立てないコア（変愚・短愚・幻想）では常に偽——**あちらの動きは 1 ビットも変わらない**。
 *
 * 文字を打っている最中は開かない（打ち終わりの Enter を奪わない）。
 *
 * @note **規則はここ 1 か所**。パッドの A とキーボードの Enter が別々に判断すると、
 * どちらか片方だけ直された状態になる（`--ui-check` の (4''') もここを見る）。
 */
bool confirm_opens_command_menu(const GameFrame &frame)
{
    return frame.awaiting_command && !frame.text_input_active;
}

/*!
 * @brief カットアウェイの**半平面の法線**（水平面内で「対象 → カメラ」の向き）。
 *
 * @details `Cutaway::face_x/face_y` の注記のとおり、透過してよいのは
 * **カメラと対象の間**にあるものだけである。カメラの水平な視線は `yaw` だけで決まり
 * （`Camera::forward()` の水平成分 = `(sin yaw, −cos yaw)`）、その逆向きが
 * 「対象からカメラを見る向き」になる。
 *
 * **`yaw = 0` では厳密に `(0, 1)`＝南**である（`sin 0 == 0` / `cos 0 == 1` は厳密）。
 * つまり回していないときは「南だけ抜く」という従来の判定と 1 ビットも違わない。
 *
 * @note `camera.eye() − at` から作ってはいけない。注視点はプレイヤの**丸めない**位置に
 * 追従するので（§14-3）、対象のマス中心とわずかにずれ、`yaw = 0` でも法線に
 * 小さな x 成分が出る。それだけで「回していないのに絵が変わった」になる。
 */
void cutaway_face_dir(const Camera &camera, float &face_x, float &face_y)
{
    face_x = -std::sin(camera.yaw);
    face_y = std::cos(camera.yaw);
}

/*!
 * @brief フレームを**文字で**出す（P0 ⑤ の名残。いまは `--text` の中身）。
 *
 * @details P0 では「これが画面」だったが、P8 で本物の UI が入ったので**調べるための重ね書き**
 * になった。残しているのは、コアが送ってきたものと 3D で出ているものを**目で突き合わせる**
 * 場所がここしか無いからである（`cells` の `ascii_fallback` を格子に置く）。
 *
 * @note **Term の写しとメッセージはもう描かない。**そちらは `ui/game_hud.cpp` が持っている。
 * 2 か所から同じものを描くと、片方だけ直された状態に必ずなる（§14-13）。
 */
void draw_frame_as_text(TextOverlay &text, const GameFrame &frame, const std::string &title_line,
    int screen_w, int screen_h, double fps, std::size_t frame_bytes, bool ascii_map)
{
    const int cw = text.cell_w();
    const int ch = text.cell_h();
    const int margin = 8;

    int row = 0;
    text.draw_cell(margin, margin, 0, row++, title_line, kHeaderColor);

    char buf[256]{};
    std::snprintf(buf, sizeof(buf), "frame=%llu  bytes=%zu  fps=%.1f  cells=%zu  view=%dx%d  cam=(%d,%d)  player=(%d,%d)",
        static_cast<unsigned long long>(frame.frame_id), frame_bytes, fps, frame.cells.size(),
        frame.view_w, frame.view_h, frame.cam_x, frame.cam_y, frame.player_gx, frame.player_gy);
    text.draw_cell(margin, margin, 0, row++, buf, kHeaderColor);

    std::snprintf(buf, sizeof(buf), "%s  HP %d/%d  SP %d/%d  AU %d  LV %d  DL %d%s%s%s",
        frame.hud.name.c_str(), frame.hud.hp, frame.hud.hp_max, frame.hud.sp, frame.hud.sp_max,
        frame.hud.gold, frame.hud.level, frame.hud.depth,
        frame.title_screen ? "  [title]" : "", frame.menu_open ? "  [menu]" : "",
        frame.text_input_active ? "  [text-input]" : "");
    text.draw_cell(margin, margin, 0, row++, buf, kDimColor);

    text.draw_cell(margin, margin, 0, row++,
        "[P0] 矢印/テンキー=移動  Enter=決定  ESC=取消  文字キーはそのまま  Alt+F4/×=終了", kDimColor);
    ++row;

    const int body_y = margin + (row * ch);
    // 下段（メッセージ）に空けておく行数。
    const int footer_rows = 4;
    const int body_rows = std::max(1, ((screen_h - body_y - margin) / ch) - footer_rows);

    if (ascii_map && frame.menu_term_lines.empty()) {
        /*
         * --- 可視窓の cells を格子に置く ---
         * **窓の原点は `cam - view/2`**（`presentation_bridge.cpp` の `ox = px - view_w/2` /
         * `cam_x = ox + view_w/2`）。`cam` は窓の左上ではなく**中心**である。
         * P0 では `cam` を左上として引いていたため、プレイヤが常に左上隅に出て
         * 地図の西半分が画面の外にあった（P2 で直した）。
         */
        const int origin_gx = frame.cam_x - (frame.view_w / 2);
        const int origin_gy = frame.cam_y - (frame.view_h / 2);
        const int cols = std::max(1, (screen_w - (margin * 2)) / cw);
        for (const auto &cell : frame.cells) {
            const int col = cell.gx - origin_gx;
            const int line = cell.gy - origin_gy;
            if ((col < 0) || (col >= cols) || (line < 0) || (line >= body_rows)) {
                continue;
            }
            const char glyph = (cell.ascii_fallback != '\0') ? cell.ascii_fallback : ' ';
            if (glyph == ' ') {
                continue;
            }
            const char one[2] = { glyph, '\0' };
            text.draw(margin + (col * cw), body_y + (line * ch), one, from_term_color(cell.fg_color));
        }
    }
}

/*!
 * @brief ライブラリのバケットを本描画へ流す（P10）。
 * @details 2 巡に分ける: **地面（`is_ground`）はカットアウェイを掛けない**（掛けると
 * 手前の地面に穴が開く。`Cutaway` の約束 2）。立つものは掛ける。
 * 戻るときカットアウェイの倍率は 1 のままである（呼び手が次の描画で設定し直すこと）。
 */
void draw_library_color(VoxelRenderer &renderer, PrefabLibrary &library, const TerrainView &terrain)
{
    /*
     * **ライブラリの置き場所が無ければ何もしない。**`TerrainView::lib` はライブラリを渡して組んだときだけ
     * 埋まる。TRON 画調（`blocks_only`）はライブラリを引かないので**空のまま**来る——
     * ここで守らないと `lib[i]` が範囲外を読んで落ちる（実際に落ちた）。
     */
    if (terrain.lib.size() != static_cast<std::size_t>(library.count())) {
        return;
    }
    renderer.set_cutaway_scale(0.f);
    for (int i = 0; i < library.count(); ++i) {
        const auto &bucket = terrain.lib[static_cast<std::size_t>(i)];
        LibraryEntry &entry = library.entry_mut(i);
        if (bucket.empty() || !entry.gpu_ready || !entry.is_ground) {
            continue;
        }
        renderer.draw_instanced(entry.gpu, bucket.data(), bucket.size(),
            library.motions_of(i), library.motion_count_of(i));
    }
    renderer.set_cutaway_scale(1.f);
    for (int i = 0; i < library.count(); ++i) {
        const auto &bucket = terrain.lib[static_cast<std::size_t>(i)];
        LibraryEntry &entry = library.entry_mut(i);
        if (bucket.empty() || !entry.gpu_ready || entry.is_ground) {
            continue;
        }
        renderer.draw_instanced(entry.gpu, bucket.data(), bucket.size(),
            library.motions_of(i), library.motion_count_of(i));
    }
}

/*!
 * @brief ライブラリのバケットを影のパスへ流す（P10）。
 * @details 動きの行列は `PrefabLibrary::update_motions()` が作った**同じもの**を
 * 本描画も読む（§8.2-1「影のパスにも同じ変形」。2 回作ると位相がずれる）。
 */
void draw_library_depth(VoxelRenderer &renderer, PrefabLibrary &library, const TerrainView &terrain,
    const Mat4 &light_view_projection)
{
    //! **ライブラリの置き場所が無ければ何もしない**（`draw_library_color` と同じ理由）。
    if (terrain.lib.size() != static_cast<std::size_t>(library.count())) {
        return;
    }
    for (int i = 0; i < library.count(); ++i) {
        const auto &bucket = terrain.lib[static_cast<std::size_t>(i)];
        LibraryEntry &entry = library.entry_mut(i);
        if (bucket.empty() || !entry.gpu_ready) {
            continue;
        }
        renderer.draw_instanced_depth(entry.gpu, light_view_projection, bucket.data(), bucket.size(),
            library.motions_of(i), library.motion_count_of(i));
    }
}

/*!
 * @brief `HD2D_FORCE_TIME=<時:分 または 分>` — 時刻を上書きする（P5 の見え方を詰めるため）。
 *
 * @details 昼夜は**単項目あたりの効果が最大**（設計書 §12-4）だが、実際に日没を見るには
 * ゲーム内で半日歩かねばならない。夕焼けの色を 1 回直すたびに半日というのは通らないので、
 * 外から時刻を差し込める口を開けておく。**コアには一切影響しない**（描く側だけが嘘をつく）。
 *
 * @return 分（0〜1439）。指定が無ければ負値。
 */
int forced_day_minute()
{
    const char *const raw = std::getenv("HD2D_FORCE_TIME");
    if ((raw == nullptr) || (raw[0] == '\0')) {
        return -1;
    }
    int a = 0;
    int b = -1;
    const int fields = std::sscanf(raw, "%d:%d", &a, &b);
    if (fields <= 0) {
        return -1;
    }
    const int minutes = (fields >= 2) ? ((a * 60) + b) : a;
    return ((minutes % 1440) + 1440) % 1440;
}

TextColor from_term_color(std::uint8_t index, float alpha)
{
    const RgbColor rgb = term_color_to_rgb(index);
    return TextColor{ static_cast<float>(rgb.r) / 255.f, static_cast<float>(rgb.g) / 255.f, static_cast<float>(rgb.b) / 255.f, alpha };
}

/*!
 * @brief ポスト処理を用意する。**失敗は起動の失敗**（トーンマップがここにしか無い）。
 * @details P5 の影は「無くても遊べる」ので失敗しても続けたが、こちらは無いと
 * 線形の HDR がそのまま画面へ出て白く飛ぶ。続ける意味が無い。
 */
bool init_post_chain(PostChain &post, const AppOptions &options, std::string &err)
{
    if (!post.init(err)) {
        return false;
    }
    if (!options.lut_path.empty() && !post.load_lut_cube(options.lut_path, err)) {
        return false;
    }
    if (!options.lut_path.empty()) {
        std::fprintf(stderr, "[hd2d] LUT: %s\n", post.lut_source().c_str());
    }
    return true;
}

/*!
 * @brief 灯りを点ける度合い（0 = 昼 / 1 = 夜）。町の窓と街灯の強さに掛ける（P10 第 2 期）。
 *
 * @details `LightingState::daytime` の 2 値では**日没の瞬間に町じゅうの窓が一斉に点く。**
 * 光の色（`sun_colors()`）と同じで、階段状に飛ぶものは絵にならない（罠 10 と同じ形）。
 * 17:00 から点り始めて 18:30 に満、5:00 から消え始めて 6:30 に消える。
 * @note 太陽の高度からではなく時刻から引く。高度は緯度と季節の式が要るのに対し、
 * ここで欲しいのは「人が灯りを点ける時刻」なので暦のほうが素直である。
 */
float lamp_night_factor(const LightingState &light)
{
    const auto ramp = [](float x, float a, float b) { return std::clamp((x - a) / (b - a), 0.f, 1.f); };
    const auto minute = static_cast<float>(light.day_minute);
    const float evening = ramp(minute, 17.f * 60.f, 18.5f * 60.f);
    const float morning = 1.f - ramp(minute, 5.f * 60.f, 6.5f * 60.f);
    return std::max(evening, morning);
}

/*!
 * @brief プレイヤの位置からカットアウェイの中心と深度を作る（P7・設計書 §13）。
 *
 * @param radius 抜く半径（画素）。**0 なら無効な `Cutaway` を返す。**
 *
 * @details 基準にするのは**腰の高さ**（0.55 マス）である。足元（z = 0）にすると
 * 「プレイヤより手前」の判定が地面すれすれで起き、抜ける範囲が下半分に偏る。
 *
 * 深度は**ここで投影して作る。**深度バッファのその画素に入っているのは
 * 「プレイヤを隠している壁」の深度なので、それを基準にすると壁は自分より手前に無いことになり、
 * 判定が永久に空振りする（P7 で最初にそう書きかけた）。
 */
Cutaway make_player_cutaway(const Camera &camera, const GameFrame &frame, float radius,
    bool camera_side_only)
{
    /*
     * 抜くのは**カメラとプレイヤの間**にあるものだけ（2026-08-11 に決めた:「真横の
     * オブジェクトを透過しない」／2026-08-19:「透過はカメラとキャラの間に障害がある場合として
     * 変更しよう」）。境と余白は `make_point_cutaway` が持つ。
     * 検査（`--cutaway-check`）は半平面を掛けずに従来の円で見る。
     */
    //! **腰の高さ**を狙う。足元（z=0）にすると抜ける範囲がプレイヤの下半分に偏る。
    return make_point_cutaway(camera,
        Vec3{ static_cast<float>(frame.player_gx) + 0.5f,
            static_cast<float>(frame.player_gy) + 0.5f, 0.55f },
        radius, camera_side_only);
}

/*!
 * @brief 世界の 1 点を抜く（`Cutaway` を 1 つ作る）。カメラの後ろなら半径 0 で返る。
 * @param camera_side_only 真なら**カメラと対象の間**にあるものだけ抜く（`Cutaway::face_x`）。
 *   偽なら半平面の判定を掛けない（従来の円そのもの。検査と屋根外しはこちら）。
 */
Cutaway make_point_cutaway(const Camera &camera, const Vec3 &at, float radius, bool camera_side_only)
{
    Cutaway out;
    if (radius <= 0.f) {
        return out; // 半径 0 ＝ 無効
    }
    float sx = 0.f;
    float sy = 0.f;
    if (!camera.project(at, sx, sy)) {
        return out; // カメラの後ろ（起こらないはずだが、起きたら抜かない）
    }
    out.centre_x = sx;
    // **`gl_FragCoord` は左下原点**、`Camera::project` は左上原点。ここで返す。
    out.centre_y = static_cast<float>(camera.viewport_h) - sy;
    out.radius = radius;
    if (camera_side_only) {
        cutaway_face_dir(camera, out.face_x, out.face_y);
        /*
         * 境は**対象のマスの、カメラ側の辺**である。0.05 マスの余白は、ちょうど境に乗る
         * 真横の壁の面が数値誤差でカメラ側へ数えられないための保険。
         *
         * 0.55 = マス中心から辺まで 0.5 ＋ 余白 0.05。`yaw = 0` では
         * `face = (0,1)` なので `face_min = at.y + 0.55`＝**従来の `gy + 1.05` と同じ値**になる
         * （`at.y` はマス中心 `gy + 0.5`）。
         */
        out.face_min = (at.x * out.face_x) + (at.y * out.face_y) + 0.55f;
    }
    out.depth = window_depth_of(camera, at);
    return out;
}

/*!
 * @brief 効かせるポスト処理を決める（P7）。**`--post=` が `HD2D_POST=` より強い。**
 * @details 綴りが読めなかったら**黙って既定へ落とさない**。`--windowed=` で
 * 一度それをやって「なぜ大きいのか」を考える羽目になった（`hd2d_main.cpp` の注記）。
 */
PostFlags resolve_post_flags(const AppOptions &options)
{
    PostFlags flags;
    std::string spec = options.post_spec;
    const char *const env = std::getenv("HD2D_POST");
    if (spec.empty() && (env != nullptr)) {
        spec = env;
    }
    if (spec.empty()) {
        return flags;
    }
    std::string err;
    if (!PostFlags::parse(spec, flags, err)) {
        std::fprintf(stderr, "[hd2d] %s。全部入りのままにします\n", err.c_str());
        return PostFlags{};
    }
    std::fprintf(stderr, "[hd2d] post: %s\n", flags.to_line().c_str());
    return flags;
}

/*!
 * @brief `SDL_ShowSimpleMessageBox` は UTF-8 を受ける（この TU の実行文字コードと同じ）。
 */
void show_message(const std::string &text)
{
    std::fprintf(stderr, "[hd2d] %s\n", text.c_str());
    (void)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "変愚蛮怒 HD2D", text.c_str(), nullptr);
}

/*!
 * @brief 世界の点を**窓深度**（[0,1]）へ写す。
 * @details `Camera::project()` は画素だけを返すので、深度が要るここで別に計算する。
 * `math3d.h` は `Mat4 × Vec4` を持たない（要らなかったので）ので、その場で展開する。
 */
float window_depth_of(const Camera &camera, const Vec3 &world)
{
    const Mat4 vp = camera.view_projection();
    const float z = (vp.m[2] * world.x) + (vp.m[6] * world.y) + (vp.m[10] * world.z) + vp.m[14];
    const float w = (vp.m[3] * world.x) + (vp.m[7] * world.y) + (vp.m[11] * world.z) + vp.m[15];
    if (std::abs(w) < 1e-6f) {
        return 1.f;
    }
    return std::clamp(((z / w) * 0.5f) + 0.5f, 0.f, 1.f);
}

} // namespace hd2d
