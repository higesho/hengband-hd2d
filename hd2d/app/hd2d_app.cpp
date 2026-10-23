#if defined(HENGBAND_DESKTOP_LINUX)
#include <unistd.h>
#endif
/*!
 * @file hd2d_app.cpp
 * @brief `hd2d_app.h` の実装。**P0（土台と接続）**。
 *
 * ## いまここで確かめていること
 * 1. GL 4.6 core のコンテキストが取れる
 * 2. コアと握手できる
 * 3. **キーが通ってプレイヤが動く**
 * 4. 正常に終われる
 *
 * 画面に出しているのは**フレームの中身をそのまま文字にしたもの**である。
 * 絵の検証ではない。ボクセルもカメラも P1 以降で、ここには 1 行も無い。
 *
 * ## 罠（設計書 §14 から、この段で効くもの）
 * - present は 1 フレーム 1 回（§14-5）。`SDL_GL_SwapWindow` はループの末尾に 1 つだけ
 * - 全画面 GL は外部キャプチャが止まる（§14-7）。**P0 の窓は既定でウィンドウモード**にしてある
 */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX //!< これが無いと windows.h の min/max マクロが `std::min` を壊す
#endif
#include <windows.h>
#endif

#include "app/hd2d_app.h"
#include "ui/menu_frame.h"
#include "ui/menu_scale.h"
#include "app/hd2d_checks.h" //!< 検査モード（--*-check）。中身は hd2d/app/checks/
#include "app/app_clock.h" //!< 時計。検査のときだけ進み方を決め打ちにできる
#include "app/run_parts.h"
#include "app/input_controller.h"
#include "app/test_keyboard.h"
#include "app/ui_probe.h"
#include "frame/message_more.h"
#include "app/core_import_ui.h"
#include "app/lineage_ui.h"
#include "app/video_recorder.h" //!< 画面内録画（HD2D_RECORD）
#include "app/optional_render_resources.h" //!< run() から切り出した部品
#include "app/app_support.h" //!< 遊ぶ経路と検査の両方が使う土台（窓・GL・画面の保存）
#include "app/checks/check_support.h" //!< 検査どうしで使い回す作り物
#include "app/checks/targeting_live_trace.h"

#include "world/slab_library.h"
#include "assets/tile_catalog.h"
#include "i18n/lang.h"
#include "net/core_link.h"
#include "dcss/dcss_ui.h" //!< DCSS（`dcss-ui` を申告したコアだけ。段 U1）
#include "render/billboard_renderer.h"
#include "render/glyph_atlas.h"
#include "render/ground_ring.h"
#include "render/light_shaft.h"
#include "render/camera.h"
#include "render/cloud_layer.h"
#include "render/dust_motes.h" //!< 空中に浮かぶ埃
#include "render/weather_fx.h" //!< 天候（雨・雪・風・日中の光。コアが申告したときだけ）
#include "render/gl_core.h"
#include "render/lighting.h"
#include "render/math3d.h"
#include "render/post_process.h"
#include "render/render_view.h" //!< 「1 回ぶんの視点」。フラットは 1 個・VR は左右で 2 個
#include "render/shadow_map.h"
#include "render/sky_dome.h"
#include "render/term_colors.h"
#include "render/text_overlay.h"
#include "render/voxel_renderer.h"
#include "voxel/greedy_mesher.h"
#include "voxel/part_motion.h"
#include "voxel/prefab.h"
#include "ui/click_path.h"
#include "ui/diagonal_only.h"
#include "ui/feature_menu.h"
#include "audio/ambience_mix.h"
#include "audio/ambience_table.h"
#include "audio/sfx_catalog.h"
#include "audio/audio_engine.h"
#include "ui/floor_cutin.h"
#include "ui/fps_mode.h"
#include "ui/game_hud.h"
#include "ui/game_pad.h"
#include "ui/ui_image.h"
#include "ui/hd2d_settings.h"
#include "ui/ui_layout.h"
#include "ui/ui_paint.h"
#include "ui/virtual_pad.h"
#if defined(__ANDROID__)
#include "android/android_safe_area.h" //!< 切り欠き（ノッチ）を避けた範囲を採る
#endif
#include "xr/xr_fake.h" //!< 疑似 HMD（`--vr-fake`）。頭が無くても両眼の絵を撮れる
#include "xr/xr_input.h" //!< Touch → PadInput（既存の割り当てへ流し込むだけ）
#include "xr/xr_math.h" //!< VR の座標写像（純関数。頭が無くても回せる）
#include "xr/xr_room.h" //!< VR の部屋と卓（§20）。疑似 HMD の板もここが描く
#include "xr/xr_session.h" //!< VR。既定 OFF・`--vr` で入る
#include "world/dungeon_style.h"
#include "world/entity_view.h"
#include "model/model_asset.h"
#include "app/model_view.h"
#include "world/overlay_view.h"
#include "world/floor_meaning.h"
#include "world/prefab_library.h"
#include "world/terrain_memory.h"
#include "world/terrain_view.h"

#if defined(HD2D_EDITOR)
// P9 のボクセルエディタ。リリース用ビルド（`HengbandHd2dEditor=0`）にはこの 2 つの TU ごと入らない。
#include "edit/edit_check.h"
//! `--slab-ladder-check` が板を**書いて読み戻す**のに使う（書く側はここにしかない）。
#include "edit/prefab_writer.h"
#include "edit/voxel_editor.h"
#endif

#include "frame/cell_feature_bits.h"
#include "frame/minimap_snapshot.h"

#include "frame/frame_codec.h"
#include "frame/game_frame.h"
#include "frame/protocol_messages.h"
#include "ui/combat_fx_view.h"
#include "render/attack_fx_view.h"
#include "render/fx_styles/fx_parts.h"
#include "render/voxel_fx_renderer.h"
#include "ui/attack_fx_overlay.h"
#include "world/entity_fx_mod.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <array> //!< `--prefab-check=` の材質の内訳
#include <atomic> //!< ライブラリの読み込みを別スレッドで回す（コア選択の後の演出。2026-08-23）
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <exception> //!< 壊れた cfg のパスで `std::filesystem::path` が投げる（`core_is_available`）
#include <filesystem> //!< コア選択画面が「隣に在る exe」を見る（設計 §6.1）
#include <fstream> //!< 検査が無音の wav を 1 本書く（`write_silent_wav`）
#include <functional> //!< コア選択画面へ「決めたら呼ぶ」「まだ読んでいるか」を渡す
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <thread> //!< 同上（画の裏でライブラリを読む）
#include <vector>

namespace hd2d {

using namespace hd2d::gl;

namespace {

//! 駆動ループの目標周期（およそ 60fps）。
constexpr Uint32 kFrameIntervalMs = 16;
//! `quit_request` を送ってからコアの `exit` を待つ上限（v1 §9.2）。
constexpr Uint64 kQuitGraceMs = 30000;





/*!
 * @brief `HD2D_GPU_SYNC=1` — 各パスの後で `glFinish` して**GPU の時間を実際に測る**。
 *
 * @details 既定では `SDL_GL_SwapWindow` が垂直同期を待つので、影のパスのコストは
 * `present` に吸われて `shadow=0.00ms` としか出ない。**「速い」ではなく「見えていない」**である。
 * 設計書 §13 は「影のパスで幾何を 2〜3 回描くので予算に余裕を見ること」と言っており、
 * P5 の検証項目は「影のパスを含めたフレーム時間の実測」なので、測れる口が要る。
 *
 * 常時 on にはしない（`glFinish` は CPU と GPU を毎フレーム同期させるので遅くなる）。
 */
bool gpu_sync_enabled()
{
    const char *const raw = std::getenv("HD2D_GPU_SYNC");
    return (raw != nullptr) && (raw[0] != '\0') && (raw[0] != '0');
}




const TextColor kWarnColor{ 1.f, 0.45f, 0.35f, 1.f };



/*!
 * @brief コアの stderr は CP932 が混ざりうる（コアは `/execution-charset:shift-jis`）。
 * @details 先に UTF-8 として**厳密に**検証し、通ればそのまま返す（既に UTF-8 の行を
 * CP932 と誤読して壊さないため）。どちらでもなければ原文（化けるより落ちない方を採る）。
 */
std::string to_utf8_best_effort(const std::string &text)
{
    if (text.empty()) {
        return text;
    }
#if !defined(_WIN32)
    /*
     * 非 Windows は 1 プロセスで、コアの stderr は自分の stderr（`core_stderr_tail` は
     * 常に空）。ここへ CP932 が来る経路がそもそも無いので、素通しでよい。
     */
    return text;
#else
    const int len = static_cast<int>(text.size());
    if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), len, nullptr, 0) > 0) {
        return text;
    }
    const int wide_len = ::MultiByteToWideChar(932, 0, text.data(), len, nullptr, 0);
    if (wide_len <= 0) {
        return text;
    }
    std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
    if (::MultiByteToWideChar(932, 0, text.data(), len, wide.data(), wide_len) <= 0) {
        return text;
    }
    const int utf8_len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_len, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) {
        return text;
    }
    std::string out(static_cast<std::size_t>(utf8_len), '\0');
    if (::WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_len, out.data(), utf8_len, nullptr, nullptr) <= 0) {
        return text;
    }
    return out;
#endif /* _WIN32 */
}

//! コアが原因の失敗。**stderr の末尾と全文ログの場所を必ず添える**（v1 §13-4）。
void show_core_failure(CoreLink &link, const std::string &text)
{
    std::string body = text;
    const std::string tail = link.core_stderr_tail(CoreLink::kStderrDialogLines);
    if (!tail.empty()) {
        body += "\n\n--- ゲームコアの出力（末尾） ---\n";
        body += to_utf8_best_effort(tail);
    }
    const std::string path = link.core_stderr_log_path();
    if (!path.empty()) {
        body += "\n全文: ";
        body += path;
    }
    /*
     * **テスト用の入口（`--test-input-file=`）が有効なときだけ**、確認の窓を出さず、理由を標準エラーへ書く。
     * 窓が出ると、操作を待ってプロセスが止まり、検査側は答えが来ないまま上限の時間を待つことになる
     * （Rogue 5.4 の `V` でコアが落ちた件。F-rogue54-01）。呼び出し元は窓の後で `run()` を終えるので、
     * 終了コードは窓を出す場合と同じ（0 以外）で、検査側はプロセスの終了として落ちを知る。
     * 遊ぶ人（テスト用の入口なし）には今までどおり窓を出す。
     */
    if (ui_probe().enabled()) {
        std::fprintf(stderr, "[hd2d] core failure (test entry: no dialog): %s\n", body.c_str());
        std::fflush(stderr);
        return;
    }
    show_message(body);
}

//! 「コアがまだ終了していません／強制終了しますか」（v1 §9.2）。**勝手に kill しない。**
bool ask_force_kill()
{
    SDL_MessageBoxButtonData buttons[2]{};
    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    buttons[0].buttonid = 0;
    buttons[0].text = "このまま待つ";
    buttons[1].flags = 0;
    buttons[1].buttonid = 1;
    buttons[1].text = "ゲームを終わらせる";

    SDL_MessageBoxData data{};
    data.flags = SDL_MESSAGEBOX_WARNING;
    data.title = "変愚蛮怒 HD2D";
    data.message = "ゲームコアがまだ終了していません。\n"
                   "（コアは終了要求を ESC として受け取ります。ゲーム中の画面では\n"
                   "　それだけでは終わらないことがあります。）\n\n"
                   "このまま待ちますか。それとも今すぐ終わらせますか。\n"
                   "終わらせる場合は、まず緊急セーブの機会をコアに与えます。";
    data.numbuttons = 2;
    data.buttons = buttons;

    int pressed = 0;
    if (SDL_ShowMessageBox(&data, &pressed) != 0) {
        return false; // 出せなかった＝明示の指示は取れていない。殺さない
    }
    return pressed == 1;
}

/* ============================================================ GL のデバッグ出力 */


/* ============================================================ 窓と GL の用意 */



/* ============================================================== 画面（P0） */

//! 見出しに出す 1 行（接続と GL の素性）。
std::string build_title_line(const presentation::HelloAckMessage &ack)
{
    std::string line = "core=" + (ack.core_name.empty() ? std::string("?") : ack.core_name);
    line += " " + ack.core_version;
    line += "  protocol=" + std::to_string(ack.protocol);
    line += "  GL=" + gl_string(GL_VERSION);
    return line;
}











/*!
 * @brief **屋根を外す穴**（層 2。2026-08-18 に決めた）。
 *
 * > 透過は以下の層に分けて 2 重に透過させる
 * > 1 通常の透過（1 マス南のブロックに視界が遮られる際に発生する透過）
 * > 2 屋根自体を透過させキャラの周囲を一定範囲**高さ 1 ブロックの構造物を残し**
 * >   屋根（建造物）を透過させる
 *
 * @details 層 1（`make_player_cutaway`）との違いは 3 つだけで、**どれも既にある軸**である:
 *
 * | | 層 1 | 層 2（ここ） |
 * |---|---|---|
 * | 向き | カメラ側だけ | **全方位**（半平面 `Cutaway::face_x` を掛けない）——屋根は頭上にあって「カメラと対象の間」ではない |
 * | 高さ | 制限なし | **`roof_min_z` より上だけ**（既定 1 マス＝腰までの壁と床が残る） |
 * | 深度 | 手前だけ | **無効**（巨大値）——絞るとプレイヤより北の屋根が残って奥の部屋が見えない |
 *
 * @note **屋内でなければ呼ばないこと。** 屋外で呼ぶと、周りの木や隣家の屋根まで
 * 頭上として抜けてしまう。屋内かどうかの判定は**地形から導く**（`town_id` で決めない
 * ——コア間で衝突する既知の穴）。
 *
 * @note 強さ（`power`）は**穴ごとに持てる**（2026-08-18 に決めた:「変愚では二重で透過を
 * 持つことはないはずだから、幻想独自のルールとして追加すればいい」）。屋根は**層 1 より強く**
 * 抜く——頭上を覆う面なので、層 1 と同じ強さだと網目が残って部屋の中が読めない。
 */
Cutaway make_roof_cutaway(const Camera &camera, const GameFrame &frame, float radius,
    float min_z = 1.f, float power = 3.4f)
{
    Cutaway cut = make_point_cutaway(camera,
        Vec3{ static_cast<float>(frame.player_gx) + 0.5f,
            static_cast<float>(frame.player_gy) + 0.5f, 0.55f },
        radius, false); //!< 屋根は頭上にあって「カメラと対象の間」ではない。半平面は掛けない
    cut.roof_min_z = min_z;
    //! 深度を掛けない（**北の屋根も外す**）。`gl_FragCoord.z` は [0,1] なので 2 で足りる。
    cut.depth = 2.f;
    cut.power = power;
    return cut;
}


/*!
 * @brief 実体のボクセル板を描く（2026-08-15。ビルボードから置き換えた）。
 *
 * @param light_view_projection 影の地図へ書くときだけ渡す。nullptr なら本描画。
 *
 * @details **同じ板はまとめて 1 回で描く。**1 体 1 描画にすると、同じ種族が群れている
 * ダンジョンで描画呼び出しが実体の数だけ増える。実体は多くて数十なので、
 * 並べ替えずに「まだ描いていない板を 1 つ選び、同じ板を全部集める」で足りる。
 *
 * @param layer 描く層（`EntityLayer`）。-1 なら全部の層。層の順に描く理由は `draw_scene` の実体の描画に書いた。
 *
 * **本描画では抜きを切る**（`set_cutaway_scale(0)`）。カットアウェイが抜くのは
 * 「実体を隠している壁」のほうで、実体自身を網目にしたら本末転倒になる。
 */
void draw_entity_slabs(VoxelRenderer &renderer, SlabLibrary &library, const EntityView &entities,
    const Mat4 *light_view_projection, int layer = -1)
{
    if (entities.slabs.empty()) {
        return;
    }
    std::vector<bool> done(entities.slabs.size(), false);
    if (layer >= 0) {
        for (std::size_t i = 0; i < entities.slabs.size(); ++i) {
            done[i] = static_cast<int>(entities.slabs[i].layer) != layer;
        }
    }
    std::vector<InstanceData> batch;
    for (std::size_t i = 0; i < entities.slabs.size(); ++i) {
        if (done[i] || (entities.slabs[i].slab < 0)) {
            continue;
        }
        const int id = entities.slabs[i].slab;
        batch.clear();
        for (std::size_t j = i; j < entities.slabs.size(); ++j) {
            if (done[j] || (entities.slabs[j].slab != id)) {
                continue;
            }
            done[j] = true;
            batch.push_back(entities.slabs[j].inst);
        }
        GpuPrefab &gpu = library.gpu(id);
        if (light_view_projection != nullptr) {
            renderer.draw_instanced_depth(gpu, *light_view_projection, batch.data(), batch.size());
        } else {
            renderer.draw_instanced(gpu, batch.data(), batch.size());
        }
    }
}

/* ================================================== プレハブ 1 個を見る（P1） */



/*!
 * @brief 実体の板の粒を 7 段すべてで通す（`--slab-ladder-check`）。窓を出さない。
 *
 * @details 2026-08-21 に決めた「人物・アイテム・モンスターは
 * 8/16/32/64/128/256/512 を受け入れられるように。**コアは問わずエンジンとして**」。
 * 段ごとに合成の板を組んで、**書く → 読み戻す → メッシュ化 → LOD** まで通す。
 *
 * ここが要るのは、**通らない段が黙って絵の崩れとして出る**からである。実際に 512 で
 * 踏んだ: `.vox` の `XYZI` は座標が 1 バイトなので x=256 が x=0 へ折り返し、
 * 板の右半分が左半分に重なった絵が出た（落ちも警告もしない）。いまは
 * `XYZ2`（16 ビット座標）で書き、`XYZI` で 256 超が来たら**読み込みを失敗させる**。
 * その 2 つをここで毎回確かめる。
 */
#if defined(HD2D_EDITOR)
#endif // HD2D_EDITOR（書く側は `hd2d/edit/prefab_writer.cpp`。リリース用には入らない）






/* ============================== 地形の検査（合成フレーム。コアを起こさない） */











/*! @} */



/* ======================== ポスト処理の検査（合成フレーム。コアを起こさない）P7 */

//! いまの既定のフレームバッファを RGBA で読む（左下原点のまま。比較にしか使わない）。






/* ================================== UI の検査（P8。コアを起こさない） */








/* ==================================================== コア選択画面（設計 §6.1） */

/*!
 * @brief 自 exe のあるディレクトリ。**cwd には依らない。**
 * @details `core_link.cpp` の同名の関数と同じもの。あちらは file-static なので写した
 * （公開すると「コアの起こし方」の一部が外へ漏れる。ここが欲しいのは
 * 「隣に何の exe が在るか」だけ）。
 */
std::filesystem::path app_exe_directory()
{
#if defined(_WIN32)
    char buf[MAX_PATH]{};
    const DWORD len = ::GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if ((len == 0) || (len >= MAX_PATH)) {
        return std::filesystem::path();
    }
    return std::filesystem::path(std::string(buf, len)).parent_path();
#elif defined(HENGBAND_DESKTOP_LINUX)
    std::error_code ec;
    return std::filesystem::read_symlink("/proc/self/exe", ec).parent_path();
#else
    std::error_code ec;
    return std::filesystem::current_path(ec);
#endif
}

/*!
 * @brief コアの exe の在り処を決める。**`cores/` を先に見て、無ければ自 exe の隣。**
 *
 * @details 配布物ではコアを 1 階層深い `cores/` へ入れる——画面と横に並んでいると
 * **コアを直に起動してしまう**（2026-09-06 に気づいた「間違えて各コアを実行してしまう」）。
 * コアは単体では遊べない（画面が子として起こし、パイプでプロトコル v1 を話す）。
 *
 * 開発のツリーは今までどおり横並びなので**両方を見る**。絶対パスはそのまま返す。
 * cfg の名前は**ファイル名だけ**から作るので、どちらに置いても同じ cfg を使う
 * （`settings_path_for_core` の検査がその形を押さえている）。
 */
#if defined(_WIN32) || defined(HENGBAND_DESKTOP_LINUX)
std::filesystem::path resolve_core_path(const std::string &name)
{
    std::filesystem::path given(name);
    if (given.is_absolute()) {
        return given;
    }
    if (auto imported = imported_core_path(name); !imported.empty()) return imported;
    const std::filesystem::path base = app_exe_directory();
    std::error_code ec;
    const std::filesystem::path deep = base / "cores" / given;
    if (std::filesystem::is_regular_file(deep, ec)) {
        return deep;
    }
    return base / given;
}
#endif

/*!
 * @brief その道のコアが**在るか**だけを見る。**起こさない。**
 *
 * @details 2026-08-23 に決めた「コアの選択画面時点ではコアの起動はしないように」。
 * Windows は隣に exe が在るかを見るだけなので元から起こしていない。**問題は Android/Quest**
 * で、以前はここが `core_thread_resolve`（＝`dlopen(RTLD_NOW)`）を呼んでいた——
 * 選ぶ前に**コア 4 本ぜんぶを読み込んで静的初期化まで走らせて**いたことになる。
 * 平台側が「在るかだけ」を答える口（`core_thread_available`）を差していればそれを使い、
 * 無ければ従来どおり解決に落ちる（後方互換。差していない入口でも一覧は出る）。
 */
bool core_is_available(const AppOptions &options, const std::string &path)
{
#if defined(_WIN32) || defined(HENGBAND_DESKTOP_LINUX)
    (void)options;
    if (path.empty()) {
        return false;
    }
    /*
     * **`std::filesystem::path` の組み立ては投げる。** MSVC は `std::string` を
     * その機械の narrow の符号（ここでは CP932）として読むので、cfg に化けた名が
     * 残っていると「CP932 として不正な並び」で `std::system_error` が飛ぶ。
     * 実際に落とした——壊れた cfg を掃除するための関数が、壊れた cfg で落ちていた。
     * 読めない道は**そのまま「在らない」**でよい（どのみち起こせない）。
     */
    try {
        std::error_code ec;
        //! 在り処の決め方は `resolve_core_path`（`cores/` を先に見る）で 1 か所に閉じる。
        const auto resolved = resolve_core_path(path);
        if (!std::filesystem::is_regular_file(resolved, ec)) return false;
#if defined(HENGBAND_DESKTOP_LINUX)
        return ::access(resolved.c_str(), X_OK) == 0;
#else
        return true;
#endif
    } catch (const std::exception &) {
        return false;
    }
#else
    if (path.empty()) {
        return false;
    }
    if (options.core_thread_available != nullptr) {
        return options.core_thread_available(path);
    }
    return (options.core_thread_resolve != nullptr) && (options.core_thread_resolve(path) != nullptr);
#endif
}

/*!
 * @brief コアの道が同じものを指すか。**Windows は大小文字を区別しない。**
 * @details cfg を手で書いた人が `hengbandcore.exe` と綴っても、自動登録の
 * `HengbandCore.exe` と二重に並ばないようにするため。
 */
bool same_core_path(const std::string &a, const std::string &b)
{
#if defined(_WIN32)
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char ca = ((a[i] >= 'A') && (a[i] <= 'Z')) ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        const char cb = ((b[i] >= 'A') && (b[i] <= 'Z')) ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
        if (ca != cb) {
            return false;
        }
    }
    return true;
#else
    return a == b;
#endif
}

/*!
 * @brief cfg から読んだ一覧を整える。**在らないパスと重複を落とす。**
 *
 * @details 2026-08-23 に気づいた「コアがループするように並んでいて小さい」の始末。
 * 実際に起きていたのは繰り返しではなく**重複**である——cfg の `cores=` の表示名が
 * 一度 CP932 で読み違えられ、その化けた名の末尾の下位バイトが区切りの `|` を喰った。
 * 結果 `名|パス` が 1 つのパスに潰れ（`…HengbandCore.exe` という在りもしないパス）、
 * 下の継ぎ足しが「まだ載っていない」と判じて**同じコアをもう 1 行足した**。
 * 4 本のはずが 7 行になり、行の高さが窓を人数で割った分だけ縮んでいた。
 *
 * 直し方は「化けを直す」ではなく**在るものだけ残す**である。名前がどう化けていようと、
 * パスが指す exe / .so が無ければその行は選んでも起動できない＝出す意味が無い。
 * これで壊れた cfg は次の書き戻しで自然に治る（`settings.cores = core_list`）。
 */
std::vector<CoreEntry> sanitize_cores(const AppOptions &options, const std::vector<CoreEntry> &stored)
{
    std::vector<CoreEntry> kept;
    for (const CoreEntry &entry : stored) {
        if (entry.path.empty()) {
            continue;
        }
        bool duplicate = false;
        for (const CoreEntry &have : kept) {
            if (same_core_path(have.path, entry.path)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            std::fprintf(stderr, "[hd2d] cfg のコアが重複しています（落とします）: %s\n", entry.path.c_str());
            continue;
        }
        if (!core_is_available(options, entry.path)) {
            std::fprintf(stderr, "[hd2d] cfg のコアが見つかりません（落とします）: %s\n", entry.path.c_str());
            continue;
        }
        kept.push_back(entry);
    }
    return kept;
}

/*!
 * @brief `HengbandHd2d.exe` と同じ場所を見て、**在るコアだけ**を並べる。
 *
 * @details 初回起動（cfg に `cores=` が無い）ときだけ呼ぶ。
 * §6.1 の「初回は exe と同じ場所を見て在るものだけ自動登録」。
 * **無いものは並べない**——選べない項目を出しても「起動できませんでした」を
 * 出すだけで、遊ぶ人には何の情報にもならない。
 *
 * ここが画面側の持つコアの知識の全部である（同 §1 制約 1）。表示名と exe の名前だけで、
 * タイルもキーもデータの置き場も持たない。
 */
std::vector<CoreEntry> discover_cores(const AppOptions &options)
{
    struct Known {
        const char *name;
        const char *path;
    };
    std::vector<CoreEntry> found;
#if defined(_WIN32)
    static const Known kKnown[] = {
        { "変愚蛮怒", "HengbandCore.exe" },
        { "短愚蛮怒", "TangbandCore.exe" },
        //! TOband。表示名は固有名詞のまま（和名は無い）。
        { "TOband", "TobandCore.exe" },
        { "幻想蛮怒", "GensobandCore.exe" },
        /*
         * Sil-Q。**表示名は英語のまま**である
         * ——先方に日本語版が無く、コアの文字列も全部英語で出る（要件 R2）。
         * ここで和名を付けると、選んだ先の画面と名前が食い違う。
         */
        { "Sil-Q", "SilCore.exe" },
        /*
         * FroxComposband。**表示名は英語のまま**
         * ——固有名詞に和名は無い（Sil-Q と同じ理由。M1 で日本語になっても変えない）。
         */
        { "FroxComposband", "FroxCore.exe" },
        { "RogueClone2", "Rogue2Core.exe" },
        { "Rogue 3.6", "Rogue36Core.exe" },
        { "Rogue 5.4", "Rogue54Core.exe" },
        { "Umoria", "UmoriaCore.exe" },
        { "Angband", "AngbandCore.exe" },
        //! DCSS（Dungeon Crawl Stone Soup 日本語版）。表示名は固有名詞のまま（Sil-Q・Frox と同じ理由）。
        { "DCSS", "DcssCore.exe" },
    };
    for (const Known &known : kKnown) {
        if (core_is_available(options, known.path)) {
            found.push_back(CoreEntry{ known.name, known.path });
        }
    }
#elif defined(HENGBAND_DESKTOP_LINUX)
    static const Known kKnown[] = {
        { "変愚蛮怒", "HengbandCore" }, { "短愚蛮怒", "TangbandCore" },
        { "TOband", "TobandCore" },
        { "幻想蛮怒", "GensobandCore" }, { "Sil-Q", "SilCore" },
        { "FroxComposband", "FroxCore" }, { "RogueClone2", "Rogue2Core" },
        { "Rogue 3.6", "Rogue36Core" }, { "Rogue 5.4", "Rogue54Core" },
        { "Umoria", "UmoriaCore" },
        { "Angband", "AngbandCore" },
        { "DCSS", "DcssCore" }
    };
    for (const Known &known : kKnown) {
        if (core_is_available(options, known.path)) found.push_back({known.name, known.path});
    }

#else
    /*
     * Android / Quest: コアは APK 同梱の .so（1 プロセス 2 スレッドのまま、選ばれた .so を
     * dlopen してコアスレッドの入口を引く。 の多コア形）。
     * 「在るものだけ」は**.so が在るかだけ**で見る（`core_is_available`）。ここで dlopen して
     * しまうと、選ぶ前に 4 本ぜんぶを読み込むことになる（2026-08-23 に決めた）。
     */
    static const Known kKnown[] = {
        { "変愚蛮怒", "libhengcore.so" },
        { "短愚蛮怒", "libtangcore.so" },
        //! TOband。`.so` を作るのは後段（Android / Quest は M0 の対象外）——無い間は一覧に出ない。
        { "TOband", "libtobandcore.so" },
        { "幻想蛮怒", "libgensocore.so" },
        //! 表示名を英語のままにする理由は Windows 側と同じ（上の註）。
        { "Sil-Q", "libsilcore.so" },
        /*
         * FroxComposband。**`.so` を作るのは後段**（設計 §6.3・§10。Android / Quest は
         * M0 の対象外）——`kKnown` は「在るものだけ」を拾うので、無い間は一覧に出ない。
         */
        { "FroxComposband", "libfroxcore.so" },
        { "RogueClone2", "librogue2core.so" },
        { "Rogue 3.6", "librogue36core.so" },
        { "Rogue 5.4", "librogue54core.so" },
        { "Umoria", "libumoriacore.so" },
        { "Angband", "libangbandcore.so" },
        //! DCSS（日本語版）。生成物が無いと `.so` が組まれない（tools/core_sources/dcss_android_core.cmake）ので、その間は一覧に出ない。
        { "DCSS", "libdcsscore.so" },
    };
    //! 並びも exe 版と同じにする（`kKnown` の順がそのまま画面の順になる）。
    for (const Known &known : kKnown) {
        if (core_is_available(options, known.path)) {
            found.push_back(CoreEntry{ known.name, known.path });
        }
    }
#endif
    return found;
}

//! 選んだバナーが画面幅まで広がるのにかける時間（2026-08-23 に決めた「1.5 秒で最大に」）。
constexpr float kCoreSelectZoomMs = 1500.f;
/*!
 * @brief 演出の 1 フレームで素材を GPU へ載せてよい時間（ミリ秒）。
 * @details 60 fps の 1 フレームは 16.6 ms。この画面が描くのはパネル 4〜5 枚と字だけなので、
 * 12 ms を素材に回しても間に合う（実測でも演出は滑らかなまま）。
 * 大きくすると素材は早く載るが、いずれ演出がかくつく——**演出を止めないほうを採る**。
 */
constexpr int kCoreSelectUploadBudgetMs = 12;

//! exe / so の名から系譜図・バナーの鍵（`rogue36` など）を引く。
//! `assets/ui/lineage.json` の `core` 欄と同じ鍵を使う。分からなければ空文字。
std::string core_key_for_path(const std::string &path)
{
    std::string low = path;
    for (char &c : low) {
        c = ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c - 'A' + 'a') : c;
    }
    const char *key = (low.find("dcss") != std::string::npos)        ? "dcss"
                      : (low.find("toband") != std::string::npos)    ? "toband"
                      : (low.find("rogue36") != std::string::npos)   ? "rogue36"
                      : (low.find("rogue54") != std::string::npos) ? "rogue54"
                      : (low.find("rogue2") != std::string::npos)  ? "rogue2"
                      : (low.find("tang") != std::string::npos)    ? "tang"
                      : (low.find("genso") != std::string::npos)   ? "genso"
                      : (low.find("umoria") != std::string::npos)  ? "umoria"
                      : (low.find("angband") != std::string::npos) ? "angband"
                      : (low.find("frox") != std::string::npos)    ? "frox"
                      : (low.find("heng") != std::string::npos)    ? "heng"
                      : (low.find("sil") != std::string::npos)     ? "sil"
                                                                   : nullptr;
    return (key != nullptr) ? key : std::string();
}

/*!
 * @brief コア選択画面を回す。**CoreLink を起こす前**に呼ぶ（設計 §6.1）。
 * @param cores 一覧（2 件以上のときだけ呼ばれる）
 * @param initial 初期カーソル（前回選んだ項目の添字）
 * @param on_decided **決めた瞬間**に 1 回呼ぶ（演出より前）。呼ぶ側はここで素材の読み込みを
 * 別スレッドで走らせる——広がる 1.5 秒を待たせないため（2026-08-23 に決めた「その裏でロードが始まる」）。
 * @param still_loading 広がり切った後、これが真の間は画を回し続ける（読み込みの間も窓が死なない）。
 * @return 選んだ添字、または **-1**（窓を閉じた／ESC）
 *
 * @details 使う道具はどれも `link.start()` より前に用意されているもの（GL 窓・
 * `TextOverlay`・`UiPaint`・`UiImagePainter`・`GamePad`）。ここで自前の
 * `SDL_PollEvent` と swap を回す——**まだ主ループに入っていない**ため。
 */
int run_core_select(const std::vector<CoreEntry> &cores, int initial, Window &window, TextOverlay &base_text,
    UiPaint &paint, UiImagePainter &images, GamePad &pad, const Hd2dSettings &cfg,
    const std::function<void()> &on_decided = {}, const std::function<bool()> &still_loading = {},
    const std::string &shot_path = std::string(), int shot_after = 0)
{
    const auto import_cfg = core_import_config();
    std::error_code import_error;
    const bool import_enabled = cores.empty()
        && std::filesystem::is_regular_file(import_cfg.compiler, import_error)
        && std::filesystem::is_regular_file(import_cfg.kit / "catalog.json", import_error);
    LineageData lineage_probe;
    std::string lineage_probe_error;
    const bool lineage_enabled = load_lineage(lineage_probe, lineage_probe_error);
    if (!lineage_enabled) {
        std::fprintf(stderr, "[hd2d] lineage: %s\n", lineage_probe_error.c_str());
    }
    struct ImportOverlay { TextOverlay value; ~ImportOverlay() { value.shutdown(); } } enlarged;
    int initial_w = 0, initial_h = 0; SDL_GetWindowSize(window.window, &initial_w, &initial_h);
    const int font_size = core_import_font_size(initial_w, initial_h);
    std::string font_error;
    const bool large = font_size > base_text.cell_h() && enlarged.value.init(font_size, font_error);
    TextOverlay &text = large ? enlarged.value : base_text;
    int cursor = std::clamp(initial, 0, std::max(0, static_cast<int>(cores.size()) - 1));
    int drawn = 0;

    /*
     * **言語の札**（2026-08-26 に決めた「共通にして、コア選択画面に
     * 言語切り替えボタンを作ろう」）。
     *
     * ここに置くのは、**言語がコアより先に決まるもの**だからである。設定は
     * コアごとのファイルに分けたが（`settings_path_for_core`）、言語だけは
     * `hd2d.cfg` が持つ——コアを替えるたびに画面の言葉が変わるのは筋が通らない。
     *
     * 替えた結果は `i18n` に入る。呼び手はこの関数から戻ったあと
     * `i18n::current()` を `settings.lang` へ写して cfg へ書く。
     */
    const auto cycle_lang = [](int delta) {
        const auto &langs = i18n::available();
        if (langs.size() < 2) {
            return; //!< 1 つしか読めていないなら替えようが無い
        }
        const int count = static_cast<int>(langs.size());
        int index = 0;
        for (int i = 0; i < count; ++i) {
            if (i18n::current() == langs[static_cast<std::size_t>(i)].code) {
                index = i;
                break;
            }
        }
        const auto &next = langs[static_cast<std::size_t>((((index + delta) % count) + count) % count)];
        i18n::set_language(next.code);
    };
    /*!
     * @brief 言語の札の並べ方。**選べるものを全部横に並べる**（2026-09-01 に決めた
     * 「日本語も English も両方見えた状態にして。現状では切り替えられるってわからんので」）。
     *
     * @details いままでは `◀ 日本語 ▶` で、**いま選んでいる 1 つしか出ていなかった**。
     * 矢印で「替えられる」ことは言っていたつもりだったが、**替えた先が見えない**ので
     * 伝わらなかった。並べて、いま選んでいるものへ `●`、ほかへ `○` を付ける。
     *
     * **2 つ（日本語・English）ならトグルに見え、増えても崩れない**——
     * `i18n::available()` は `assets/lang/<code>.json` が読めたものだけを返すので、
     * 言語が増えたらそのぶん札が伸びる。場所は**右上**のまま。
     */
    struct LangSlot {
        std::string code; //!< `i18n::set_language()` へ渡す
        std::string text; //!< 印つきの見え方（`● 日本語`）
        RectPx box{};     //!< 押せる範囲（この札 1 つぶん）
        bool on{ false }; //!< いま選んでいるか
    };
    //! 札と札のあいだ／下敷きの内側の余白。
    constexpr int kLangGap = 18;
    constexpr int kLangPad = 14;
    const auto lang_slots_at = [&text](int w, int h) {
        (void)h;
        std::vector<LangSlot> slots;
        int total = 0;
        for (const auto &info : i18n::available()) {
            LangSlot s;
            s.code = info.code;
            s.on = (i18n::current() == info.code);
            //! 印は**同じ全角 1 字**にそろえる（`●`／`○`）。替えても幅が動かない。
            s.text = std::string(s.on ? "● " : "○ ") + info.endonym;
            s.box.w = text.measure(s.text);
            total += s.box.w;
            slots.push_back(std::move(s));
        }
        if (slots.empty()) {
            return slots;
        }
        total += kLangGap * (static_cast<int>(slots.size()) - 1);
        const int height = text.cell_h() + 16;
        int x = w - 24 - kLangPad - total;
        for (auto &s : slots) {
            s.box.x = x;
            s.box.y = 20;
            s.box.h = height;
            x += s.box.w + kLangGap;
        }
        return slots;
    };
    //! 下敷き（札の群れを囲む枠）。**押した所の判定には使わない**——判定は札ごと。
    const auto lang_rect_at = [&lang_slots_at](int w, int h) {
        const auto slots = lang_slots_at(w, h);
        if (slots.empty()) {
            return RectPx{ w - 24, 20, 0, 0 };
        }
        const RectPx &a = slots.front().box;
        const RectPx &b = slots.back().box;
        return RectPx{ a.x - kLangPad, a.y, (b.x + b.w + kLangPad) - (a.x - kLangPad), a.h };
    };
    //! 系譜の札（左上。言語の札（右上）と対にする）。
    const auto lineage_button_rect_at = [&text](int w, int h) {
        (void)w; (void)h;
        const std::string label = i18n::tr("hd2d.app.lineage.open");
        const int width = text.measure(label) + (kLangPad * 2);
        const int height = text.cell_h() + 16;
        return RectPx{ 24, 20, width, height };
    };

    /*
     * コアごとのバナー（2026-08-19 に決めた「テキストではなくバナー表示に」）。
     * exe / so の名から絵を引く（表示名は SJIS の日本語なので鍵にしない）。読めなかった
     * 行は従来どおり文字で出す——絵が無いだけで選べなくなるのは本末転倒である。
     * 絵の中身と再生成は tools/gen_core_select_banners.py（左=場面・右へ暗転・題字と説明）。
     */
    std::vector<UiImage> banners(cores.size());
    /*
     * **列に並べる**。並びは見つかった順ではなく、下の表の順で固定する。
     * - 縦長の窓（幅 <= 高さ）は 2 列（決めたこと 2026-09-23「コアが増えてきたのでバナーを 2 列にして
     *   左に Rogue3.6 / Rogue5.4 / Rogue Clone2、右に 変愚 / 短愚 / 幻想 / Sil-Q / Frox」。Angband は左の列の末尾、
     *   Rogue → Angband の系譜の順）。TOband は右の列の最後尾。
     * - 横長の窓（幅 > 高さ）は 3 列:
     *     Rogue3.6     変愚   Sil-Q
     *     Rogue5.4     短愚
     *     RogueClone2  幻想
     *     Umoria       Frox
     *     Angband      TOband
     * 表に無いコアは真ん中（縦長では右）の列の末尾。空の列は詰める（Rogue だけ・変愚系だけの配布は 1 列で中央）。
     * 窓は回せる（Android の回転・窓の大きさの変更）ので、両方の並びを作っておき、毎回窓の縦横で選ぶ。
     */
    struct CoreSlot {
        int column{ 0 };
        int row{ 0 };
    };
    struct ColumnLayout {
        std::vector<CoreSlot> slots;
        std::vector<int> column_rows; //!< 列ごとの段の数（空の列は詰めてある）
        int column_count() const { return static_cast<int>(column_rows.size()); }
        int max_rows() const
        {
            int rows = 0;
            for (const int r : column_rows) {
                rows = std::max(rows, r);
            }
            return rows;
        }
    };
    std::vector<std::string> core_keys(cores.size());
    for (std::size_t i = 0; i < cores.size(); ++i) {
        core_keys[i] = core_key_for_path(cores[i].path);
    }
    const auto build_layout = [&](const std::vector<std::vector<const char *>> &columns, int extra_column) {
        ColumnLayout layout;
        layout.slots.resize(cores.size());
        std::vector<int> rows(columns.size(), 0);
        std::vector<bool> placed(cores.size(), false);
        for (std::size_t c = 0; c < columns.size(); ++c) {
            for (const char *key : columns[c]) {
                for (std::size_t i = 0; i < cores.size(); ++i) {
                    if (!placed[i] && core_keys[i] == key) {
                        layout.slots[i] = CoreSlot{ static_cast<int>(c), rows[c]++ };
                        placed[i] = true;
                    }
                }
            }
        }
        for (std::size_t i = 0; i < cores.size(); ++i) {
            if (!placed[i]) {
                layout.slots[i] = CoreSlot{ extra_column, rows[static_cast<std::size_t>(extra_column)]++ };
            }
        }
        //! 空の列を詰める（列の番号を付け直す）。全部空なら 1 列。
        std::vector<int> renumber(columns.size(), 0);
        for (std::size_t c = 0; c < columns.size(); ++c) {
            renumber[c] = static_cast<int>(layout.column_rows.size());
            if (rows[c] > 0) {
                layout.column_rows.push_back(rows[c]);
            }
        }
        for (auto &slot : layout.slots) {
            slot.column = renumber[static_cast<std::size_t>(slot.column)];
        }
        if (layout.column_rows.empty()) {
            layout.column_rows.push_back(0);
        }
        return layout;
    };
    const ColumnLayout tall_layout = build_layout({
        { "rogue36", "rogue54", "rogue2", "umoria", "angband" },
        { "heng", "tang", "genso", "sil", "frox", "toband", "dcss" },
    }, 1);
    const ColumnLayout wide_layout = build_layout({
        { "rogue36", "rogue54", "rogue2", "umoria", "angband" },
        { "heng", "tang", "genso", "frox", "toband", "dcss" },
        { "sil" },
    }, 1);
    const auto layout_for = [&](int win_w, int win_h) -> const ColumnLayout & {
        return (win_w > win_h) ? wide_layout : tall_layout;
    };
    const auto current_layout = [&]() -> const ColumnLayout & {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window.window, &w, &h);
        return layout_for(w, h);
    };
    //! 同じ列の中で上下に動く／列を移る（端からは反対の端へ回る）。列を移るときは同じ段（無ければその列の最後）へ。
    const auto move_cursor = [&](int current, int dx, int dy) {
        if (cores.empty() || current < 0) {
            return 0;
        }
        const ColumnLayout &layout = current_layout();
        const CoreSlot from = layout.slots[static_cast<std::size_t>(current)];
        const int column_count = layout.column_count();
        int column = from.column;
        int row = from.row;
        if (dx != 0 && column_count > 1) {
            column = (column + (dx > 0 ? 1 : column_count - 1)) % column_count;
            row = std::min(row, layout.column_rows[static_cast<std::size_t>(column)] - 1);
        }
        const int rows_here = layout.column_rows[static_cast<std::size_t>(column)];
        if (dy != 0 && rows_here > 0) {
            row = (row + (dy > 0 ? 1 : rows_here - 1)) % rows_here;
        }
        for (std::size_t i = 0; i < layout.slots.size(); ++i) {
            if (layout.slots[i].column == column && layout.slots[i].row == row) {
                return static_cast<int>(i);
            }
        }
        return current;
    };
    for (std::size_t i = 0; i < cores.size(); ++i) {
        const char *key = core_keys[i].empty() ? nullptr : core_keys[i].c_str();
        if (key == nullptr) {
            continue; // 知らないコア。文字行で出す
        }
        std::string image_err;
        if (!banners[i].load(std::string("assets/ui/banner_") + key + ".png", image_err)) {
            std::fprintf(stderr, "[hd2d] %s\n", image_err.c_str()); // 出ないことに気づけるように
        }
    }
    //! 返り道がいくつもあるので、後始末（GL テクスチャ）は看取りに任せる。
    struct BannerGuard {
        std::vector<UiImage> &list;
        ~BannerGuard()
        {
            for (UiImage &image : list) {
                image.unload();
            }
        }
    } banner_guard{ banners };

    /*
     * バーチャルパッド（2026-08-18 に決めた「この画面でも有効にして」）。
     * この画面で意味があるのは LS（上下で選ぶ）と A / B だけなので、他のボタンを隠した
     * 写し設定で動かす。安全域（切り欠き）はこの段ではまだ読めていないので窓いっぱいを
     * 渡す——盤面は中央・ボタンは大きいので実害は無い。コマンド表は握手前なので空
     * （`VirtualPad::draw` の約束どおり。5851 行の見本と同じ形）。
     */
    VirtualPad vpad;
    VirtualPadSettings vpad_settings = cfg.vpad;
    for (int i = 0; i < kVpadControlCount; ++i) {
        const bool keep = (i == static_cast<int>(VpadControl::LS)) || (i == static_cast<int>(VpadControl::A))
            || (i == static_cast<int>(VpadControl::B));
        vpad_settings.visible[i] = keep && cfg.vpad.visible[i];
    }

    /* 行の矩形。**描画とタップの当たりで同じ式を使う**（片方だけ直すとズレる）。 */
    struct RowPlan {
        int x;
        int y0;
        int w;
        int h;
        int gap;
        int head_h;
        int foot_h;
    };
    /*
     * バナーは 4:1（1536×384。tools/gen_core_select_banners.py の寸法）。高さは
     * 「見出しと足の間に全行が収まる」から決め、幅は画面幅で抑える。上限 360px は
     * 2 件しか無いときに 1 枚が間延びしないための蓋。
     */
    const auto plan_rows = [&](int win_w, int win_h) {
        RowPlan plan{};
        const int line_h = text.cell_h() + 10;
        plan.gap = 24;
        /*
         * 頭と足は**ただの余白**である（2026-08-23 に決めた「バナー上下の説明は不要」）。
         * 以前はここに見出し「ゲームコアを選んでください」と操作の説明を出していた。
         * 文字を消したぶんはバナーの高さになる。
         */
        plan.head_h = 32;
        plan.foot_h = std::max(32, win_h - core_import_entry_rect(win_w, win_h).y + 8);
        //! 段の数はいちばん長い列で決める。短い列は上から詰める。
        const ColumnLayout &layout = layout_for(win_w, win_h);
        const int column_count = layout.column_count();
        const int rows = std::max(1, layout.max_rows());
        const int avail_h = win_h - plan.head_h - plan.foot_h;
        int banner_h = std::min(360, (avail_h - ((rows - 1) * plan.gap)) / std::max(1, rows));
        int banner_w = banner_h * 4;
        //! 横は列の数ぶん（列の間も gap）が窓に収まるように抑える。
        const int max_w = ((win_w - 120) - ((column_count - 1) * plan.gap)) / column_count;
        if (banner_w > max_w) {
            banner_w = max_w;
            banner_h = banner_w / 4;
        }
        banner_h = std::max(banner_h, line_h + 8); // 極端に低い窓でも行として成立させる
        plan.w = banner_w;
        plan.h = banner_h;
        plan.x = (win_w - ((banner_w * column_count) + ((column_count - 1) * plan.gap))) / 2;
        const int used = (banner_h * rows) + ((rows - 1) * plan.gap);
        plan.y0 = plan.head_h + std::max(0, (avail_h - used) / 2);
        return plan;
    };
    const auto row_rect_at = [&](int win_w, int win_h, int index) {
        const RowPlan plan = plan_rows(win_w, win_h);
        const ColumnLayout &layout = layout_for(win_w, win_h);
        const CoreSlot slot = (index >= 0 && index < static_cast<int>(layout.slots.size()))
            ? layout.slots[static_cast<std::size_t>(index)] : CoreSlot{ 0, 0 };
        return RectPx{ plan.x + (slot.column * (plan.w + plan.gap)), plan.y0 + (slot.row * (plan.h + plan.gap)),
            plan.w, plan.h };
    };

    /*
     * 決めた後の演出（2026-08-23 に決めた）:
     * 「選択したコアのバナーが拡大していき全画面の幅になるようにアニメーションして、
     *   その裏でロードが始まる。選択されなかったバナーは拡大するバナーの裏で
     *   フェードアウト。拡大は 1.5 秒で最大に」
     *
     * 順番が肝である。**絵より先に `on_decided()` を呼ぶ**——素材の読み込みは呼ぶ側が
     * 別スレッドで走らせるので、1.5 秒の演出と読み込みが重なる（演出のぶん待ちが伸びない）。
     * 広がり切った後は `still_loading()` が偽になるまで同じ画を回し続ける
     * （窓が「応答なし」に見えないように）。
     *
     * 重なりの順は「選ばれなかった行 → 選んだ行 → 文字」。`UiImagePainter` は
     * 呼んだ順にそのまま描く（溜めない）ので、**後から描いたものが上**になる＝
     * 消えていく行は必ず広がる絵の裏に入る。
     */
    const auto decide = [&](int index) {
        if (cores.empty()) { if (import_enabled) { run_core_import(window.window, text, paint, {}, &pad); return -2; } return -1; }
        if (on_decided) {
            on_decided(); //!< **絵より先に**。1.5 秒を読み込みに使わせる
        }
        const bool has_banner = (index >= 0) && (index < static_cast<int>(banners.size())) && banners[index].valid();
        const std::string loading = i18n::tr("hd2d.app.hd2d-app.loading");
        const Uint32 began = SDL_GetTicks();
        unsigned frames = 0; //!< 演出が滑らかに回っているかを後から言えるように数える
        for (;;) {
            ++frames;
            //! 入力は捨てる（決定は取り消せない）。溜めないと窓が「応答なし」になる。
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
            }
            const Uint32 elapsed = SDL_GetTicks() - began;
            const float raw = std::min(1.f, static_cast<float>(elapsed) / kCoreSelectZoomMs);
            const float grow = raw * raw * (3.f - (2.f * raw)); //!< なめらかに始まり、なめらかに止まる
            int w = 0;
            int h = 0;
            SDL_GetWindowSize(window.window, &w, &h);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, w, h);
            glDisable(GL_DEPTH_TEST);
            glClearColor(0.02f, 0.02f, 0.04f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            /*
             * 選ばれなかった行。**前半で消える**（広がる絵に追い越される前に薄くなる）。
             * **減光の幕（0.6 の黒）は薄めない。**一緒に薄めると、消え始めの絵が
             * 幕を失って**明るくなってから**消える——「フェードアウト」に見えない
             * （実際にそう撮れた）。幕はそのままなので、始まりの絵は選択画面と同じ濃さで、
             * そこから単調に薄れる。絵が消えた後に残る黒い帯は、背景がほぼ黒なので見えない。
             */
            const float others = std::max(0.f, 1.f - (raw / 0.5f));
            if (others > 0.f) {
                for (std::size_t i = 0; i < cores.size(); ++i) {
                    if (static_cast<int>(i) == index) {
                        continue;
                    }
                    const RectPx row = row_rect_at(w, h, static_cast<int>(i));
                    if (banners[i].valid()) {
                        images.begin(w, h);
                        images.draw_cover(banners[i], row, others);
                    } else {
                        paint.begin(w, h);
                        paint.rect(row, PaintColor{ 0.07f, 0.08f, 0.13f, others });
                        paint.flush();
                    }
                    paint.begin(w, h);
                    paint.rect(row, PaintColor{ 0.f, 0.f, 0.f, 0.6f });
                    paint.flush();
                }
            }

            /*
             * 選んだ行。行の矩形から**画面幅**へ広がる。バナーは 4:1 なので着地の高さは
             * `w / 4`（縦の中央）。`draw_cover` は比を保って切るので、途中の矩形が
             * 少しでも 4:1 からずれても絵は伸びない。
             */
            const RectPx from = row_rect_at(w, h, index);
            const RectPx to{ 0, (h - (w / 4)) / 2, w, w / 4 };
            const auto mix = [grow](int a, int b) {
                return static_cast<int>(std::lround(static_cast<float>(a) + ((static_cast<float>(b - a)) * grow)));
            };
            const RectPx now{ mix(from.x, to.x), mix(from.y, to.y), mix(from.w, to.w), mix(from.h, to.h) };
            if (has_banner) {
                images.begin(w, h);
                images.draw_cover(banners[index], now, 1.f);
            } else {
                paint.begin(w, h);
                paint.rect(now, PaintColor{ 0.07f, 0.08f, 0.13f, 1.f });
                paint.flush();
            }

            /*
             * 「読み込み中…」は**広がり切ってから**出す（`raw` の後ろ 3 割で入る）。
             * 広がっている最中に出すと、動く絵と一緒に字も動いて読めない。
             */
            const float tell = std::max(0.f, (raw - 0.7f) / 0.3f);
            text.begin(w, h);
            if (!has_banner && (index >= 0) && (index < static_cast<int>(cores.size()))) {
                text.draw(now.x + 24, now.y + ((now.h - text.cell_h()) / 2), cores[index].name,
                    TextColor{ 1.f, 1.f, 1.f, 1.f }, now.w - 48);
            }
            if (tell > 0.f) {
                text.draw((w - text.measure(loading)) / 2, now.y + now.h + 32, loading,
                    TextColor{ 0.85f, 0.88f, 0.95f, tell });
            }
            text.flush();

            SDL_GL_SwapWindow(window.window);
            /*
             * **広がっている最中も毎フレーム呼ぶ。**呼ぶ側はここで素材を少しずつ
             * GPU へ載せる（`PrefabLibrary::pump_upload`）。広がり終えるころには
             * 載せ終わっている＝演出の裏で読み込みが済む、が狙いである
             * （2026-08-23 に決めた「その裏でロードが始まる」の続き）。
             * 戻り値は「まだ仕事が残っているか」。
             */
            const bool busy = still_loading ? still_loading() : false;
            /*
             * **仕事が残っている間は寝ない。** `SDL_GL_SwapWindow` は垂直同期で
             * すでに 16 ms 待つので、その上に `SDL_Delay(16)` を積むと 1 秒あたりの
             * フレームが半分になり、**フレームごとに汲む素材の量も半分になる**
             * （実測で気づいた）。手が空いているときだけ寝て、CPU を明け渡す。
             */
            if (!busy) {
                SDL_Delay(16);
            }
            if ((raw >= 1.f) && !busy) {
                /*
                 * 数字を 1 行だけ残す。**フレーム数は演出が滑らかだったかの証拠**で、
                 * 素材を汲む予算（`kCoreSelectUploadBudgetMs`）を触ったときに
                 * 「絵が犠牲になっていないか」をこれで見る（60 fps なら 1.5 秒で 90 枚）。
                 */
                std::fprintf(stderr, "[hd2d] コア選択の演出: %u フレーム / %u ms\n", frames,
                    static_cast<unsigned>(SDL_GetTicks() - began));
                break;
            }
        }
        return index;
    };

    //! 系譜図を開ける。戻り値が -100 なら「そのまま続ける（Back）」。
    //! それ以外は run_core_select 自体の戻り値（Quit は -1、Launch は decide() の結果）。
    const auto open_lineage = [&]() -> int {
        std::vector<std::string> avail;
        for (const auto &k : core_keys) {
            if (!k.empty()) avail.push_back(k);
        }
        const std::string initial = ((cursor >= 0) && (static_cast<std::size_t>(cursor) < core_keys.size()))
            ? core_keys[static_cast<std::size_t>(cursor)] : std::string();
        const LineageResult lr = run_lineage(window.window, text, paint, images, &pad, cfg, avail, initial);
        if (lr.action == LineageAction::Quit) {
            return -1;
        }
        if (lr.action == LineageAction::Launch) {
            for (std::size_t i = 0; i < core_keys.size(); ++i) {
                if (core_keys[i] == lr.core_key) {
                    return decide(static_cast<int>(i));
                }
            }
        }
        return -100; //!< Back
    };

    for (;;) {
        test_keyboard().poll("core-select");
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // インポートボタンを、重なり得るバーチャルパッドより先に処理する。
            int iw = 0, ih = 0; SDL_GetWindowSize(window.window, &iw, &ih);
            int ix = -1, iy = -1;
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) { ix = event.button.x; iy = event.button.y; }
            if (event.type == SDL_FINGERDOWN) { ix = static_cast<int>(event.tfinger.x * iw); iy = static_cast<int>(event.tfinger.y * ih); }
            const auto import_box = core_import_entry_rect(iw, ih);
            if (import_enabled && ix >= import_box.x && ix < import_box.x + import_box.w && iy >= import_box.y && iy < import_box.y + import_box.h) { run_core_import(window.window, text, paint, {}, &pad); return -2; }
            const RectPx lineage_box = lineage_enabled ? lineage_button_rect_at(iw, ih) : RectPx{};
            if (lineage_enabled && ix >= lineage_box.x && ix < lineage_box.x + lineage_box.w && iy >= lineage_box.y && iy < lineage_box.y + lineage_box.h) {
                const int rc = open_lineage();
                if (rc != -100) return rc;
                continue;
            }
            /*
             * Android の指タップは、コア行と言語札を**バーチャルパッドより先に**判定する。
             * コアが 8 本まで増えた現在、下側の Rogue 3.6 / 5.4 行は LS/A/B の描画領域と
             * 重なり得る。従来は vpad.on_event() が先に FINGERDOWN を消費したため、
             * その 2 行だけタップが一覧へ届かず「起動できない」状態になっていた。
             *
             * ここでは画面上の明示的な一覧行を最優先する。行外のタップだけを後段の
             * 物理/仮想パッドへ渡すので、パッド操作自体は従来どおり残る。
             */
            if (ix >= 0) {
                const RectPx lr = lang_rect_at(iw, ih);
                if ((ix >= lr.x) && (ix < (lr.x + lr.w)) && (iy >= lr.y) && (iy < (lr.y + lr.h))) {
                    for (const auto &s : lang_slots_at(iw, ih)) {
                        if ((ix >= s.box.x) && (ix < (s.box.x + s.box.w))) {
                            i18n::set_language(s.code);
                            break;
                        }
                    }
                    continue;
                }
                bool core_row_tapped = false;
                for (std::size_t i = 0; i < cores.size(); ++i) {
                    const RectPx r = row_rect_at(iw, ih, static_cast<int>(i));
                    if ((ix >= r.x) && (ix < (r.x + r.w)) && (iy >= r.y) && (iy < (r.y + r.h))) {
                        core_row_tapped = true;
                        if (static_cast<int>(i) == cursor) {
                            return decide(cursor);
                        }
                        cursor = static_cast<int>(i);
                        break;
                    }
                }
                if (core_row_tapped) {
                    continue;
                }
            }
            if (pad.on_event(event)) {
                continue;
            }
            {
                int ww = 0;
                int wh = 0;
                SDL_GetWindowSize(window.window, &ww, &wh);
                if (vpad.on_event(event, vpad_settings, RectPx{ 0, 0, ww, wh }, ww, wh)) {
                    continue;
                }
            }
            if (event.type == SDL_QUIT) {
                return -1;
            }
            if (event.type == SDL_DROPFILE) {
                std::string zip = event.drop.file; SDL_free(event.drop.file);
                if (import_enabled) { run_core_import(window.window, text, paint, zip, &pad); return -2; }
                continue;
            }
            if (import_enabled && event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F8) {
                run_core_import(window.window, text, paint, {}, &pad); return -2;
            }
            if (lineage_enabled && event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_g) {
                const int rc = open_lineage();
                if (rc != -100) return rc;
                continue;
            }
            if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_ESCAPE || !cores.empty())) {
                switch (event.key.keysym.sym) {
                case SDLK_UP:
                case SDLK_k:
                    cursor = move_cursor(cursor, 0, -1);
                    break;
                case SDLK_DOWN:
                case SDLK_j:
                    cursor = move_cursor(cursor, 0, +1);
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE:
                    return decide(cursor);
                /*
                 * **左右は列を移る**（2 列にしたため。2026-09-23）。言語の札（右上）は
                 * Tab / Shift+Tab へ移した。札のクリックとパッドの肩ボタンはそのまま。
                 */
                case SDLK_LEFT:
                case SDLK_h:
                    cursor = move_cursor(cursor, -1, 0);
                    break;
                case SDLK_RIGHT:
                case SDLK_l:
                    cursor = move_cursor(cursor, +1, 0);
                    break;
                case SDLK_TAB:
                    cycle_lang((event.key.keysym.mod & KMOD_SHIFT) != 0 ? -1 : +1);
                    break;
                case SDLK_ESCAPE:
                    return -1;
                default:
                    break;
                }
            }
        }

        /* パッド。方向で動かし、A で決める（`pad_input_is_fixed` の固定の意味）。 */
        int dx = 0;
        int dy = 0;
        if (!cores.empty() && pad.poll_direction(SDL_GetTicks(), true, dx, dy) && (dx != 0 || dy != 0)) {
            //! 斜めは縦を優先する（列を移るつもりの無い指の揺れで、隣の列へ飛ばないように）。
            cursor = (dy != 0) ? move_cursor(cursor, 0, dy) : move_cursor(cursor, dx, 0);
        }
        PadPress press{};
        while (pad.take_pressed(press)) {
            if (press.input == PadInput::A) {
                return decide(cursor);
            }
            if (press.input == PadInput::B) {
                return -1;
            }
            if (lineage_enabled && (press.input == PadInput::Y)) {
                const int rc = open_lineage();
                if (rc != -100) return rc;
            }
            //! 言語の札（右上）。**十字は一覧を動かす**ので、肩のボタンを当てる。
            if (press.input == PadInput::LeftShoulder) {
                cycle_lang(-1);
            }
            if (press.input == PadInput::RightShoulder) {
                cycle_lang(+1);
            }
        }

        /* バーチャルパッド。物理パッドと同じ入力（LS で動かし、A で決める）。 */
        if (!cores.empty() && vpad.poll_direction(SDL_GetTicks(), dx, dy) && (dx != 0 || dy != 0)) {
            cursor = (dy != 0) ? move_cursor(cursor, 0, dy) : move_cursor(cursor, dx, 0);
        }
        while (vpad.take_pressed(press)) {
            if (press.input == PadInput::A) {
                return decide(cursor);
            }
            if (press.input == PadInput::B) {
                return -1;
            }
        }

        int w = 0;
        int h = 0;
        SDL_GetWindowSize(window.window, &w, &h);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0.02f, 0.02f, 0.04f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (ui_probe().enabled()) {
            //! テスト用の問い合わせ（`app/ui_probe.h`）：押せる部品（コアの行・言語の札・系譜・取り込み）を記録する。
            //! 矩形は上のクリックの判定が使うものと同じ関数から採る。
            ProbeWindow listing;
            listing.kind = "core-select";
            listing.selected = cursor;
            for (std::size_t i = 0; i < cores.size(); ++i) {
                const RectPx r = row_rect_at(w, h, static_cast<int>(i));
                ui_probe().button(cores[i].name, ProbeRect{ r.x, r.y, r.w, r.h }, true, static_cast<int>(i) == cursor, "core",
                    false, cores[i].path);
                listing.items.push_back(ProbeItem{ cores[i].path, cores[i].name, {}, true, static_cast<int>(i) == cursor });
            }
            ui_probe().window(std::move(listing));
            for (const auto &slot : lang_slots_at(w, h)) {
                //! 札の字は印（`● `／`○ `。4 バイト）つき。名前だけを部品の名前にする。
                ui_probe().button(slot.text.size() > 4 ? slot.text.substr(4) : slot.text,
                    ProbeRect{ slot.box.x, slot.box.y, slot.box.w, slot.box.h }, true, slot.on, "lang", false, slot.code);
            }
            if (lineage_enabled) {
                const RectPx lb = lineage_button_rect_at(w, h);
                ui_probe().button(i18n::tr("hd2d.app.lineage.open"), ProbeRect{ lb.x, lb.y, lb.w, lb.h }, true, false, "lineage", false, "lineage");
            }
            if (import_enabled) {
                const RectPx ib = core_import_entry_rect(w, h);
                ui_probe().button(core_import_caption("hd2d.app.core-import.entry"), ProbeRect{ ib.x, ib.y, ib.w, ib.h }, true,
                    false, "import", false, "import");
            }
            ui_probe().frame_info(std::string(), 0, w, h, "core-select", "menu");
        }
        const UiProbeLayer probe_layer("core-select");
        //! 背景は素の闇だけ（決めたこと 2026-08-19「この画面自体に背景画像をなくそう」）。
        paint.begin(w, h);
        for (std::size_t i = 0; i < cores.size(); ++i) {
            const RectPx row = row_rect_at(w, h, static_cast<int>(i));
            if (static_cast<int>(i) == cursor) {
                //! 選択中はひと回り大きい明色をバナーの下へ——縁取りとして見える。
                constexpr int frame = 3;
                paint.rect(RectPx{ row.x - frame, row.y - frame, row.w + (frame * 2), row.h + (frame * 2) },
                    PaintColor{ 0.62f, 0.78f, 1.f, 1.f });
            }
            if (!banners[i].valid()) {
                paint.rect(row, PaintColor{ 0.07f, 0.08f, 0.13f, 1.f }); // 絵の無い行の下地
            }
        }
        paint.flush();

        images.begin(w, h);
        for (std::size_t i = 0; i < cores.size(); ++i) {
            if (!banners[i].valid()) {
                continue;
            }
            //! 全行を不透明で貼る。矩形は 4:1 で絵と同じ比なので cover でも切れない。
            images.draw_cover(banners[i], row_rect_at(w, h, static_cast<int>(i)), 1.f);
        }

        /*
         * 非選択の行は**黒を重ねて**暗くする（2026-08-19 に決めた「暗くするだけで
         * 透過は不要」）。絵そのものを半透明で貼ると背面が透けて濁る。
         */
        paint.begin(w, h);
        for (std::size_t i = 0; i < cores.size(); ++i) {
            if (static_cast<int>(i) != cursor) {
                paint.rect(row_rect_at(w, h, static_cast<int>(i)), PaintColor{ 0.f, 0.f, 0.f, 0.6f });
            }
        }
        paint.flush();

        /*
         * 文字はバナーの無い行の名前だけ（2026-08-23 に決めた「バナー上下の説明は不要」）。
         * 見出し「ゲームコアを選んでください」と足の操作説明はここから消した——
         * バナーに題字と一行説明が入っており、操作は上下と決定しか無い。
         * 文言 `hd2d.app.hd2d-app.choose-a-game-core` /
         * `…up-down-to-choose-enter-to-start` は**カタログに残してある**（戻すときのため）。
         */
        text.begin(w, h);
        for (std::size_t i = 0; i < cores.size(); ++i) {
            if (banners[i].valid()) {
                continue; // 絵にコア名が入っている
            }
            const bool on = (static_cast<int>(i) == cursor);
            const RectPx row = row_rect_at(w, h, static_cast<int>(i));
            const TextColor color = on ? TextColor{ 1.f, 1.f, 1.f, 1.f } : TextColor{ 0.72f, 0.75f, 0.8f, 1.f };
            text.draw(row.x + 24, row.y + ((row.h - text.cell_h()) / 2), (on ? "> " : "  ") + cores[i].name,
                color, row.w - 48);
        }
        text.flush();

        /*
         * **言語の札**。下敷き → 文字の順で描く（バナーの上に載せる）。
         * 「◀ 日本語 ▶」の形にして、**左右で替わることを絵で言う**——
         * 説明の行を足すと、せっかく消した足の案内が戻ってくる。
         */
        {
            const RectPx lr = lang_rect_at(w, h);
            const auto slots = lang_slots_at(w, h);
            paint.begin(w, h);
            paint.rect(lr, PaintColor{ 0.f, 0.f, 0.f, 0.55f });
            paint.rect(RectPx{ lr.x, lr.y, lr.w, 2 }, PaintColor{ 0.62f, 0.78f, 1.f, 0.8f });
            paint.rect(RectPx{ lr.x, lr.y + lr.h - 2, lr.w, 2 }, PaintColor{ 0.62f, 0.78f, 1.f, 0.8f });
            //! 選んでいる札の下に**帯**を敷く。印だけだと遠目に読み取れない。
            for (const auto &s : slots) {
                if (s.on) {
                    paint.rect(RectPx{ s.box.x - 6, s.box.y + 3, s.box.w + 12, s.box.h - 6 },
                        PaintColor{ 0.24f, 0.36f, 0.56f, 0.9f });
                }
            }
            paint.flush();
            text.begin(w, h);
            for (const auto &s : slots) {
                //! 選んでいない側も**読める明るさ**にする（暗すぎると「無い」ように見える）。
                const TextColor color = s.on ? TextColor{ 1.f, 1.f, 1.f, 1.f }
                                             : TextColor{ 0.72f, 0.75f, 0.8f, 1.f };
                text.draw(s.box.x, s.box.y + ((s.box.h - text.cell_h()) / 2), s.text, color, s.box.w);
            }
            text.flush();
        }

        //! 系譜の札（左上。assets/ui/lineage.json が読めたときだけ出す。
        if (lineage_enabled) {
            const RectPx lb = lineage_button_rect_at(w, h);
            paint.begin(w, h);
            paint.rect(lb, PaintColor{ 0.f, 0.f, 0.f, 0.55f });
            paint.rect(RectPx{ lb.x, lb.y, lb.w, 2 }, PaintColor{ 0.62f, 0.78f, 1.f, 0.8f });
            paint.rect(RectPx{ lb.x, lb.y + lb.h - 2, lb.w, 2 }, PaintColor{ 0.62f, 0.78f, 1.f, 0.8f });
            paint.flush();
            text.begin(w, h);
            text.draw(lb.x + kLangPad, lb.y + ((lb.h - text.cell_h()) / 2), i18n::tr("hd2d.app.lineage.open"),
                TextColor{ 1.f, 1.f, 1.f, 1.f }, lb.w - (kLangPad * 2));
            text.flush();
        }

        /* バーチャルパッド（下敷き → 文字の順で 1 束。盤面より上に載せる）。 */
        paint.begin(w, h);
        text.begin(w, h);
        vpad.draw(paint, text, vpad_settings, RectPx{ 0, 0, w, h }, PadMacroFlags{}, vpad.chord_mask(),
            cfg.pad, std::vector<PadCommand>{});
        paint.flush();
        text.flush();

        /*
         * `--core-select-check`。**swap の前に読む**（既定のフレームバッファは
         * swap で入れ替わるので、後に読むと前のフレームが写る）。
         */
        if (import_enabled) {
        const auto import_box = core_import_entry_rect(w, h);
        paint.begin(w, h); paint.rect(import_box, {.12f, .18f, .28f, 1}); paint.flush();
        text.begin(w, h);
        text.draw(import_box.x + 12, import_box.y + (import_box.h - text.cell_h()) / 2, core_import_caption("hd2d.app.core-import.entry"), {1,1,1,1}, import_box.w - 24);
        if (cores.empty()) {
            std::string help = i18n::tr("hd2d.app.core-import.empty");
            const auto count = text.fit_bytes(help, w - 72);
            text.draw(36, h / 2, help.substr(0, count), {1,1,1,1}, w - 72);
            if (count < help.size()) text.draw(36, h / 2 + text.cell_h() + 8, help.substr(count), {1,1,1,1}, w - 72);
        }
        text.flush();
        }
        ++drawn;
        if ((shot_after > 0) && (drawn >= shot_after)) {
            if (!shot_path.empty()) {
                int sw = 0;
                int sh = 0;
                SDL_GetWindowSize(window.window, &sw, &sh);
                (void)save_framebuffer_bmp(shot_path, sw, sh);
            }
            return -1;
        }

        SDL_GL_SwapWindow(window.window);
        SDL_Delay(16);
    }
}

/*!
 * @brief 起こし直しの申し送り（`kRunRelaunchCore`）。**`run()` を跨いで 1 個**。
 * @details 返り値に載せられないので置き場が要る。`run()` は 1 プロセスに 1 本ずつ
 * しか回らない（呼び手が順に呼び直す）ので、これで足りる。
 */
CoreRelaunch g_core_relaunch;

void set_core_relaunch(const CoreRelaunch &request) { g_core_relaunch = request; }

} // namespace

CoreRelaunch take_core_relaunch()
{
    CoreRelaunch taken = g_core_relaunch;
    g_core_relaunch = CoreRelaunch{}; //!< **取ると空になる。**持ち越すと環が止まらない
    return taken;
}

int run(const AppOptions &options)
{
    if (options.lineage_check) {
        //! 窓も GL も要らない（layout_lineage まで純粋な計算）。
        return run_lineage_check(
            options.lineage_data_path.empty() ? std::string("assets/ui/lineage.json") : options.lineage_data_path);
    }
    if (!options.model_check_path.empty()) {
        ModelAsset asset;
        std::string error;
        if (!load_glb(options.model_check_path, asset, error)) {
            std::fprintf(stderr, "[model] %s\n", error.c_str());
            return 1;
        }
        std::printf("MODEL_CHECK_JSON={\"vertices\":%zu,\"bounds_min\":[%.9g,%.9g,%.9g],"
            "\"bounds_max\":[%.9g,%.9g,%.9g]}\n", asset.vertex_count,
            asset.bounds_min.x, asset.bounds_min.y, asset.bounds_min.z,
            asset.bounds_max.x, asset.bounds_max.y, asset.bounds_max.z);
        return 0;
    }
    if (!options.model_view_path.empty()) {
        return run_model_view(options);
    }
    if (options.model_terrain_check) return run_model_terrain_check(options);
    // 2026-09-08: gameplay GLB models deferred at the user's request.
    // Archived implementation and materials: .
    ModelAsset wall_model_asset, floor_model_asset, player_model_asset;
    std::array<ModelAsset,kTerrainModelCount> terrain_part_assets;
    std::array<bool,kTerrainModelCount> terrain_part_loaded{};
    const bool wall_model_loaded=false, floor_model_loaded=false, player_model_loaded=false;
    if (!options.model_terrain_pack.empty() || !options.model_wall_path.empty()
        || !options.model_floor_path.empty() || !options.model_player_path.empty()) {
        std::fprintf(stderr,"[model] gameplay GLB trial deferred; using existing voxel terrain and entities\n");
    }
    std::string input_error;
    if (!test_keyboard().configure(options.test_input_file, input_error)) {
        std::fprintf(stderr, "[test-keyboard] %s\n", input_error.c_str());
        return 1;
    }
    /*
     * 文言のカタログ（`i18n/lang.h`）。**モードの分岐より前に読む。**
     * 検査のモード（`--ui-check` ほか）は下の分岐でそのまま返ってしまうので、
     * 遊ぶ経路にだけ置くと**検査だけカタログ無しで走る**（ID がそのまま出て、
     * 見出しを名前で探す検査が全部落ちる。実際にそう落とした）。
     * 言語の選択（cfg の `lang`）は設定を読んだ後で当てる。ここでは器だけ用意する。
     */
    i18n::load("assets/lang");
    fx_sprite_set_dir("assets/fx"); //!< 攻撃の視覚効果の絵の板（どくろなど。`render/fx_styles/fx_parts.h`）
    /*
     * `HD2D_LANG` で言語を当てる手段（このツリーの `HD2D_*` と同じ作法）。**cfg より強い。**
     * 検査のモードは cfg を読まないので、これが無いと「日本語以外で走らせる」道が
     * 1 つも無い。訳を当てた画面を撮るときにも使う。
     */
    bool lang_from_env = false;
    if (const char *env_lang = std::getenv("HD2D_LANG"); (env_lang != nullptr) && (env_lang[0] != '\0')) {
        if (i18n::set_language(env_lang)) {
            lang_from_env = true;
            std::fprintf(stderr, "[hd2d] HD2D_LANG=%s\n", env_lang);
        } else {
            std::fprintf(stderr, "[hd2d] HD2D_LANG=%s のカタログがありません（assets/lang）\n", env_lang);
        }
    }
    if (options.town_check) {
        return run_town_check(options); // 実データの町を読む。窓も GL も要らない
    }
    if (options.town_view) {
        return run_town_view(options); // 実データの町を描く（コアは起こさない）
    }
    if (options.world_check) {
        return run_world_check(options); // 窓も GL も要らない
    }
    if (options.motion_check) {
        return run_motion_check(options); // 同上
    }
    if (options.pad_check) {
        return run_pad_check(options); // 同上（窓も GL もコアも要らない）
    }
    if (!options.lut_export_path.empty()) {
        // `--lut-export=`: いまの `GradeParams` を `.cube` に書いて終わる。GL も窓も要らない。
        std::string export_err;
        if (!write_lut_cube(options.lut_export_path, GradeParams{}, kLutSize, export_err)) {
            std::fprintf(stderr, "[hd2d] %s\n", export_err.c_str());
            return 1;
        }
        std::fprintf(stderr, "[hd2d] LUT を書き出しました（%d³）: %s\n", kLutSize, options.lut_export_path.c_str());
        return 0;
    }
    if (options.terrain_check) {
        return run_terrain_check(options);
    }
    if (options.post_check) {
        return run_post_check(options);
    }
    if (options.cutaway_check) {
        return run_cutaway_check(options);
    }
    if (options.ui_check) {
        return run_ui_check(options); // P8
    }
    if (!options.status_hud_check_path.empty()) {
        return run_status_hud_check(options); // HP/MP の玉とバフ・デバフのアイコン
    }
    {
        /*
         * `--light-range-check=出力の頭`（光の届く範囲の検査）。
         * 引数は `forwarded_args` から拾う——ここで読めば、共通の引数表（`desktop_command_line.h`）を
         * 触らずに足せる。見つかったらコアへは転送せず、そのまま検査へ入る。
         */
        static const std::string kLightRangeCheckArg = "--light-range-check=";
        for (const std::string &arg : options.forwarded_args) {
            if (arg.compare(0, kLightRangeCheckArg.size(), kLightRangeCheckArg) == 0) {
                AppOptions check_options = options;
                check_options.light_range_check_path = arg.substr(kLightRangeCheckArg.size());
                return run_light_range_check(check_options);
            }
        }
        if (!options.light_range_check_path.empty()) {
            return run_light_range_check(options);
        }
    }
    {
        /*
         * `--attack-fx-check=出力の頭`（攻撃の視覚効果の検査）。
         * 引数は `--light-range-check=` と同じく `forwarded_args` から拾う（共通の引数表を触らない）。
         * 値が空（`--attack-fx-check=`）なら書き出さずに検査だけ走る。
         */
        for (const std::string &arg : options.forwarded_args) {
            if (arg == "--dcss-fx-check") {
                return dcss::run_dcss_fx_check(); //!< DCSS の攻撃の演出の部品だけの検査（窓・GL・コアなし。`dcss/dcss_fx_check.cpp`）
            }
            if (arg == "--dcss-touch-check") {
                return dcss::dcss_touch_selftest(); //!< DCSS のタッチの決まりと名前の問いの検出の検査（窓・GL・コアなし。`dcss/dcss_touch.cpp`。0 = 全部通った）
            }
        }
        static const std::string kAttackFxCheckArg = "--attack-fx-check=";
        for (const std::string &arg : options.forwarded_args) {
            if (arg.compare(0, kAttackFxCheckArg.size(), kAttackFxCheckArg) == 0) {
                return run_attack_fx_check(options, arg.substr(kAttackFxCheckArg.size()));
            }
        }
    }
    if (options.vr_math_check) {
        //! VR の座標の検査。窓も GL もコアも要らない（純関数だけ）。
        std::string report;
        bool ok = xr::orientation_check(report);
        std::fprintf(stderr, "%s", report.c_str());
        /*
         * **盤の 90° 回転**。純関数なので頭が無くても閉じる。
         * 回る向きをフラットの `rotate_screen_delta` と突き合わせているので、
         * 「VR だけ逆に回る」を実機へ持ち出す前に捕まえられる。
         */
        std::string board_turn_report;
        const bool board_turn_ok = xr::board_turn_check(board_turn_report);
        std::fprintf(stderr, "%s", board_turn_report.c_str());
        ok = ok && board_turn_ok;
        /*
         * **Touch → PadInput の対応も一緒に出す**（§8）。実機が無くても
         * 「どのボタンがどこへ行くか」を目で確かめられるようにするため。
         * 重複（2 つの Touch が同じ `PadInput` へ）は必ず捕まえる——
         * 黙って上書きされると、押しても効かないボタンが 1 個できる。
         */
        std::fprintf(stderr, "[vr-math] Touch → PadInput\n");
        int count = 0;
        const xr::TouchBinding *const bindings = xr::touch_bindings(count);
        bool seen[kPadInputCount] = {};
        for (int i = 0; i < count; ++i) {
            const int slot = static_cast<int>(bindings[i].input);
            const bool dup = (slot >= 0) && (slot < kPadInputCount) && seen[slot];
            if ((slot >= 0) && (slot < kPadInputCount)) {
                seen[slot] = true;
            }
            std::fprintf(stderr, "    %s %-22s -> %-3s  %s\n", dup ? "FAIL" : "OK  ", bindings[i].touch,
                pad_input_name(bindings[i].input), bindings[i].path);
            ok = ok && !dup;
        }
        //! 割り当ての無い `PadInput` は「欠番」として出す（黙って消さない）。
        for (int i = 0; i < kPadInputCount; ++i) {
            if (!seen[i]) {
                std::fprintf(stderr, "    --   %-22s -> %-3s  （Touch に空きが無い。割り当て UI で振り直せる）\n",
                    "(欠番)", pad_input_name(static_cast<PadInput>(i)));
            }
        }
        std::fprintf(stderr, "[hd2d] RESULT: %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    if (options.vr_check) {
        return run_vr_check(options); // VR。窓と GL は要るがコアは起こさない
    }
    /*
     * P9 のボクセルエディタ。**コアを起こさない。**リリース用ビルド
     * （`/p:HengbandHd2dEditor=0`）には入っていないので、そのときは黙って無視せず断る。
     */
    if (options.edit_check || !options.edit_name.empty() || !options.edit_new_name.empty()) {
#if defined(HD2D_EDITOR)
        if (options.edit_check) {
            return run_edit_check(options); // 窓も GL も要らない
        }
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
#if defined(_WIN32)
            (void)::MessageBoxA(nullptr, "SDL の初期化に失敗しました。", "Hengband HD2D", MB_OK | MB_ICONERROR);
#else
            std::fprintf(stderr, "[hd2d] SDL の初期化に失敗しました: %s\n", SDL_GetError());
#endif
            return 1;
        }
        Window editor_window;
        std::string editor_err;
        if (!create_window(options, editor_window, editor_err)) {
            show_message(editor_err);
            SDL_Quit();
            return 1;
        }
        const int editor_result = run_voxel_editor(options, editor_window.window);
        SDL_GL_DeleteContext(editor_window.context);
        SDL_DestroyWindow(editor_window.window);
        SDL_Quit();
        return editor_result;
#else
        std::fprintf(stderr, "[hd2d] この exe にボクセルエディタは入っていません"
                             "（HengbandHd2dEditor=0 で組まれたリリース用ビルドです）\n");
        return 1;
#endif
    }
    if (options.slab_ladder_check) {
        //! 実体の板の粒の受け口（`--slab-ladder-check`）。窓も GL もコアも要らない。
#if defined(HD2D_EDITOR)
        return run_slab_ladder_check(options);
#else
        std::fprintf(stderr, "[hd2d] --slab-ladder-check は書く側（hd2d/edit）が要ります"
                             "（HengbandHd2dEditor=0 で組まれたリリース用ビルドです）\n");
        return 1;
#endif
    }
    if (options.material_check) {
        return run_material_check(options); //!< ライブラリぜんぶの材質の推定（GL 不要）
    }
    if (!options.prefab_name.empty()) {
        // P1 の口。**コアを起こさない**（絵だけを見る／数だけを採る）。
        return options.prefab_check_only ? run_prefab_check(options) : run_prefab_view(options);
    }
    //! **パッドも起こす**（P8）。無い機械でも `SDL_Init` は成功する。
    Uint32 sdl_init_flags = SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER;
#if defined(_WIN32)
    // Preserve Windows input initialization when returning from a finished core.
    // Repeated SDL_Init would otherwise increment the subsystem reference counts.
    sdl_init_flags &= ~SDL_WasInit(sdl_init_flags);
#endif
    if (sdl_init_flags != 0 && SDL_Init(sdl_init_flags) != 0) {
#if defined(_WIN32)
        (void)::MessageBoxA(nullptr, "SDL の初期化に失敗しました。", "Hengband HD2D", MB_OK | MB_ICONERROR);
#else
        std::fprintf(stderr, "[hd2d] SDL の初期化に失敗しました: %s\n", SDL_GetError());
#endif
        return 1;
    }
    Window window;
    std::string err;
    if (!create_window(options, window, err)) {
        show_message(err);
        SDL_Quit();
        return 1;
    }
#if defined(__ANDROID__)
    /*
     * SDL は Android で**窓を作ったとき**に文字入力を開始し、IME が画面の下半分を
     * 覆ったまま始まる。SDL_Init 直後に
     * 呼んでも窓の生成で戻ってしまうので、**窓の後**で引っ込める（旧版の入口と同じ位置）。
     * 名前入力では、主ループが text_input_active の変化に応じて IME を表示する。
     */
    SDL_StopTextInput();
#endif
    if (options.gl_probe_only) {
        // `--gl-probe`: 素性を stderr に出して終わる。窓は出さない。
        std::fprintf(stderr, "[hd2d] gl-probe ok\n");
        SDL_GL_DeleteContext(window.context);
        SDL_DestroyWindow(window.window);
        SDL_Quit();
        return 0;
    }

    /*
     * VR。**`--vr` が無ければ 1 行も走らない。**
     *
     * 立たなかったら理由を出して**フラットで続ける**（設計書 §10）。HMD を外した機械で
     * 起動できなくなるほうが困る。いまは M0 なので、立っても HMD に出るのは
     * クリア色だけ（絵は M1 で `draw_scene` を抜き出してから）。
     */
    xr::Session xr;
    /*!
     * 盤の据え方（§5）。**最初のフレームで 1 回据えたら、再中心の操作でしか動かない。**
     * 毎フレーム置き直すと盤が頭についてきて、覗き込めないうえに酔う。
     */
    xr::BoardPlacement vr_board;
    /*!
     * @brief ジオラマの盤の 90° 回転。
     *
     * @details **フラットの `camera_turn` とは別の状態**である（VR の間だけ生きる）。
     * 同じ変数にすると、HMD を外した瞬間に平らな画面が勝手に回る。
     * @note **保存しない・再中心で 0 に戻す**（設計書 §4.3）。卓の上の盤を持ち替える操作に
     * 相当するので、据え直したら向きも素へ戻るのが自然である。
     */
    int vr_board_turn = ((options.vr_board_turn % xr::kBoardTurnCount) + xr::kBoardTurnCount) % xr::kBoardTurnCount;
    bool vr_board_placed = false;
    //! いま据えてある盤が一人称のものか（見せ方を切り替えたら据え直す。§17）。
    bool vr_board_first_person = false;
    /*!
     * @brief 部屋と卓（§20。2026-08-14 に決めた）。**ジオラマのときだけ立つ。**
     * @details 一人称では `visible == false`——あちらは「完全にゲーム空間に入り込む」ので、
     * 部屋を置くと世界が二重になる。
     */
    xr::RoomPlacement vr_room;
    /*!
     * @brief 再中心したときの頭。**盤も卓も板もこれを基準に据わる。**
     * @details 板の置き場所は毎フレームここから組み直す（割り付けが変わっても
     * ついてくるように）。スナップ振り向きではこれを回す（§17.3）。
     */
    xr::HeadAnchor vr_anchor;
    xr::RoomRenderer vr_room_renderer;
    bool vr_room_ready = false;
    //! 疑似 HMD で板を貼るための面（実物ではクワッドレイヤなので使わない）。
    xr::UiSurface vr_fake_ui;
    //! この回に積む板（毎フレーム組み直す）。
    std::vector<xr::UiPanel> vr_panels;
    /*!
     * @brief 板の向き（ステージの y 軸まわり）。**頭には貼り付けない**（§22）。
     * @details 「向いている方角の頭上に固定」。遊びの角の外へ出たぶんだけ
     * `xr::follow_panel_yaw` が寄せる。再中心のときに頭の向きへ揃える。
     */
    float vr_panel_yaw = 0.f;
    //! スナップ振り向きの再武装（倒しっぱなしで回り続けないように）。
    bool vr_snap_armed = true;
    //! `--vr` と `--vr-fake` は排他（§10）。両方来たら実物を採る。
    if (options.vr && options.vr_fake) {
        std::fprintf(stderr, "[hd2d] --vr と --vr-fake は同時に使えません。--vr を採ります。\n");
    }
    xr::Input xr_input;
    if (options.vr) {
        /*
         * 描画解像度の倍率は cfg にしか無く、
         * **設定を読むのはこの下**である。swapchain は `init` の中で作られてしまい
         * 後から掛け直す手段が無いので、ここでは cfg を直に引く。
         *
         * **cfg が無くても必ず渡すこと。**読めたときだけ渡す形にしていたので、
         * cfg を持たない機（＝入れたばかりの機）では既定が一度も効かなかった。
         * 既定は `Hd2dSettings` の初期化子（`kVrRenderScaleDefault`）が持っているので、
         * **読めなければそのままの値が答え**である。既定はどの機でも 1.0（§15.5）なので、
         * この行があっても絵は動かない——効くのは遊ぶ人が自分で下げたときだけである。
         */
        Hd2dSettings stored_for_xr;
        (void)load_settings(default_settings_path(), stored_for_xr);
        xr.set_render_scale(stored_for_xr.vr_render_scale);
        std::string xr_err;
        if (xr.init(xr_err)) {
            //! **歩調は `xrWaitFrame` が握る。**vsync と二重に待つと即座に半分へ落ちる（罠 5）。
            SDL_GL_SetSwapInterval(0);
            /*
             * Touch（§8）。**立たなくても VR は続く**——キーボードと机のパッドで遊べる。
             * `xrAttachSessionActionSets` は 1 度しか通らないので、ここで 1 回だけ。
             */
            std::string input_err;
            if (!xr_input.init(xr.native_instance(), xr.native_session(), input_err)) {
                std::fprintf(stderr, "[hd2d] --vr: %s（Touch は効きません）\n", input_err.c_str());
            }
        } else {
            std::fprintf(stderr, "[hd2d] --vr: %s\n[hd2d] フラットで続けます。\n", xr_err.c_str());
        }
    }

    TextOverlay text;
    VoxelRenderer renderer;
    VoxelRenderer item_icon_renderer;
    UiPaint paint; //!< P8。パネルの下敷き・枠・ゲージ（文字より先に流す）
    UiImagePainter images; //!< P8。タイトル画とパネルの背面
    if (!text.init(kTextPx, err) || !renderer.init(err) || !item_icon_renderer.init(err) || !paint.init(err) || !images.init(err)) {
        show_message(err);
        SDL_GL_DeleteContext(window.context);
        SDL_DestroyWindow(window.window);
        SDL_Quit();
        return 1;
    }
    /*
     * VR の部屋と卓（§20）。**VR のときだけ組む**（フラットでは 1 バイトも要らない）。
     * 作れなくても致命ではない——部屋と卓が出ないだけで、盤は今までどおり浮かぶ。
     */
    if (options.vr || options.vr_fake) {
        std::string room_err;
        vr_room_ready = vr_room_renderer.init(room_err);
        if (!vr_room_ready) {
            std::fprintf(stderr, "[hd2d] VR の部屋を作れませんでした: %s（卓は出ません）\n", room_err.c_str());
        }
    }
    /*
     * 背面の 1 枚絵（P8）。**読めなくても致命ではない**（無くても遊べる）ので、
     * 理由だけ出して続ける。出ないことに気づけるよう、黙って諦めない。
     */
    UiBackdrops backdrops;
    UiBackdrops backdrops_orb_only; //!< 「背面の絵」を切ったとき用（玉の素材だけを持つ）
    UiImage title_image;
    UiImage panel_bottom_image;
    UiImage panel_right_image;
    UiImage orb_back_image;
    UiImage orb_glass_image;
    UiImage orb_liquid_hp_image;
    UiImage orb_liquid_mp_image;
    UiImage orb_glass_top_image;
    UiImage orb_front_image;
    UiImage status_icons_image;
    UiImage weather_icons_image;
    {
        std::string image_err;
        if (!title_image.load(kTitleImagePath, image_err)) {
            std::fprintf(stderr, "[hd2d] %s\n", image_err.c_str());
        }
        if (!panel_bottom_image.load(kPanelBgBottomPath, image_err)) {
            std::fprintf(stderr, "[hd2d] %s\n", image_err.c_str());
        }
        if (!panel_right_image.load(kPanelBgRightPath, image_err)) {
            std::fprintf(stderr, "[hd2d] %s\n", image_err.c_str());
        }
        backdrops.painter = &images;
        backdrops.title = &title_image;
        backdrops.panel_bottom = &panel_bottom_image;
        backdrops.panel_right = &panel_right_image;
        /*
         * HP/MP の玉の 6 層とバフ・デバフのアイコンのシート。ここで読むのは
         * **変愚の素材**（`assets/ui/hud/` 直下。コアが決まる前の既定で、変愚と知らないコアはこのまま使う）。
         * コアごとの意匠は、握手でコア名が届いた後に読み替える（下の「玉の配置と素材」。同 §6）。
         * **読めなくても致命ではない**（読めない層があれば玉もアイコンも出さず、元の棒を描く）。
         * 縮めて貼る絵なのでミップマップを作る（`load_minified`）。
         */
        const std::pair<UiImage *, const char *> orb_files[] = {
            { &orb_back_image, kOrbBackPath }, { &orb_glass_image, kOrbGlassPath },
            { &orb_liquid_hp_image, kOrbLiquidHpPath }, { &orb_liquid_mp_image, kOrbLiquidMpPath },
            { &orb_glass_top_image, kOrbGlassTopPath }, { &orb_front_image, kOrbFrontPath },
            { &status_icons_image, kStatusIconsPath },
            { &weather_icons_image, kWeatherIconsPath },
        };
        for (const auto &file : orb_files) {
            if (!file.first->load_minified(file.second, image_err)) {
                std::fprintf(stderr, "[hd2d] %s（HP/MP の玉は元の棒で出します）\n", image_err.c_str());
            }
        }
        backdrops.orb.painter = &images;
        backdrops.orb.back = &orb_back_image;
        backdrops.orb.glass = &orb_glass_image;
        backdrops.orb.liquid_hp = &orb_liquid_hp_image;
        backdrops.orb.liquid_mp = &orb_liquid_mp_image;
        backdrops.orb.glass_top = &orb_glass_top_image;
        backdrops.orb.front = &orb_front_image;
        backdrops.orb.status_icons = &status_icons_image;
        backdrops.orb.weather_icons = &weather_icons_image;
        //! 「背面の絵」の設定を切っても玉は出す（玉は絵ではなく HP/MP の表示そのもの）。
        backdrops_orb_only.painter = &images;
        backdrops_orb_only.orb = backdrops.orb;
    }
    /*
     * 階が変わったときのカットイン演出（P10。2026-08-11 に決めた。`ui/floor_cutin.h`）。
     * **読めなくても致命ではない**（演出が出ないだけ）ので、理由だけ出して続ける。
     */
    FloorCutin floor_cutin;
    {
        std::string cutin_err;
        if (!floor_cutin.init(cutin_err)) {
            std::fprintf(stderr, "[hd2d] カットイン演出を用意できませんでした: %s\n", cutin_err.c_str());
        }
    }
    GamePad pad; //!< P8。十字／スティックで歩き、ボタンでコマンドを出す
    /*
     * 修-03：すでに繋がっている機器をここで開く。言語切り替えの起こし直し（`kRunRelaunchCore`）は
     * `SDL_Quit` をしないので、機器の追加の知らせが二度と来ない。知らせだけに頼ると十字キーが死ぬ。
     */
    pad.open_present_devices();
    /*!
     * バーチャルパッド（2026-08-12 に決めた）。**物理パッドと同じ出来事を出す**ので、
     * この先の経路（割り当て・メニュー操作・一人称）は 2 つのパッドを区別しない。
     */
    VirtualPad vpad;
    std::vector<PadCommand> pad_commands;
    //! いまのキー配置の種類（`pad_commands.current_keymap`。"original" / "rogue"）。
    std::string pad_keymap{ "original" };
    //! どのボタンにマクロが乗っているか（`macro_triggers` で届く。表示にだけ使う）。
    PadMacroFlags pad_macros;
#if !defined(__ANDROID__)
    SDL_StartTextInput(); //!< 印字文字は `SDL_TEXTINPUT` から採る
#else
    /*
     * Android で StartTextInput は「ソフトキーボードを出す」を意味する。常時出すと
     * IME が画面の下半分を覆ったままになる（旧版 A-T8）ので、ここでは出さない。
     * 印字文字のコマンドはその間使えない（カーソル操作と Enter/ESC は KEYDOWN で届く）。
     * 名前入力などの場面で自動で出す仕組みは AH-02の宿題。
     */
#endif

    /*
     * プレハブライブラリ（P10）。**読めなくても起動は止めない**（遊べることが先。
     * 仮の箱と板で続け、理由を stderr へ出す。検査の側はライブラリが無いと FAIL する）。
     *
     * **宣言だけここへ上げてある。**読み込みはコアを選んだ瞬間に別スレッドで始まり
     * （2026-08-23 に決めた「拡大の裏でロードが始まる」）、本体は下で待ち合わせる。
     */
    PrefabLibrary library;
    TerrainMemory terrain_memory;
    //! 一度置いた小物を覚えておくロック（P10。`world/terrain_view.h` の `PropLatch`）。
    PropLatch prop_latch;
    PrefabLibrary *library_ptr = nullptr;
    //! ライブラリを読むスレッド（GL は触らない）。**選択画面を出さない経路では走らない**（joinable が偽）。
    std::thread library_thread;
    std::atomic<bool> library_busy{ false }; //!< 画を回し続けるかの判定（主スレッドが読む）
    bool library_load_ok = false; //!< スレッドの結果。**join した後に読む**（それが待ち合わせ）
    std::string library_load_err;
    //! 読み込みと GPU 転送にかかった時間（起動の重さの内訳。どちらが効いているかを見るため）。
    Uint32 library_load_ms = 0;
    //! 演出のフレームから汲んだ結果（`pump_upload`）。落ちたら下の待ち合わせで理由を出す。
    bool library_pump_ok = true;
    std::string library_pump_err;

    /*
     * --- どのコアを起こすか（設計 §6.1）---
     *
     * **接続より前**である。ここで決めないと、`link.start()` が既定の
     * `HengbandCore.exe` を起こしてしまう。設定は cfg から**この 1 件だけ**先に読む
     * （本体の読み込みは接続の後。VR の板の寸法が :7737 で同じことをしているのと同じ形）。
     *
     * **素材の読み込みより前でもある**（2026-08-23 に決めた「起動が遅い」）。
     * 以前はプレハブライブラリ（2500 個超）を読んで GPU へ載せた後にこの画面を出していた。
     * コアを増やすたびにライブラリが太るので、**選択画面が出るまでに 7 秒**かかっていた
     * （実測。窓は 0.4 秒で出るのに、その後ずっと黒いままだった）。コアはここでも
     * 1 本しか起こしていない——遅いのはライブラリであって、コアの起動ではない。
     * ライブラリはどのコアでも同じものを使うので、待たせる場所を**選んだ後**へ移した。
     * ここで使う道具（窓・GL・`TextOverlay`・`UiPaint`・`UiImagePainter`・`GamePad`）は
     * すべてこの行より上で用意されている。**これらより前へは動かせない。**
     *
     * 規則は 3 つ:
     * - `--core-path=` が有れば画面を出さない（スクリプト・検査のため）
     * - 登録が 1 件以下なら画面を出さない（**既存遊ぶ人の起動を変えない**）
     * - それ以外は一覧を出し、前回選んだものにカーソルを載せる
     */
    std::vector<CoreEntry> core_list;
    /*!
     * @brief **コア選択の画を出したか**（2026-08-23）。
     * @details 遊び終えたときにそこへ戻すかの判断に使う（`kRunRestart`）。
     * `--core-path=` で飛ばしたときや登録が 1 件のときは立たない——戻る先が無い。
     */
    bool core_select_shown = false;
    std::string core_choice = options.core_path;
    /*!
     * @brief 言語切り替えで起こし直している回か（`kRunRelaunchCore` の続き）。
     * @details 立っていたら**コア選択の画を出さない**——同じコアをそのまま起こす。
     * 「画を出したか」は前の回のものを持ち越す（遊び終えたときの行き先が変わらないように）。
     */
    const bool relaunching = !options.relaunch.core_path.empty();
    if (relaunching) {
        core_choice = options.relaunch.core_path;
        core_select_shown = options.relaunch.core_select_shown;
        std::fprintf(stderr, "[hd2d] 言語を替えたのでコアを起こし直します: %s%s\n", core_choice.c_str(),
            options.relaunch.resume ? "（--resume）" : "");
    }
    {
        Hd2dSettings core_cfg;
        (void)load_settings(default_settings_path(), core_cfg);
        /*
         * **cfg の一覧はそのまま信じない**（`sanitize_cores`。2026-08-23 に気づいた）。
         * 在らない道と重複をここで落とす。全部落ちれば下の初回扱い（自動登録）へ流れる。
         *
         * 1 つでも落ちたら**残りも捨てて作り直す**。並び順は遊ぶ人の記憶なので普段は
         * 触らないが、壊れた cfg の並びは記憶ではなく壊れ方の跡である——今回の
         * 化けでは、生き残った 1 件（`Sil-Q`）が先頭に居座り、直したはずの一覧が
         * 元の並びに戻らなかった。**壊れていたと分かったら並びも信じない。**
         */
        core_list = sanitize_cores(options, core_cfg.cores);
        if (core_list.size() != core_cfg.cores.size()) {
            std::fprintf(stderr, "[hd2d] cfg のコア一覧が壊れていたので作り直します\n");
            core_list.clear();
        }
        if (core_list.empty()) {
            core_list = discover_cores(options); // 初回。**在るものだけ**
        } else {
            /*
             * **後から増えたコアを継ぎ足す。** cfg に `cores=` が在ると初回の
             * 自動登録は走らないので、そのままだと**新しいコアを置いても一覧に
             * 出ない**（隣に exe が在るのに選べない）。既存の並び順は変えず、
             * 見つかったのに載っていないものだけを末尾へ足す——順序は遊ぶ人の
             * 記憶でもあるので、勝手に並べ替えない。
             */
            for (const CoreEntry &found : discover_cores(options)) {
                bool known = false;
                for (const CoreEntry &have : core_list) {
                    if (same_core_path(have.path, found.path)) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    std::fprintf(stderr, "[hd2d] new core found: \"%s\" -> %s\n",
                        found.name.c_str(), found.path.c_str());
                    core_list.push_back(found);
                }
            }
        }
        /*
         * **言語は `hd2d.cfg` が持つ**（2026-08-26 に決めた「共通にして、
         * コア選択画面に言語切り替えボタンを作ろう」）。コアごとのファイルにも
         * `lang=` は書かれるが、**読むのはこちらだけ**である——言語はコアを選ぶ前に
         * 要るし、コアを替えるたびに画面の言葉が変わるのは筋が通らない。
         *
         * **コア選択の画より前に当てる。** 前はこの当てが後ろに在ったので、
         * **選択画面だけ既定の言語**で出ていた（英語にしていても日本語で出ていた）。
         *
         * `HD2D_LANG` が指定されていたら**そちらが勝つ**（註のとおり。§21.4 で
         * 註と実物が逆になっていたのをここで揃えた）。
         */
        if (!lang_from_env && !core_cfg.lang.empty() && !i18n::set_language(core_cfg.lang)) {
            std::fprintf(stderr, "[hd2d] lang=%s のカタログが無いので %.*s で開きます\n",
                core_cfg.lang.c_str(), static_cast<int>(i18n::current().size()), i18n::current().data());
        }
        if (options.core_select_check) {
            /*
             * 画面だけを確かめて終わる（`--core-select-check`）。一覧を出し、
             * `--shot=` が有ればその画を書いて 0 で返る。**コアは起こさない。**
             */
            std::fprintf(stderr, "[hd2d] core-select-check: %u entr%s\n",
                static_cast<unsigned>(core_list.size()), (core_list.size() == 1) ? "y" : "ies");
            for (const CoreEntry &entry : core_list) {
                std::fprintf(stderr, "[hd2d]   \"%s\" -> %s%s\n", entry.name.c_str(), entry.path.c_str(),
                    (entry.path == core_cfg.last_core) ? "  (last)" : "");
            }
            int initial = 0;
            for (std::size_t i = 0; i < core_list.size(); ++i) {
                if (core_list[i].path == core_cfg.last_core) {
                    initial = static_cast<int>(i);
                    break;
                }
            }
            const int shot_frames = (options.shot_after_frames != 0) ? std::abs(options.shot_after_frames) : 30;
            // コア未登録でも有効な初期画面として、インポートの入口を表示する。
            (void)run_core_select(core_list, initial, window, text, paint, images, pad,
                core_cfg, {}, {}, options.shot_path, shot_frames);
            std::fprintf(stderr, "[hd2d] RESULT: PASS\n");
            text.shutdown();
            SDL_GL_DeleteContext(window.context);
            SDL_DestroyWindow(window.window);
            SDL_Quit();
            return 0;
        }
        if (!options.lineage_shot_path.empty() || options.lineage_view) {
            /*
             * `--lineage-shot=`。系譜図の画面を直接開いて撮って終わる。**コアは起こさない。**
             */
            std::vector<std::string> avail;
            for (const CoreEntry &entry : core_list) {
                const std::string key = core_key_for_path(entry.path);
                if (!key.empty()) avail.push_back(key);
            }
            std::string initial_key = options.lineage_initial_key;
            if (initial_key.empty()) {
                for (const CoreEntry &entry : core_list) {
                    if (entry.path == core_cfg.last_core) { initial_key = core_key_for_path(entry.path); break; }
                }
            }
            const int lineage_shot_frames = (options.shot_after_frames != 0) ? std::abs(options.shot_after_frames) : 30;
            (void)run_lineage(window.window, text, paint, images, &pad, core_cfg, avail, initial_key,
                options.lineage_shot_path, lineage_shot_frames,
                options.lineage_data_path.empty() ? std::string("assets/ui/lineage.json") : options.lineage_data_path);
            std::fprintf(stderr, "[hd2d] RESULT: PASS\n");
            text.shutdown();
            SDL_GL_DeleteContext(window.context);
            SDL_DestroyWindow(window.window);
            SDL_Quit();
            return 0;
        }
        if (core_choice.empty()) {
            int initial = 0;
            for (std::size_t i = 0; i < core_list.size(); ++i) {
                if (core_list[i].path == core_cfg.last_core) {
                    initial = static_cast<int>(i);
                    break;
                }
            }
            /*
             * 決めた瞬間に**ライブラリを別スレッドで読み始める**（演出は `run_core_select` の中）。
             * `library.load()` は GL を触らないのでこれで安全。GPU 転送は下の待ち合わせの後。
             */
            const auto begin_library_load = [&]() {
                library_busy.store(true);
                library_thread = std::thread([&]() {
                    const Uint32 load_began = clock::ticks();
                    library_load_ok = library.load(options.voxel_dir, library_load_err);
                    library_load_ms = clock::ticks() - load_began;
                    library_busy.store(false);
                });
            };
            /*
             * 演出の 1 フレームぶんの仕事。**読み込みが済んだら、そのまま載せ始める。**
             * 予算 6 ms は 60 fps の 1 フレーム（16.6 ms）の中で絵を描く余裕を残す値。
             * ここで全部やろうとすると演出がかくつく——**演出を止めないことが目的**なので
             * 予算のほうを優先する（残りは次のフレームで汲む）。
             */
            const auto library_still_loading = [&]() {
                if (library_busy.load()) {
                    return true; // まだ .vox を読んでいる（GL に渡せるものがまだ無い）
                }
                if (!library_load_ok) {
                    return false; // 読めなかった。載せる仕事も無い（下で理由を出す）
                }
                if (!library_pump_ok) {
                    return false; // 載せる途中で落ちた。理由は下で出す
                }
                library_pump_ok = library.pump_upload(renderer, kCoreSelectUploadBudgetMs, library_pump_err);
                return library_pump_ok && library.upload_busy();
            };
            int picked;
            do {
                picked = run_core_select(core_list, initial, window, text, paint, images, pad, core_cfg,
                    begin_library_load, library_still_loading);
                if (picked == -2) {
                    core_list = sanitize_cores(options, core_list);
                    for (const auto &found : discover_cores(options)) {
                        if (std::none_of(core_list.begin(), core_list.end(), [&](const CoreEntry &entry) { return same_core_path(entry.path, found.path); })) core_list.push_back(found);
                    }
                    initial = 0;
                }
            } while (picked == -2);
            // 明示的な --core-path 指定以外は、本数に関係なく選択画面へ戻れる。
            core_select_shown = true;
            if (picked < 0) {
                //! 閉じられた。**コアを起こさずに**畳む（起こしてから殺すと子が残りうる）。
                text.shutdown();
                SDL_GL_DeleteContext(window.context);
                SDL_DestroyWindow(window.window);
                SDL_Quit();
                return 0;
            }
            core_choice = core_list[static_cast<std::size_t>(picked)].path;
        }
    }

    /*
     * 仮の地形（P2 ⑤）。**壁なら箱・床なら平板**の 2 種類だけ。
     * 床の板は地面より下（`origin_z = -2`）に置いて、天面がちょうど z = 0 に来るようにする。
     * P10 からは本物のプレハブ（下のライブラリ）が主役で、これは**ライブラリが読めないときの避難先**と
     * 壁の下敷き（`under_slabs`）に残る。
     */
    Prefab wall_prefab = make_box_prefab("wall", 32, 32, 32, 0, 32);
    Prefab floor_prefab = make_box_prefab("floor", 32, 32, 2, -2, 32);
    GpuPrefab wall_gpu;
    GpuPrefab floor_gpu;
    if (!renderer.upload(wall_prefab, wall_gpu, err) || !renderer.upload(floor_prefab, floor_gpu, err)) {
        show_message("仮の地形を GPU へ載せられませんでした:\n" + err);
        SDL_GL_DeleteContext(window.context);
        SDL_DestroyWindow(window.window);
        SDL_Quit();
        return 1;
    }
    /*
     * プレハブライブラリ（P10）。**読めなくても起動は止めない**（遊べることが先。
     * 仮の箱と板で続け、理由を stderr へ出す。検査の側はライブラリが無いと FAIL する）。
     */
    {
        /*
         * 読み込みの本体。**選択画面の裏で回っていたぶんはここで待ち合わせる。**
         * GPU への転送（`upload_gpu`）は GL を触るので**必ず主スレッド**で、待ち合わせた後に行う
         * （`load` は GL を触らない＝別スレッドで回せる。`prefab_library.cpp` の「GL 不要」の註）。
         */
        std::string lib_err;
        bool loaded = false;
        if (library_thread.joinable()) {
            library_thread.join();
            loaded = library_load_ok;
            lib_err = library_load_err;
        } else {
            //! 選択画面を出さなかった経路（`--core-path=` / コアが 1 本）。従来どおりここで読む。
            const Uint32 load_began = clock::ticks();
            loaded = library.load(options.voxel_dir, lib_err);
            library_load_ms = clock::ticks() - load_began;
        }
        if (!loaded) {
            std::fprintf(stderr, "[hd2d] プレハブライブラリを読めませんでした（仮の箱と板で続けます）:\n%s\n",
                lib_err.c_str());
        } else if (!library_pump_ok) {
            //! 演出のフレームで載せている途中に落ちていた。理由はそのとき採ったもの。
            std::fprintf(stderr, "[hd2d] ライブラリを GPU へ載せられませんでした（仮の箱と板で続けます）:\n%s\n",
                library_pump_err.c_str());
            library.release_gpu(renderer);
        } else if (!library.upload_gpu(renderer, lib_err)) {
            std::fprintf(stderr, "[hd2d] ライブラリを GPU へ載せられませんでした（仮の箱と板で続けます）:\n%s\n",
                lib_err.c_str());
            library.release_gpu(renderer);
        } else {
            library_ptr = &library;
            /*
             * 内訳も出す。**起動の重さはほぼ全部ここ**なので、次に遅いと言われたときに
             * どちらが効いているかを推測せずに済む。
             *
             * **後ろの数字はほぼメッシュ化（CPU）である。** GL の呼び出しは 225 ms しかない
             * （実測 2026-08-23。プレハブ 2,514 個）。だから `upload_gpu` は
             * **メッシュ化を多スレッドで先回りさせ、GL だけ主スレッドで**回す
             * 直列だった頃は 5,980 ms、いまは 1,010 ms。
             */
            std::fprintf(stderr,
                "[hd2d] プレハブライブラリ: %d 個（terrain_prefabs.jsonc。読み込み %u ms / メッシュ化と GPU 転送 %u ms）\n",
                library.count(), static_cast<unsigned>(library_load_ms), library.upload_ms());
        }
    }
    /*
     * --- 実体のボクセル板（2026-08-15。ビルボードから置き換えた）---
     * 素材は `tools/voxel/tile_to_slab.py` が `assets/voxel/slab/` へ焼く。
     * **画面に出た索引だけ**読んで GPU へ載せる（`world/slab_library.h`）。
     */
    SlabLibrary slab_library;
    slab_library.set_dir(options.voxel_dir + "/slab");
    if (!slab_library.init(err)) {
        show_message("実体の板の用意に失敗しました:\n" + err);
        SDL_GL_DeleteContext(window.context);
        SDL_DestroyWindow(window.window);
        SDL_Quit();
        return 1;
    }
    /*
     * --- 影（P5 ①）---
     * **失敗しても続ける。**影が無くても遊べるので、ここで起動を止める理由が無い。
     * `shadow.ready()` が偽なら影のパスを丸ごと飛ばし、シェーダには 0 を渡す（＝影の強さ 0）。
     */
    ShadowMap shadow;
    if (!shadow.init(kShadowMapSide, err)) {
        std::fprintf(stderr, "[hd2d] 影を用意できませんでした（影なしで続けます）: %s\n", err.c_str());
    }
    /*
     * --- 空（地上の天球。2026-08-11 に決めた）---
     * **影と同じく、失敗しても続ける。**空が無くても遊べる（背景が単色に戻るだけ）。
     * テクスチャ（星・太陽・月）はここで焼く。絵の資産は持たない（`render/sky_dome.h`）。
     */
    SkyDome sky;
    SkyState sky_state;
    if (!sky.init(err)) {
        std::fprintf(stderr, "[hd2d] sky dome unavailable (continuing without sky): %s\n", err.c_str());
    }
    /*
     * --- ブロックの雲（2026-08-11 に決めた）---
     * 空と同じ扱い: 失敗しても続ける（雲が無くても遊べる）。模様はここで焼く。
     */
    CloudLayer clouds;
    if (!clouds.init(err)) {
        std::fprintf(stderr, "[hd2d] cloud layer unavailable (continuing without clouds): %s\n", err.c_str());
    }
    /*
     * --- 空中に浮かぶ埃（2026-08-23 に決めた）---
     * 雲と同じ扱い: 失敗しても続ける（埃が無くても遊べる）。粒の種はここで 1 度だけ撒く。
     */
    DustMotes dust;
    if (!dust.init(err)) {
        std::fprintf(stderr, "[hd2d] 埃を用意できませんでした（埃なしで続けます）: %s\n", err.c_str());
    }
    /*
     * --- 雨と雪（天候。2026-10-18）---
     * 埃と同じ扱い: 失敗しても続ける。コアが天候を申告したときだけ使う（`weather_view`）。
     */
    WeatherParticles weather_particles;
    if (!weather_particles.init(err)) {
        std::fprintf(stderr, "[hd2d] 雨と雪を用意できませんでした（出さずに続けます）: %s\n", err.c_str());
    }
    WeatherView weather_view;
    WeatherState forced_weather_state;
    const bool force_weather = forced_weather(forced_weather_state); //!< `HD2D_FORCE_WEATHER`（検査用）
    if (force_weather) {
        std::fprintf(stderr, "[hd2d] HD2D_FORCE_WEATHER: 天候を 雨%d 風%d 気温%d に差し替えます（コアが申告したときだけ。コアには影響しません）\n",
            forced_weather_state.rain, forced_weather_state.wind, forced_weather_state.temp);
    }
    /*
     * --- ポスト処理（P7）---
     * **影と違って、失敗したら続けない。**トーンマップがここにしか無いので、
     * 無いまま進むと線形の HDR が画面へ直に出て白く飛ぶ（`init_post_chain` の注記）。
     */
    PostChain post;
    const PostFlags post_flags = resolve_post_flags(options);
    PostParams post_params;
    if (!init_post_chain(post, options, err)) {
        show_message("ポスト処理を用意できませんでした:\n" + err);
        shadow.shutdown();
        slab_library.shutdown(&renderer);
        renderer.shutdown();
        text.shutdown();
        SDL_GL_DeleteContext(window.context);
        SDL_DestroyWindow(window.window);
        SDL_Quit();
        return 1;
    }
    TileCatalog tile_catalog;
    EntityTracker entities;
    /*!
     * @name アスキー実体
     *
     * @details **要ると分かってから用意する**（`glyph_atlas.init()`）。アトラスは 1024×1024 の
     * RGBA（4MB）で、板のまま遊ぶ人には 1 バイトも要らない。用意に失敗したら
     * **板へ落とす**——文字が出ないまま実体が消えるより、従来の絵のほうがまし。
     * @{
     */
    /*!
     * @name 2D アスキー地図の文字
     *
     * @details UI の文字（`text`）とは**別のマス**を使えるようにするため、専用の
     * `TextOverlay` を持つ。2026-08-19 に決めた「設定で回せるように」。
     *
     * **要ると分かってから作る**（`GlyphAtlas` と同じ流儀）。アトラスは 1024×1024 の
     * `GL_R8`（1MB）で、3D のまま遊ぶ人には 1 バイトも要らない。
     * 大きさを変えたら作り直す（`ascii_text_px` が今のマスを覚えている）。
     * 用意できなかったら **UI の文字へ落とす**——地図が出ないより字が小さいほうがまし。
     */
    OptionalRenderResources optional_render;
    bool player_model_attempted = false;
    bool terrain_models_attempted = false;
    bool terrain_models_reported = false;
    bool player_model_reported = false;
    auto &ascii_text = optional_render.ascii_text;
    auto &ascii_text_px = optional_render.ascii_text_px; //!< いま焼いてある大きさ（0 = 用意していない）
    auto &ascii_text_failed = optional_render.ascii_text_failed;
    //! このフレームで専用の字を積んだか（流すのは `paint.flush()` の直後 1 か所）。
    auto &ascii_text_pending = optional_render.ascii_text_pending;
    /*! @} */
    auto &glyph_atlas = optional_render.glyph_atlas;
    auto &glyph_boards = optional_render.glyph_boards;
    auto &glyph_ready = optional_render.glyph_ready;
    auto &glyph_failed = optional_render.glyph_failed;
    /*! @} */
    /*!
     * @brief 足元のリング（SQ-1。`render/ground_ring.h`）。
     * @details 警戒度と照準を輪で見せる。**要ると分かってから用意する**（字の目録と同じ）
     * ——出さないコアでは `rings` が常に空なので、シェーダも組まれない。
     */
    auto &ground_rings = optional_render.ground_rings;
    /*!
     * @brief **上空から差し込む光の柱**（2026-08-22 に決めた。陽だまり）。
     * @details 立てるマスは `TerrainView::light_shafts`（対応表の `light_shaft`）。
     * 足元のリングと同じで、**要るまで作らない**（柱を持たない階では 1 バイトも使わない）。
     */
    auto &light_shafts = optional_render.light_shafts;
    bool shaft_failed = false;
    bool ring_failed = false;
    EntityView entity_view;
    /*!
     * @brief 一過性の重ね書きの字（SQ-2。`world/overlay_view.h`）。
     * @details 実体とは**別の列**。板で描いていても重ね書きは字なので、
     * `EntityView::glyphs`（板と排他）へは混ぜられない。
     */
    std::vector<BillboardInstance> overlay_glyphs;

    TerrainView terrain;
    //! 光る床（P7。溶岩・発光地形）。フレームごとに詰め直す。
    std::vector<InstanceData> emissive_slabs;
    FloorMeaning meaning;
    int meaning_rebuilds = 0;
    //! 町の敷地の分解（P10 第 2 期・§10）。**意味づけを作り直したときだけ**組み直す。
    TownPlan town_plan;
    //! 岩山の記憶（`world/town_plan.h`）。**町が変わったら中で捨てる。**
    CragMemory crag_memory;
    //! 光と影（P5）。フレームをまたいで持つのは、画面の下の行に出すため。
    SceneLighting lighting;
    /*!
     * @name 画調
     * @details `look` は毎フレーム `settings.scene_look` から作り直す。
     * `look_lut_baked` は**いま LUT に焼いてある画調**で、これが変わったときだけ焼き直す
     * （LUT を焼くのは 32³ の格子なので毎フレームやる仕事ではない）。
     * @{
     */
    LookParams look;
    SceneLookKind look_lut_baked = SceneLookKind::Standard;
    //! いま LUT に焼いてあるセピアの量（`settings.sepia`）。変わったときだけ焼き直す。
    float sepia_lut_baked = 0.f;
    /*!
     * @name 面のアスキー文字
     * @details 字のシートは `GlyphAtlas` が持つ（フォントを 2 度開かない）。ここが持つのは
     * **マスごとの記号と色のテクスチャ**（`GL_RGBA8UI`。R = 文字コード, GBA = 前景色）で、
     * 中身は `TerrainMemory` の記憶から毎フレーム詰め直す。
     * @{
     */
    LookGlyphSource look_glyphs;
    GLuint look_cells_tex = 0;
    int look_cells_w = 0;
    int look_cells_h = 0;
    std::vector<std::uint8_t> look_cells_rgba;
    bool look_sheet_ready = false;
    bool look_sheet_failed = false;
    /*! @} */
    /*! @} */
    //! 実際に使った光の状態。`HD2D_FORCE_TIME` があるので `frame.lighting` とは限らない。
    LightingState light_state;
    int dropped_lights = 0;
    //! 点光源の溢れを標準エラーへ書いたか（**1 回きり**。画面には出さない。2026-08-12）。
    bool dropped_lights_reported = false;
    const int force_time = forced_day_minute(); //!< `HD2D_FORCE_TIME`。負値なら上書きしない
    /*!
     * @brief `HD2D_ARENA_WATCH=1` — 観戦カメラ（設計書 §4.6）を**フロアの種別に依らず**当てる。
     * @details 検査の手段（`HD2D_FORCE_TIME` と同じ立場）。本物の賭け試合はヘッドレスの
     * 駆動では**数フレームで決着してしまい**、撮影が ON の窓に間に合わない（実測 2026-08-24。
     * 最初の地形フレームのメッシュ生成より試合のシミュレーションの方が速い）。
     * 実機では描画とメッセージが間を作るので問題にならない——**画で確かめるためだけ**の口である。
     */
    const bool force_arena_watch = (std::getenv("HD2D_ARENA_WATCH") != nullptr);
    if (force_arena_watch) {
        std::fprintf(stderr, "[hd2d] HD2D_ARENA_WATCH: 観戦カメラを常時当てます（検査用）\n");
    }
    const bool gpu_sync = gpu_sync_enabled(); //!< `HD2D_GPU_SYNC`。影の実費を測るとき
    if (force_time >= 0) {
        std::fprintf(stderr, "[hd2d] HD2D_FORCE_TIME: 時刻を %02d:%02d に固定します（コアには影響しません）\n",
            force_time / 60, force_time % 60);
    }
    if (gpu_sync) {
        /*
         * **垂直同期を切らないと測れない。**vsync が効いていると `SDL_GL_SwapWindow` は
         * すぐ返り、待ちは「次に GL を呼んだところ」で起きる。つまりフレーム頭の `glFinish`
         * が前のフレームの待ちを丸ごと吸い、影のパスが 15ms かかったように見える
         * （実際に一度そう読んだ）。同期して測るときは vsync も外すこと。
         */
        SDL_GL_SetSwapInterval(0);
        std::fprintf(stderr, "[hd2d] HD2D_GPU_SYNC: vsync を切り、各パスの後で glFinish します"
                             "（**普段より遅くなります。費目の比較にだけ使うこと**）\n");
    }
    const Uint64 perf_freq = clock::perf_freq();
    double ms_meaning = 0.0;
    double ms_build = 0.0;
    double ms_shadow = 0.0;
    double ms_submit = 0.0;
    double ms_post = 0.0;
    double ms_decode = 0.0;
    double ms_present = 0.0;
    double ms_frame = 0.0;
    /*
     * 診断: 環境変数 `HD2D_SLOW_FRAME_MS=<ミリ秒>` を付けると、主ループの 1 周がその時間を超えたときに
     * 費目ごとの時間を標準エラーへ 1 行出す。起動の主な区切りにも時刻の印を出す（`diag_mark`）。
     * 付けなければ何もしない（`DCSS` の「画面が一瞬応答しない」の調査のために足した）。
     */
    const double slow_frame_ms = []() {
        const char *v = std::getenv("HD2D_SLOW_FRAME_MS");
        return ((v != nullptr) && (*v != '\0')) ? std::atof(v) : 0.0;
    }();
    const auto diag_mark = [&](const char *label) {
        if (slow_frame_ms > 0.0) {
            std::fprintf(stderr, "[hd2d] diag t=%llu ms: %s\n", static_cast<unsigned long long>(clock::ticks64()), label);
        }
    };
    Uint64 diag_prev_loop_perf = 0;
    diag_mark("run: 窓と描画の用意が済んだ（コアを起こす前）");

    // --- コアを起こして握手する ---
    /*!
     * @brief コアが `lang-restart` を名乗ったか（下の features の節で決まる）。
     * @details 握手より前に置くのは、駆動ループがこの値を見るからである。
     */
    bool core_wants_lang_restart = false;
    /*!
     * @brief コアが `map_click` を申告したか（AH-58。下の features の節で決まる）。
     * @details 真のコアだけ、地図のクリック・タップを `input_event` の `map_click` で送る。
     * **コア名では決めない**（申告しないコアは、これまでのクリック移動のまま）。
     */
    bool core_map_click = false;
    /*!
     * @brief DCSS の画面の部品（握手でコアが `dcss-ui` を名乗ったときだけ作る。ほかのコアは空のまま 1 行も通らない）。
     * @details 決めたこと D10。原作のタイル版の区画（ステータス・メッセージ・ミニマップ・タブの欄）を描き、立体の地図は
     * その中のダンジョンの矩形に描く。。
     */
    std::unique_ptr<dcss::DcssUi> dcss_ui;
    //! DCSS（D24）: 実体のあるマスと、実体の後ろへ回す地形のオブジェクト（毎フレーム作り直す）。
    std::unordered_set<std::uint32_t> dcss_occupied;
    LibraryDeferred dcss_deferred;
    CoreLink link;
    CoreLinkOptions link_options;
    link_options.forwarded_args = options.forwarded_args;
    if (options.relaunch.resume) {
        /*
         * 起こし直しの回だけ `--resume` を足す（直前に保存した人物を開かせる）。
         * **ふつうの起動では渡さない**——どのコアも知らない引数で落ちうるし、
         * 知っているコアでも「前の人物が勝手に開く」のは意図と違う。
         */
        link_options.forwarded_args.emplace_back("--resume");
    }
    link_options.protocol_log_path = options.protocol_log_path;
    link_options.core_protocol_log_path = options.core_protocol_log_path;
    /*
     * 相対のパスは**自 exe のある場所から**解く（cwd からではない）。配布物では
     * コアが 1 階層深い `cores/` に居るので、`resolve_core_path` がそちらを先に見る。
     */
    if (!core_choice.empty()) {
#if defined(_WIN32)
        /*
         * データの場所を呼び手が既に決めていれば上書きしない。検証でデータを一時フォルダへ
         * 逃がすため（上書きしていたので、画面側から起動した検証が開発ツリー直下の
         * `rogue2/` などへ書いていた。2026-09-25）。ふだんの起動では未設定なので従来どおり。
         */
        if (GetEnvironmentVariableW(L"HENGBAND_DATA_ROOT", nullptr, 0) == 0) {
            SetEnvironmentVariableW(L"HENGBAND_DATA_ROOT", app_exe_directory().c_str());
        }
        link_options.core_path = resolve_core_path(core_choice);
#elif defined(HENGBAND_DESKTOP_LINUX)
        link_options.core_path = resolve_core_path(core_choice);
#else
        const std::filesystem::path picked(core_choice);
        link_options.core_path = picked.is_absolute() ? picked : (app_exe_directory() / picked);
#endif
    }
#if !defined(_WIN32) && !defined(HENGBAND_DESKTOP_LINUX)
    link_options.core_thread_main = options.core_thread_main;
    if (!core_choice.empty() && (options.core_thread_resolve != nullptr)) {
        // 選ばれたコア .so の入口へ差し替える（多コア形）。**ここが本当の「起こす」である。**
        if (const auto entry = options.core_thread_resolve(core_choice)) {
            link_options.core_thread_main = entry;
        } else {
            /*
             * 選んだ .so を開けない。**黙って別のコアを起こさない**（2026-08-23）。
             * 一覧の「在るか」は開かずに答えるようにしたので、ここは
             * 「一覧には出たが実際は読めなかった」＝組み方と表の食い違いを言う場所になった。
             * 前は直結のまま先へ進み、変愚が起きて「選んだのと違うゲームが始まる」に化けた。
             */
            show_message("選んだコアを読み込めませんでした: " + core_choice);
            text.shutdown();
            SDL_GL_DeleteContext(window.context);
            SDL_DestroyWindow(window.window);
            SDL_Quit();
            return 1;
        }
    }
    if ((link_options.core_thread_main == nullptr) && (options.core_thread_resolve != nullptr)) {
        // 一覧が空で選べなかったときの最後の受け皿は既定の変愚コア
        link_options.core_thread_main = options.core_thread_resolve("libhengcore.so");
    }
#endif
    diag_mark("link.start 前");
    if (!link.start(link_options, err)) {
        show_message(err);
        text.shutdown();
        SDL_GL_DeleteContext(window.context);
        SDL_DestroyWindow(window.window);
        SDL_Quit();
        return 1;
    }
    /*
     * DCSS だけは、画面が原作の JSON を受けて自分で区画を描けると `hello` で申告する（コアは名乗り返す）。
     * 握手より前にはコアの申告が分からないので、ここだけは起こすコアの名前（ファイル名）で決める。
     * ほかのコアの `hello` は従来と 1 バイトも変わらない。
     */
    {
        std::string lowered = core_choice;
        for (char &c : lowered) {
            c = ((c >= 'A') && (c <= 'Z')) ? static_cast<char>(c - 'A' + 'a') : c;
        }
        if (lowered.find("dcss") != std::string::npos) {
            link.set_hello_features({ "dcss-webtiles", "dcss-ui" });
        }
    }
    diag_mark("link.start 後・握手の前");
    if (!link.handshake(err)) {
        show_core_failure(link, err);
        link.shutdown();
        text.shutdown();
        SDL_GL_DeleteContext(window.context);
        SDL_DestroyWindow(window.window);
        SDL_Quit();
        return 1;
    }
    link.begin_receiving();
    const std::string title_line = build_title_line(link.hello_ack());
    diag_mark("握手が済んだ");

    /*
     * ---- 玉の配置と素材（決めたこと 2026-10-11）----
     * HP/MP の玉は**コアごとに違う意匠**。握手の `core_name` から `assets/ui/hud/<コア>/` を選び、
     * 配置（`orb_layout.json`）を覚えて 6 層を読む。変愚と知らないコアは `assets/ui/hud/` 直下（今の素材・今の寸法）。
     * 選んだ組が読めなければ変愚の素材へ落とす（無くても遊べる。それも読めなければ元の棒）。
     * 変愚と知らないコアは、起動の最初に読んだ素材のまま何もしない（変愚の起動は 1 行も変わらない）。
     * **配置を覚えるのはここで 1 度だけ**（この後に `UiLayout::compute` が玉の下端を引く）。
     */
    {
        std::string orb_note;
        OrbProfile orb = load_orb_profile(link.hello_ack().core_name, orb_note);
        if (!orb_note.empty()) {
            std::fprintf(stderr, "[hd2d] 玉の配置: %s（変愚の玉で出します）\n", orb_note.c_str());
        }
        if (!orb.asset_key.empty()) {
            const std::pair<UiImage *, const char *> layers[] = {
                { &orb_back_image, "orb_back" }, { &orb_glass_image, "orb_glass" },
                { &orb_liquid_hp_image, "orb_liquid_hp" }, { &orb_liquid_mp_image, "orb_liquid_mp" },
                { &orb_glass_top_image, "orb_glass_top" }, { &orb_front_image, "orb_front" },
            };
            bool all = true;
            for (const auto &layer : layers) {
                std::string load_err;
                if (!layer.first->load_minified(orb_layer_path(orb.asset_key, layer.second), load_err)) {
                    std::fprintf(stderr, "[hd2d] %s\n", load_err.c_str());
                    all = false;
                }
            }
            if (!all) {
                //! 途中まで読み替えた層を、変愚の素材へ戻す（層の意匠が混ざらないように）。
                std::fprintf(stderr, "[hd2d] 玉の素材 %s を読めないので、変愚の玉で出します\n", orb.asset_key.c_str());
                orb = OrbProfile{};
                for (const auto &layer : layers) {
                    std::string load_err;
                    if (!layer.first->load_minified(orb_layer_path("", layer.second), load_err)) {
                        std::fprintf(stderr, "[hd2d] %s（HP/MP の玉は元の棒で出します）\n", load_err.c_str());
                    }
                }
            }
            std::fprintf(stderr, "[hd2d] 玉: core=%s assets=%s 中心=(%.1f,%.1f) y=%.1f 半径=%.1f 下端=%d 飾り=%s\n",
                link.hello_ack().core_name.c_str(), orb_asset_dir(orb.asset_key).c_str(), orb.hp_cx, orb.mp_cx, orb.cy, orb.r,
                orb.art_bottom_y, orb.emblem.present ? "あり" : "なし");
        }
        set_orb_profile(orb);
    }

    /*
     * 板の 2 段探索（設計 §5.4 / §6.4）。コアが `asset_roots.slab` を申告していれば
     * ［申告 → 既定］の順で探す。**変愚コアは申告しないので空のまま**＝従来どおり。
     */
    //! 地面の凹凸。申告の無いコアは 0＝従来どおり平ら。
    set_ground_relief_voxels(link.hello_ack().ground_relief);
    if (!link.hello_ack().asset_root_slab.empty()) {
        slab_library.set_declared_dir(link.hello_ack().asset_root_slab);
        std::fprintf(stderr, "[hd2d] slab roots: \"%s\" -> \"%s\"\n",
            slab_library.declared_dir().c_str(), slab_library.dir().c_str());
    }

    /*
     * 題字にコアの名を添える（設計 §6.5）。**どのコアで遊んでいるかは窓の題で分かるべき**
     * ——別コアを 2 つ立ち上げたときに見分けがつかないと、セーブを取り違える。
     * コア名はコアが送ってきたものそのまま（画面側でコア名の分岐は書かない。§1 制約 1）。
     */
    if (!link.hello_ack().core_name.empty()) {
        const std::string window_title = std::string("変愚蛮怒HD - ") + link.hello_ack().core_name
            + (link.hello_ack().core_version.empty() ? "" : (" " + link.hello_ack().core_version));
        SDL_SetWindowTitle(window.window, window_title.c_str());
    }

    /*
     * **町の意匠をデータから読む**（2026-08-17。こう決めた——「コアに極力手を入れない方針なので
     * 『データに出す』とする」）。
     *
     * 意匠は `town_id` で引くが、**同じ番号が別のコアでは別の町**である（変愚の 2 番は
     * テルモラ・幻想蛮怒の 2 番は天狗の里）。だから「どのコア名のどの番号が何の町か」を
     * `assets/voxel/town_styles.jsonc` に出し、ここで**コア名に合う節だけ**を採る。
     *
     * **コアには 1 行も足していない**（`core_name` は握手で既に届く）。
     * **UI もコア名の分岐を持たない**（§1 制約 1）——名前で引くだけで、
     * どの名前が何を意味するかはデータの側にある。表が無ければ従来どおり。
     */
    {
        std::string style_log;
        const bool took = load_town_styles(options.voxel_dir, link.hello_ack().core_name, &style_log);
        if (!style_log.empty()) {
            std::fprintf(stderr, "[hd2d] %s\n", style_log.c_str());
        } else if (!took) {
            std::fprintf(stderr, "[hd2d] town_styles.jsonc は無い／このコア名の節が無い（従来の意匠で描きます）\n");
        }
    }

    /*
     * **ダンジョンの意匠も同じ流儀でデータから読む**。
     *
     * 町と同じ理由である——意匠は `dungeon_id` で引くが、**同じ番号が別のコアでは別の
     * ダンジョン**である（変愚の 5 番は竜の住みか・幻想蛮怒の 5 番は紅魔館深部）。
     * `assets/voxel/dungeon_styles.jsonc` に「どのコア名のどの番号が何か」を出し、
     * ここで**コア名に合う節だけ**を採る。表が無ければ従来どおり（変愚は 1 ボクセルも動かない）。
     *
     * **握手の直後に 1 度だけ。**読み直すと、返した `DungeonStyle` の中の
     * `const char *` が宙に浮く（文字列は表が持っている）。
     */
    {
        std::string style_log;
        const bool took
            = load_dungeon_styles(options.voxel_dir, link.hello_ack().core_name, &style_log);
        if (!style_log.empty()) {
            std::fprintf(stderr, "[hd2d] %s\n", style_log.c_str());
        } else if (!took) {
            std::fprintf(stderr,
                "[hd2d] dungeon_styles.jsonc は無い／このコア名の節が無い（従来の意匠で描きます）\n");
        }
    }

    /*
     * コアが持っていない機能の設定は出さない（設計 §6.3）。**見るのはコア名だけ**で、
     * コア名の分岐は書かない（§1 制約 1）。申告しないコアでは「ゲーム進行」「音」の節が
     * 入口から消える。
     */
    {
        const auto &features = link.hello_ack().features;
        const bool realtime = std::find(features.begin(), features.end(), "realtime") != features.end();
        set_core_supports_realtime(realtime);
        if (!realtime) {
            std::fprintf(stderr, "[hd2d] core does not declare \"realtime\"; hiding the game-progression page\n");
        }
        //! 音も同じ（2026-08-21 に決めた）。申告しないコアでは「音」の節が入口から消える。
        const bool audio = std::find(features.begin(), features.end(), "audio") != features.end();
        set_core_supports_audio(audio);
        if (!audio) {
            std::fprintf(stderr, "[hd2d] core does not declare \"audio\"; hiding the sound page\n");
        }
        /*
         * **言語を替えるには起こし直しが要るコア**。
         * Sil-Q は `init_angband()` に入る前に言語が決まっていなければならないので、
         * `ui_state.lang` を替えても遊びの途中では効かない。名乗ったコアだけ、
         * 言語を替えたときに**保存 → 終了 → 起こし直し**を回す（下の (d2)）。
         * **コア名の分岐は書かない**（§1 制約 1）。
         */
        core_wants_lang_restart
            = std::find(features.begin(), features.end(), "lang-restart") != features.end();
        if (core_wants_lang_restart) {
            std::fprintf(stderr, "[hd2d] core declares \"lang-restart\"; a language change will restart it\n");
        }
        //! 地図のクリック・タップ（AH-58。Angband が申告する）。申告しないコアでは何も変わらない。
        core_map_click = std::find(features.begin(), features.end(), "map_click") != features.end();
        if (core_map_click) {
            std::fprintf(stderr, "[hd2d] core declares \"map_click\"; map clicks and taps are sent to the core\n");
        }
        //! DCSS の画面の部品（段 U1）。申告したコアだけ。
        if (std::find(features.begin(), features.end(), "dcss-ui") != features.end()) {
            dcss_ui = std::make_unique<dcss::DcssUi>();
            std::fprintf(stderr, "[hd2d] core declares \"dcss-ui\"; the DCSS tile UI regions are drawn by the screen\n");
        }
    }

    int screen_w = options.window_w;
    int screen_h = options.window_h;
    SDL_GetWindowSize(window.window, &screen_w, &screen_h);

    /*
     * カメラの既定値。**仮である**（計画 §4-1 で「実物を見て人に決めてもらう」と決めてある）。
     * 距離だけは手で当てず、設計書 §5「1 マス 98〜130 px」から逆算する。
     */
    /*
     * 設定（P8）。**起動引数 > `hd2d.cfg` > 既定値**の順に強い。
     * 引数で打った値を cfg で上書きしてしまうと「今回だけこの値で見る」ができなくなり、
     * 絵を比べる作業が成立しない。
     */
    Hd2dSettings settings;
    /*
     * 一人称（FPS モード。おまけ機能。`ui/fps_mode.h`）。
     * @note 近くに `fps_mark` という変数があるが、あちらは**毎秒の枚数**の計測で無関係。
     *   紛らわしいので、こちらは「一人称」の意味で `first_person` と呼ぶ。
     */
    FpsMode first_person;
    /*!
     * @name 見下ろしの 90° 視点回転
     *
     * @details **意味を持つのは `camera_turn`（0..7。1 段 45°）だけ**で、`camera_turn_yaw` は
     * それへ滑らかに追いつく見た目の値である（`FpsMode` の `facing` / `yaw` と同じ分け方）。
     * 移動の回転（`rotate_screen_delta`）は必ず**確定値の方**を使う——回っている最中に
     * 押したキーが中途半端な角で解釈されると、押した先と歩いた先が食い違う。
     *
     * `camera_turn_goal` は**巻き戻さない**角である。0..2π へ丸めると 180° 回すときに
     * どちら回りか決まらず、右へ 2 回押したのに左へ回る絵が出る。
     * @note **cfg に保存しない**（設計書 §14-1）。回転は設定ではなく操作で、
     * 次に起動したときは北から始まるのが地図として安全である。
     * **起動時の角は `--turn=` > コアの申告（`hello_ack.default_view_turn`）> 0 の順に強い**
     * （申告の無いコアは 0＝従来どおり）。
     * @{
     */
    int camera_turn = options.camera_turn_given
        ? (((options.camera_turn % kViewTurnCount) + kViewTurnCount) % kViewTurnCount)
        : (((link.hello_ack().default_view_turn % kViewTurnCount) + kViewTurnCount) % kViewTurnCount);
    float camera_turn_goal = view_turn_yaw(camera_turn);
    float camera_turn_yaw = camera_turn_goal; //!< 起動直後は回さない（`--turn=` はその角から始まる）
    //! 右スティック横の 90° 回し。倒しっぱなしで回り続けないよう中立へ戻るまで next を受けない。
    bool turn_snap_armed = true;
    /*! @} */
    first_person.base_yaw = camera_turn_yaw; //!< 一人称を抜けたときの戻り先（§3.5）
    first_person.set_active(options.first_person); //!< `--fps` で最初から一人称
    if (options.first_person) {
        first_person.facing = options.first_person_yaw_deg * 0.0174533f;
        first_person.yaw = first_person.facing;
    }
    settings.camera_pitch_deg = options.camera_pitch_deg;
    settings.camera_fov_deg = options.camera_fov_deg;
    settings.camera_cell_px = options.camera_cell_px;
    settings.layout = options.layout;
    /*
     * `--layout=` は**縦横の両方**に効かせる（「今回はこれで見たい」という口なので、
     * 回した先で別の作りへ化けると比べられない）。打たれていなければ縦の既定は `Tall`
     * （`Hd2dSettings` の初期値。2026-08-12 に決めた）。
     */
    if (options.layout_from_args) {
        settings.layout_portrait = options.layout;
    }
    settings.post = post_flags;
    settings.cutaway_radius = options.cutaway_radius;
    settings.dof_strength = options.dof_strength;
    /*!
     * **そのコア専用の cfg が在ったか。**無ければ `hd2d.cfg` から引き継ぎ、
     * サブパネルの中身だけはコアごとの既定を入れ直す（番号の意味がコアで違うため）。
     */
    bool had_settings_file = false;
    //! **そのコア専用の cfg** が在ったか。サブパネルの既定を入れ直すかの判定に使う。
    bool had_core_settings_file = false;
    //! いま書き戻す先。**コアごとに別のファイル**（`settings_path_for_core` の説明）。
    const std::string core_cfg_path = settings_path_for_core(core_choice);
    {
        Hd2dSettings stored;
        /*
         * **そのコアのファイルを先に見る**（2026-08-26 に決めた
         * 「完全にコア毎の記述になるようにして」）。無ければ `hd2d.cfg` から引き継ぐ
         * ——そのコアで初めて遊ぶときに、前の設定が全部消えては困る。
         */
        had_core_settings_file = load_settings(core_cfg_path, stored);
        had_settings_file = had_core_settings_file;
        const bool from_shared = !had_core_settings_file;
        if (from_shared) {
            had_settings_file = load_settings(default_settings_path(), stored);
        }
        std::fprintf(stderr, "[hd2d] 設定: %s%s\n", core_cfg_path.c_str(),
            had_settings_file ? (from_shared ? "（無いので hd2d.cfg から引き継ぎ）" : " を読みました")
                              : "（まだ無いので既定で始めます）");
        if (had_settings_file) {
            if (!options.camera_from_args) {
                settings.camera_pitch_deg = stored.camera_pitch_deg;
                settings.camera_fov_deg = stored.camera_fov_deg;
                settings.camera_cell_px = stored.camera_cell_px;
            }
            if (!options.layout_from_args) {
                settings.layout = stored.layout;
                settings.layout_portrait = stored.layout_portrait;
            }
            if (!options.post_from_args) {
                settings.post = stored.post;
            }
            if (!options.cutaway_from_args) {
                settings.cutaway_radius = stored.cutaway_radius;
            }
            if (!options.dof_from_args) {
                settings.dof_strength = stored.dof_strength;
            }
            //! **言語は写さない。**`hd2d.cfg` が持つ（上のコア選択の前で当ててある）。
            //! コアごとのファイルにも書かれるが、読むのは共通のほうだけである。
            /*
             * コアの一覧と前回の選択（設計 §6.1）。ここで写しておくのは
             * **`saved_settings` を cfg の中身と一致させる**ため——写さないと、
             * 選択が変わっていなくても毎回 cfg を書き直すことになる。
             * 実際に選んだ結果はこの下（`saved_settings` の後）で入れる。
             */
            settings.cores = stored.cores;
            settings.last_core = stored.last_core;
            settings.move_smoothing = stored.move_smoothing; //!< 引数は無い（メニューだけ）
            //! 実体の表現。cfg → 引数の順に強い（下で上書き）。
            //! メインパネルの中身（§6）。引数は `--original` だけなので、下で上書きする。
            settings.main_panel = stored.main_panel;
            settings.modern_ui=stored.modern_ui;settings.modern_theme=stored.modern_theme;
            settings.modern_font_level=stored.modern_font_level;
            settings.ascii_panel_px = stored.ascii_panel_px;
            settings.entity_style = stored.entity_style;
            settings.entity_slab_screen_aligned = stored.entity_slab_screen_aligned;
            settings.core_defaults_rev = stored.core_defaults_rev;
            settings.dcss_pad_binds = stored.dcss_pad_binds; //!< DCSS のゲームパッドの割り当て（`dcss/dcss_pad.h`）
            settings.entity_glyph_pct = stored.entity_glyph_pct;
            //! 状態列の位置と字の受け皿。
            //! **忘れると「覚えているのに効かない」**（tools/check_settings_carry.py が捕まえる）。
            settings.status_col_side = stored.status_col_side;
            settings.entity_glyph_fallback = stored.entity_glyph_fallback;
            /*
             * 画調。**写し忘れると
             * 「cfg に書いたのに効かない」になる**（上のミニマップと同じ穴）。
             * 引数（`--look=`）はこの塊の後で上書きする。
             */
            settings.scene_look = stored.scene_look;
            settings.tron_sky = stored.tron_sky;
            /*
             * **写し忘れていた 10 件**（2026-08-23 の疑問「他の設定も保存されるか？」）。
             * どれも cfg へは書かれ、cfg からも読まれていたのに、ここへ並べていなかった
             * ——`load_settings` が読む先は `stored` であって `settings` ではないので、
             * 並べ忘れると**覚えているのに効かない**。しかも
             * `saved_settings = settings` はこの塊の直後に取るので、以後どれか 1 つでも
             * 触ると cfg 側の値まで既定で塗り潰される（＝黙って消える）。
             *
             * 見つけ方は `tools/check_settings_carry.py`（`differs_from` が見ている欄と
             * ここの写しを突き合わせる）。**欄を足したら 3 か所とも足すこと。**
             */
            settings.tron_face_glyph = stored.tron_face_glyph; //!< TRON の面の字
            settings.wear_pct = stored.wear_pct; //!< 面の汚し（`render/surface_wear.h`）
            settings.wear_materials = stored.wear_materials; //!< 材質ごとの入切
            //! 画面全体の色処理（2026-08-23 に決めた）。
            settings.vignette_strength = stored.vignette_strength;
            settings.sepia = stored.sepia;
            settings.hdr = stored.hdr;
            settings.exposure = stored.exposure;
            settings.dust = stored.dust; //!< 空中に浮かぶ埃
            settings.leaf_pct = stored.leaf_pct; //!< 木の葉（`render/leaf_detail.h`）
            //! 光の届く範囲。
            settings.light_range_pct = stored.light_range_pct;
            settings.light_range_fade_ms = stored.light_range_fade_ms;
            settings.torch_flicker = stored.torch_flicker;
            settings.dungeon_light = stored.dungeon_light; //!< 空間の明るさ
            settings.fps_slab_turn = stored.fps_slab_turn; //!< 一人称での板の向き
            settings.fps_vertical_look = stored.fps_vertical_look;
            settings.fps_wall_upper = stored.fps_wall_upper;
            settings.fps_ceiling = stored.fps_ceiling;
            //! 音。**4 件とも効いていなかった。**
            settings.bgm_mode = stored.bgm_mode;
            settings.sound_enabled = stored.sound_enabled;
            settings.music_volume = stored.music_volume;
            settings.sound_volume = stored.sound_volume;
            /*
             * ミニマップ（2026-08-19 に決めた）。**引数は無く、メニューと cfg だけ。**
             * ここへ並べ忘れると「cfg に書いたのに効かない」になる
             */
            settings.minimap_corner = stored.minimap_corner;
            settings.minimap_size_pct = stored.minimap_size_pct;
            settings.minimap_cell_px = stored.minimap_cell_px;
            settings.minimap_opacity_pct = stored.minimap_opacity_pct;
            settings.minimap_relative = stored.minimap_relative;
            settings.subs_open = stored.subs_open;
            settings.key_binds = stored.key_binds;
            /*
             * **ここを写し忘れると「覚えているのに効かない」になる。**`load_settings` が
             * 読んだ先は `stored` であって `settings` ではないので、項目ごとに写す必要がある。
             * 割り当て（`pad` / `keys.command`）・下段の幅・サブパネルの中身・背面の絵は
             * 引数の口が無く、メニューで触るしか変えようが無いものなので、そのまま写す。
             */
            settings.pad = stored.pad;
            /*
             * **この写しが無いと、遊ぶ人が消したパッドの割り当てが毎回の起動で復活する**
             * （`pad_defaults_version`（旧 `pad_defaults_applied`）は cfg に書かれるのに、読んだ結果を捨てていた。
             * バーチャルパッドの写し忘れを直すときに見つけた同型の穴。2026-08-12）。
             */
            settings.pad_defaults_version = stored.pad_defaults_version;
            /*
             * **共有の `hd2d.cfg` から引き継いだときはパッドの割り当てを持ち込まない**（2026-10-03）。
             * 割り当てはコマンドの `id`（コアの表の並び順）で書かれているので、別のコアの番号は
             * このコアでは別の命令を指す。Umoria を初めて開いたら X が「人物表」・LB が「武器の持ち替え」に
             * なっていた。`pad_defaults_version` も引き継がないので、表が届いたときに字で引く既定が入る。
             */
            if (from_shared) {
                settings.pad = Hd2dSettings{}.pad;
                settings.pad_defaults_version = 0;
            }
            settings.vpad = stored.vpad; //!< バーチャルパッド（引数の口が無く、メニューと cfg だけ）
            settings.sub_split = stored.sub_split; //!< サブパネルの枚数と仕切り（引数は無い）
            for (int i = 0; i < kUiSubPanels; ++i) {
                settings.sub_panel_kind[i] = stored.sub_panel_kind[i];
            }
            settings.backdrops = stored.backdrops;
            settings.fps_fov_deg = stored.fps_fov_deg;
            settings.fps_step_ms = stored.fps_step_ms; //!< 一人称の歩調（引数は無い）
            /*
             * ゲーム進行。引数は無く、メニューと cfg だけ。
             * **上の注記どおりここを写し忘れると「覚えているのに効かない」になる**
             * （実際に踏んだ。cfg に `realtime=1` を書いても `ui_state` は `false` のまま届いた）。
             */
            settings.realtime_enabled = stored.realtime_enabled;
            settings.realtime_speed_index = stored.realtime_speed_index;
            settings.realtime_prompt_live = stored.realtime_prompt_live;
            settings.realtime_self_span_index = stored.realtime_self_span_index;
            settings.damage_flash = stored.damage_flash;
            settings.damage_shake = stored.damage_shake;
            settings.fx_screen_shake = stored.fx_screen_shake;
            //! HP/MP の玉の見せ方（引数は無く、cfg だけ。写し忘れると「cfg に書いたのに効かない」）。
            settings.orb_wave = stored.orb_wave;
            settings.orb_wave_size_pct = stored.orb_wave_size_pct;
            settings.orb_wave_speed_pct = stored.orb_wave_speed_pct;
            settings.orb_rise_speed_pct = stored.orb_rise_speed_pct;
            settings.orb_fall_speed_pct = stored.orb_fall_speed_pct;
            /*
             * VR の盤・卓・板。引数は無く、
             * **cfg に書くのが唯一の口**である（実機で撮り比べるために置いてある）。
             *
             * **写し忘れていた。**上の注記どおりの穴で、`vr_tile_m` を cfg に書いても
             * 既定の値のまま動いていた（2026-08-14 に `--vr-fake` で気づいた。§20・罠 28）。
             */
            settings.vr_tile_m = stored.vr_tile_m;
            settings.vr_table_forward_m = stored.vr_table_forward_m;
            settings.vr_table_height_m = stored.vr_table_height_m;
            settings.vr_panel_deg_per_cell = stored.vr_panel_deg_per_cell;
            settings.vr_panel_dist_m = stored.vr_panel_dist_m;
            settings.vr_panel_lift_deg = stored.vr_panel_lift_deg;
            settings.vr_panel_follow_deg = stored.vr_panel_follow_deg;
            settings.vr_fps_cell_m = stored.vr_fps_cell_m;
            settings.vr_render_scale = stored.vr_render_scale;
            for (int i = 0; i < static_cast<int>(xr::PanelSlot::Count); ++i) {
                settings.vr_fps_show[i] = stored.vr_fps_show[i];
            }
            //! 画面モードは cfg のものをそのまま使う（引数に口が無い）。
            settings.windowed = stored.windowed;
        }
    }
    /*
     * **サブパネルの中身の既定は、コア名で選ぶ**（2026-08-23 に決めた
     * 「サブパネルは画像の状態をデフォルトにして」）。
     *
     * サブウインドウの番号は**コアごとに意味が違う**（Sil-Q の 7 は前のメッセージ、
     * 変愚の 7 は周辺の光景）ので、`Hd2dSettings` の初期値 1 つでは足りない。
     * Sil-Q で変愚の既定を使うと、その番号に描画関数が無く**空の枠が並ぶ**。
     *
     * **cfg が在るときは触らない。**在るなら遊ぶ人が決めた中身が持ち主である
     * （cfg は毎回まるごと書くので、在れば必ず `sub_panel_kind=` を持っている）。
     */
    {
        /*
         * **コアごとに覚えた割り当てを入れる**（2026-08-26。気づいたこと
         * 「新規で始めるとパネル設定が画像の通り、cfg はセーブデータ横断で共通？
         * にしても前に設定してある他のセーブデータとも、指定したデフォルト設定とも違う」）。
         *
         * 前は「cfg が在れば触らない」だった。ところが cfg の `sub_panel_kind=` は
         * **1 組しか無く、番号の意味はコアごとに違う**。コアは自分が持っていない番号を
         * 「無し」として返し、画面はその実効値を吸い上げて cfg へ書くので、
         * **別のコアを起動しただけで前のコアの割り当てが -1 に潰れていた**
         * （手元の cfg は `7,9,4,-1,-1,1,0`。Sil-Q で決めた 敵の地形の記憶（5）と
         * 冒険者（2）が消えていた）。
         *
         * いまは**設定そのものをコアごとのファイルへ分けた**（`settings_path_for_core`。
         * 2026-08-26 に決めた「完全にコア毎の記述になるようにして」）。
         * ここで既定を入れ直すのは、**そのコアのファイルがまだ無いとき**だけである
         * ——そのときの中身は `hd2d.cfg` からの引き継ぎで、番号は前のコアのものだから。
         *
         * **cfg はセーブごとではない**（設定であってセーブではない）。コアごとである。
         */
        if (!had_core_settings_file) {
            default_sub_panel_kinds(link.hello_ack().core_name, settings.sub_panel_kind);
            //! **枚数も一緒に**（2026-09-01）。中身だけ入れると、出ない枠に割り当てが残る。
            default_sub_split(link.hello_ack().core_name, settings.sub_split);
        }
        /*
         * RogueClone2 はコア側のサブウインドウ種類を持たず、UI が作る
         * メッセージ／装備／視界の敵／キャラクター／持ち物だけを表示する。
         * r8 以前に変愚用の種類番号が cfg へ保存されていても「種類 4」等を出さない。
         */
        if (link.hello_ack().core_name == "RogueClone2"
            || link.hello_ack().core_name == "Rogue 3.6"
            || link.hello_ack().core_name == "Rogue 5.4"
            || link.hello_ack().core_name == "angband") {
            /*
             * 2026-09-23 から Rogue 系も枠ごとに種類を選べる（気づいたこと「Rogue の
             * サブパネルの割り当てが変更できない」。`bridge/rogue_sub_panels.h`）。
             * 以前のように起動のたびに全部を既定へ戻すと、選んだ割り当てが消える。
             * 戻すのは Rogue 系が申告していない番号（r8 以前に保存された変愚用の番号など）だけ。
             */
            for (int i = 0; i < kUiSubPanels; ++i) {
                const int kind = settings.sub_panel_kind[i];
                if ((kind < kBuiltinSubPanelKindBase) || (kind >= kBuiltinSubPanelKindBase + kBuiltinSubPanelKindCount)) {
                    settings.sub_panel_kind[i] = -1;
                }
            }
        }
        std::fprintf(stderr, "[hd2d] サブパネルの割り当て: コア \"%s\" の%s（%d,%d,%d,%d / %d,%d,%d）\n",
            link.hello_ack().core_name.c_str(), had_core_settings_file ? "覚えた中身" : "既定",
            settings.sub_panel_kind[0], settings.sub_panel_kind[1], settings.sub_panel_kind[2],
            settings.sub_panel_kind[3], settings.sub_panel_kind[4], settings.sub_panel_kind[5],
            settings.sub_panel_kind[6]);
        (void)had_settings_file;
        /*
         * **DCSS だけ既定が違う設定**（D24。申告 `dcss-ui` のコア）: 板を画面に正対させる（横から見たような潰れが出ない）。
         * 改訂（`core_defaults_rev`）が上がっているときだけ 1 度当てる。そのコアの cfg が既にあっても当て直すが、
         * 当てた後は遊ぶ人の選んだ値が残る（メニューで切り替えられる）。ほかのコアはここを通らない。
         */
        constexpr int kDcssDefaultsRev = 2;
        if (dcss_ui && (settings.core_defaults_rev < kDcssDefaultsRev)) {
            if (settings.core_defaults_rev < 1) {
                settings.entity_slab_screen_aligned = true;
            }
            /*
             * 改訂 2（段 5。D12: 操作は原作に合わせる）: キーボードの F1 は原作の「ゲームメニュー」（`CMD_GAME_MENU`）なので、
             * HD2D のコマンドメニューの既定の F1 を外す（機能メニューは F10 のまま）。外したキーは原作へ渡る。
             */
            if (settings.core_defaults_rev < 2) {
                if (settings.key_binds.find(kActionCommandMenu).keycode == SDLK_F1) {
                    settings.key_binds.clear(kActionCommandMenu);
                }
            }
            settings.core_defaults_rev = kDcssDefaultsRev;
        }
        if (dcss_ui) {
            std::fprintf(stderr, "[hd2d] DCSS の板の向き: 画面に正対=%d（既定の改訂 %d）\n", settings.entity_slab_screen_aligned ? 1 : 0, settings.core_defaults_rev);
            dcss_ui->pad().load_line(settings.dcss_pad_binds); //!< ゲームパッドの割り当て（段 5）
            pad.set_original_timing(true); //!< 十字の方向の出し方（40ms の斜めの合成・200ms のリピート）を原作に合わせる（段 5）
        }
    }
    /*
     * **`--entity=` / `--original` は cfg より強い**（起動引数 > cfg > 既定）。
     *
     * **cfg を読んだ後に置くこと。**下の `--look=` と同じ理由で、上の塊は `stored` の
     * 中身を項目ごとに写し戻すので、前に置くと引数が cfg で塗り潰される。
     * **実際に塗り潰されていた**——`hd2d.cfg` に `entity_style=slab` が在ると
     * `--entity=ascii` が黙って無視され、実体が 1 つも描かれないまま
     * 「タイル欠け」が数えられた（2026-08-20。Sil-Q の M0 検証で発覚）。
     */
    //! `--original`。**F4 はスクリプトから押せない**ので口が要る。
    if (options.original_panel) {
        settings.main_panel = Hd2dSettings::MainPanel::Ascii;
    }
    if (!options.entity_style.empty()) {
        if (options.entity_style == "ascii") {
            settings.entity_style = Hd2dSettings::EntityStyle::Ascii;
        } else if ((options.entity_style == "tile") || (options.entity_style == "slab")) {
            settings.entity_style = Hd2dSettings::EntityStyle::Slab;
        } else {
            std::fprintf(stderr, "[hd2d] --entity=%s は読めません（tile|ascii）。設定のままにします\n",
                options.entity_style.c_str());
        }
    }
    /*
     * **`--look=` / `--tron-sky` は cfg より強い**（起動引数 > cfg > 既定）。
     *
     * **cfg を読んだ後に置くこと。**上の塊は `stored` の中身を項目ごとに写し戻すので、
     * 前に置くと引数が cfg で塗り潰される。綴りを読めなかったときは黙って落とさず言う
     * ——「打ったのに効かない」がいちばん高くつく。
     */
    if (!options.bgm_mode.empty()) {
        //! 綴りは cfg と 1 対 1（`load_settings` の `bgm=`）。読めなければ言う。
        if (options.bgm_mode == "music") {
            settings.bgm_mode = Hd2dSettings::BgmMode::Music;
        } else if (options.bgm_mode == "ambience") {
            settings.bgm_mode = Hd2dSettings::BgmMode::Ambience;
        } else if (options.bgm_mode == "off") {
            settings.bgm_mode = Hd2dSettings::BgmMode::Off;
        } else {
            std::fprintf(stderr, "[hd2d] --bgm=%s は読めません（music|ambience|off）。設定のままにします\n",
                options.bgm_mode.c_str());
        }
    }
    if (!options.scene_look.empty()) {
        //! 名前は `parsed`。**`look`（毎フレームの `LookParams`）を隠さないため。**
        SceneLookKind parsed = settings.scene_look;
        if (parse_scene_look(options.scene_look, parsed)) {
            settings.scene_look = parsed;
        } else {
            std::fprintf(stderr, "[hd2d] --look=%s は読めません（standard|tron）。設定のままにします\n",
                options.scene_look.c_str());
        }
    }
    //! 画面全体の色処理（`--vignette=` ほか）。**cfg より強い**（打たれた欄だけ）。
    if (options.vignette_strength >= 0.f) {
        settings.vignette_strength = options.vignette_strength;
    }
    if (options.sepia >= 0.f) {
        settings.sepia = options.sepia;
    }
    if (options.hdr >= 0.f) {
        settings.hdr = options.hdr;
    }
    if (options.exposure >= 0.f) {
        settings.exposure = options.exposure;
    }
    if (options.dust >= 0.f) {
        settings.dust = options.dust;
    }
    if (!options.wear_materials.empty()) {
        //! 材質ごとの入切（`--wear-materials=`）。**cfg より強い**。
        std::uint32_t bits = settings.wear_materials;
        if (parse_wear_materials(options.wear_materials, bits)) {
            settings.wear_materials = bits;
        } else {
            std::fprintf(stderr, "[hd2d] --wear-materials= が読めません: %s（all / none / stone,wood,…）\n",
                options.wear_materials.c_str());
        }
    }
    if (!options.leaf.empty()) {
        //! 木の葉（`--leaf=`）。**cfg より強い**（`--wear=` と同じ流儀）。
        float leaf_amount = static_cast<float>(settings.leaf_pct) / 100.f;
        if (parse_leaf_amount(options.leaf, leaf_amount)) {
            settings.leaf_pct = static_cast<int>((leaf_amount * 100.f) + 0.5f);
        } else {
            std::fprintf(stderr, "[hd2d] --leaf= の値が読めません: %s（off / on / 0.0〜2.0）\n",
                options.leaf.c_str());
        }
    }
    if (!options.wear.empty()) {
        //! 面の汚し（`--wear=`）。**cfg より強い**（画調の引数と同じ流儀）。
        float amount = static_cast<float>(settings.wear_pct) / 100.f;
        if (parse_wear_amount(options.wear, amount)) {
            settings.wear_pct = static_cast<int>((amount * 100.f) + 0.5f);
        } else {
            std::fprintf(stderr, "[hd2d] --wear= の値が読めません: %s（off / on / 0.0〜2.0）\n",
                options.wear.c_str());
        }
    }
    if (options.tron_sky >= 0) {
        settings.tron_sky = (options.tron_sky != 0);
    }
#if defined(__ANDROID__)
    /*
     * **Android に窓モードは無い**（`create_window` の注記）。cfg に古い `windowed=1` が
     * 残っていても押し切る——素の窓に戻すとステータスバーが地図の上へ戻ってくる。
     * 機能メニューの「画面モード」も Android では並べない（`feature_menu.cpp`）。
     */
    settings.windowed = false;
#endif
    /*
     * cfg で選ばれている言語を当てる（カタログ自体は `run()` の頭で読んである）。
     * 読めない言語が書かれていても**起動は止めない**。既定へ寄せて先へ進む。
     */
    /*
     * **ここでは当てない。写し取るだけ。** 言語は上（コア選択の前）で
     * `hd2d.cfg` から当ててあり、**コア選択の画で遊ぶ人が替えているかもしれない**。
     * ここでもう一度 cfg の値を当てると、その場で替えたぶんが**元へ戻る**。
     */
    settings.lang = std::string(i18n::current());
    first_person.fov_deg = settings.fps_fov_deg; //!< 一人称の画角は cfg から（ホイールで変わる）
    Hd2dSettings saved_settings = settings; //!< 書き戻すかの判定に使う「最後に一致していた中身」
    /*
     * コア選択の結果を覚える（設計 §6.1「前回の選択を記憶して初期カーソルに」）。
     * **`saved_settings` を取った後**に入れるので、初回の自動登録も選択の変更も
     * 主ループの設定の書き戻しが拾う。変わっていなければ 1 バイトも書かない。
     * `--core-path=` で飛ばしたときは覚えない——**「今回だけ」の指定を記憶に混ぜない**。
     */
    settings.cores = core_list;
    if (options.core_path.empty() && !core_choice.empty()) {
        settings.last_core = core_choice;
    }
    FeatureMenu feature_menu;
    /*
     * タブで持ち物一覧・原作の設定（Rogue）へ移るとき、**次の窓の中身が届くまで前のタブの
     * 画を描き続ける**ための写し（気づいたこと 2026-09-26「タブを切り替える際に一度別の画面を
     * 一瞬挟んでないか」）。閉じた機能メニューそのものは入力を受けない。写しは描くだけ。
     * 写すのは**このフレームの入力を受ける前**の機能メニュー（`menu_before_input`）——
     * 入力の後だと、タブの見出しだけが持ち物一覧に移った中身の無い窓になる。
     */
    std::unique_ptr<FeatureMenu> menu_handoff;
    Uint32 menu_handoff_since = 0;
    FeatureMenu menu_before_input;
    //! Rogue の設定一覧（新 UI の窓）から次のタブへ移る間に描き続ける、閉じる前の窓の写し。
    std::unique_ptr<ModernStoreUi> modern_handoff;
    Uint32 modern_handoff_since = 0;
    int menu_before_input_frame = -1;
    /*
     * 音。**P1 は環境音のベッドだけ。**
     * 装置が開けなくても続ける——音は遊びを止めてよい理由にならない。
     */
    audio::AudioEngine audio_engine;
    audio::AmbienceTable ambience_table;
    //! 切り替えの段取り役（落ち着き待ち・入れ替え中の抑止。§8.4）。
    audio::AmbienceDirector ambience_director;
    audio::SfxCatalog sfx_catalog;
    std::uint32_t sfx_pick = 0; //!< 候補が複数ある音を順に選ぶための数（同じ音が続かないように）
    {
        std::string audio_log;
        if (audio_engine.open(&audio_log)) {
            std::fprintf(stderr, "[hd2d][audio] %s\n", audio_log.c_str());
        } else {
            std::fprintf(stderr, "[hd2d][audio] %s\n", audio_log.c_str());
        }
        std::string table_log;
        (void)ambience_table.load("assets/audio", &table_log);
        std::fprintf(stderr, "[hd2d][audio] %s\n", table_log.c_str());
        std::string sfx_log;
        (void)sfx_catalog.load("assets/audio", &sfx_log);
        std::fprintf(stderr, "[hd2d][audio] %s\n", sfx_log.c_str());
    }
    ClickPath click_path; //!< クリックした所まで歩く（P8）
    //! 戦闘の見せ場。色も間合いもこちらが決める。
    CombatFxView combat_fx;
    //! 攻撃の視覚効果（矢・弾・投げた物・魔法の玉・爆発）。
    AttackFxView attack_fx;
    //! 今のフレームの攻撃の視覚効果（毎周 `attack_fx` から作り直す。`draw_scene` が描く）。
    FxFrame attack_fx_frame;
    /*!
     * 飛んでいく物（投げた物）の板を `item_id` から引く。**床のアイテムを描くのと同じ道**
     * （`tile_catalog` の `K` の行 → `slab_library`）。引けなければ偽（小さな箱で描く）。
     */
    const FxSlabResolver attack_fx_slab_resolver = [&](std::uint16_t item_id, FxSlabSource &source) {
        const int slab = slab_library.acquire_item(tile_catalog, item_id, renderer);
        if (slab < 0) {
            return false;
        }
        source.gpu = &slab_library.gpu(slab);
        const auto bounds = slab_library.bounds(slab);
        source.minimum = bounds.first;
        source.maximum = bounds.second;
        return true;
    };
    //! サブパネルに映せるものの一覧（コアが握手のあと 1 回だけ送る。v1 §8.1）。
    std::vector<SubPanelKindChoice> sub_panel_kind_choices;
    bool window_is_windowed = true; //!< いま窓が実際にどちらになっているか（要求との差でだけ動かす）
    /*!
     * いまマウスを掴んでいるか（`SDL_SetRelativeMouseMode`）。
     * **一人称で視界を回す間だけ掴む。**掴まないと、窓の端で `xrel` が来なくなって
     * 視界がそこで止まる（＝1 周できない）。掴んでいる間はカーソルが消え、
     * 座標も来なくなるので、**掴んだままメニューへ入らせないこと**。
     */
    bool mouse_is_relative = false;

    /*
     * 画面の作り（P8）。**3D の矩形はここが決める。**`Full` / `Hybrid` では窓いっぱい、
     * `Split` では MainMap の矩形。カメラの画素座標はこの矩形の左上を原点にする
     * （窓の座標へ戻すには `scene.x/y` を足す。マウスは引く）。
     */
    HudState hud_state;
    hud_state.subs_open = settings.subs_open;
    /*!
     * 切り欠き（ノッチ・パンチ穴）を避けた余白（左・上・右・下の画素）。
     * **Android だけが 0 以外になりうる**（`platform/android/android_safe_area.h`）。
     * 2026-08-12 に気づいた「上部がスマホ側のノッチ部分に被って見えない部分が出る」。
     */
    int safe_inset[4] = { 0, 0, 0, 0 };
    /*!
     * @brief ソフトキーボードが下から覆っている画素（Android だけ。AH-02）。
     * @details **切り欠きと同じ「下の余白」として足す。**そうすると割り付けが丸ごと
     * キーボードの上へ退くので、プロンプト行（＝打っている文字が出る所）も
     * バーチャルパッドも隠れない。IME 専用の場所取りは作らない。
     */
    int ime_inset_bottom = 0;
    InputState input_state;
#if defined(__ANDROID__)
    //! コアが自由文字入力の中に居るか（前フレームの姿）。変わり目でだけ IME を出し入れする。
    bool &ime_prompt_open = input_state.ime_prompt_open;
    /*!
     * @brief 戻るキーで閉じられたか。**閉じられている間は出し直さない。**
     * @details ここを持たずに「入力中なら毎フレーム `SDL_StartTextInput()`」と書くと、
     * 閉じた次のフレームで出し直してしまい**戻るキーが効かなくなる**。
     * 閉じたことは SDL 側の状態（`SDL_IsTextInputActive`）で気づく——戻るキーは
     * SDL の `DummyEdit.onKeyPreIme` が拾って `SDL_StopTextInput()` を呼ぶので、
     * こちらへは何の出来事も届かない。
     */
    bool &ime_dismissed = input_state.ime_dismissed;
#endif
    //! 直近で SDL へ渡した入力欄の矩形（変わったときだけ渡し直す）。**Windows でも使う。**
    SDL_Rect ime_text_rect{ 0, 0, 0, 0 };
    /*!
     * @brief IME の未確定文字列（`SDL_TEXTEDITING`）。**自前で描く。**
     *
     * @details `SDL_HINT_IME_SHOW_UI` を立てると候補一覧は素の IME が描くようになるが、
     * **未確定の文字列そのものは SDL が横取りしたまま**で、`SDL_TEXTEDITING` として
     * こちらへ回ってくる（2026-08-13 に気づいた:「変換候補ウインドウは出るように
     * なったが入力中は表示されていない」）。描かなければどこにも出ない。
     *
     * 確定すると `SDL_TEXTINPUT` が来るので、そこで空にする。
     */
    auto &ime_edit_text = input_state.ime_edit_text;
    /*!
     * @brief いま使える範囲（窓から余白を引いたもの）。
     * @details **窓の座標で持つ**（マウスと指の座標がそのまま噛み合う）。
     * 割り付けもバーチャルパッドもこれを見る。切り欠きが無ければ窓そのもの。
     */
    const auto usable_area = [&safe_inset, &ime_inset_bottom, &screen_w, &screen_h]() {
        const int bottom = safe_inset[3] + ime_inset_bottom;
        return RectPx{ safe_inset[0], safe_inset[1],
            std::max(1, screen_w - safe_inset[0] - safe_inset[2]),
            std::max(1, screen_h - safe_inset[1] - bottom) };
    };
    /*!
     * @brief いま縦持ちか。**使える範囲の形だけで決める**（`vpad_orient_of()` と同じ式）。
     * @details 画面の作りもバーチャルパッドの配置も縦横で別に持つ（2026-08-12 に決めた
     * 「縦と横が切り替わる際には自動でそれぞれに設定された画面構成に切り替わるように」）。
     * 「いま縦」を別の変数で覚えて回すと、回転の途中で描く側と入力側が食い違う。
     * **毎回ここから出す**ので、窓の大きさが変わった次のフレームには全員が揃って切り替わる。
     */
    const auto screen_is_portrait = [&usable_area]() { return vpad_orient_of(usable_area()) == kVpadPortrait; };
    UiLayout layout = UiLayout::compute(settings.layout_for(screen_is_portrait()), usable_area().w, usable_area().h,
        int(text.cell_w()*(settings.modern_ui?menu_font_scale(settings.modern_font_level):1.f)+.5f),
        int(text.cell_h()*(settings.modern_ui?menu_font_scale(settings.modern_font_level):1.f)+.5f), hud_state.subs_open,
        settings.sub_split, usable_area().x, usable_area().y,
        settings.vpad.show, false,
        settings.minimap_corner, settings.minimap_size_pct);
    /*!
     * @brief DCSS の配置を重ねる（`dcss-ui` を申告したコアだけ。ほかのコアでは何もしない）。
     * @details 下の案内の帯は従来どおり出す。その高さを窓の下端から先に取り、残りを
     * `DcssLayout::compute` の窓の大きさにする。立体の地図の矩形は原作のダンジョンの見える範囲へ差し替え、左の状態の欄・
     * サブパネル・玉・ミニマップ・メッセージの帯は空にする（描く側は「置き場所が無い」と見て何も描かない）。
     * ゲームに入る前（タイトル・キャラクター作成）は従来の配置のまま。
     */
    std::string dcss_last_description;
    std::uint64_t dcss_last_serial = 0;
    const auto apply_dcss_layout = [&](UiLayout &l) {
        if (!dcss_ui) {
            return;
        }
        const RectPx area = usable_area();
        const int reserved = l.bottom_bar.empty() ? 0 : std::max(0, (area.y + area.h) - l.bottom_bar.y);
        std::string dcss_err;
        dcss_ui->set_language(std::string(i18n::current()));
#if defined(__ANDROID__)
        // Android・Quest は原作の Android 版と同じ字の大きさの表と窓の拡大で配置する（常に小さい配置になる。「Android・Quest の配置」）。
        dcss_ui->use_android_profile(screen_w, screen_h);
#endif
        if (!dcss_ui->prepare(area.x, area.y, area.w, area.h, reserved, dcss_err)) {
            if (dcss_last_description != dcss_err) {
                dcss_last_description = dcss_err;
                std::fprintf(stderr, "[hd2d] %s\n", dcss_err.c_str());
            }
            return;
        }
        if (dcss_ui->layout_serial() != dcss_last_serial) {
            dcss_last_serial = dcss_ui->layout_serial();
            dcss_last_description = dcss_ui->describe();
            std::fprintf(stderr, "[hd2d] %s\n", dcss_last_description.c_str());
        }
        if (!dcss_ui->in_game()) {
            return;
        }
        l.scene = dcss_ui->scene_rect();
        l.status_col = RectPx{};
        l.hud = RectPx{};
        l.orb = RectPx{};
        l.message_bar = RectPx{};
        l.prompt_bar = RectPx{};
        l.minimap = RectPx{};
        l.minimap_area = RectPx{};
        l.preview_menu = RectPx{};
        l.main_panel_design_off = true;
        for (RectPx &r : l.sub) {
            r = RectPx{};
        }
        for (RectPx &r : l.grip) {
            r = RectPx{};
        }
    };
    apply_dcss_layout(layout);

    Camera camera;
    camera.pitch = settings.camera_pitch_deg * 0.0174533f;
    camera.fov_x = settings.camera_fov_deg * 0.0174533f;
    camera.viewport_w = layout.scene.w;
    camera.viewport_h = layout.scene.h;
    camera.distance = distance_for_cell_px(camera, settings.camera_cell_px);
    ViewWindow view_window = derive_view_window(camera, kMaxPropHeight, 60.f);

    presentation::UiStateMessage ui_state = build_ui_state(view_window, layout, settings, audio_engine.is_open());
    std::string last_ui_state = presentation::encode_ui_state(ui_state);
    (void)link.send_ui_state(ui_state); //!< 握手直後に必ず 1 回（v1 §7）
    /*!
     * @name サブパネルの中身が**まだ往復していない**あいだの守り（2026-08-23 に気づいた）
     *
     * @details コアが送ってくる `frame.sub_panels[i].kind` は v1 §7 の**実効値**で、
     * 画面はそれを自分の設定へ吸い上げる（`=`→`w` で変えたぶんを拾うため）。
     * ところが**起動直後のフレームは、コアがこちらの希望を受け取る前に作られている**。
     * そこには**コア自身の既定**が載っており、それを吸い上げると
     * **cfg に覚えた割り当てが毎回の起動で消える**——実測でそうなっていた
     * （Sil-Q は `init2.c:1212` で 0/1/4/5/2/7/9 を立てる。こちらが覚えた
     * 7/9/4/5/2/0/1 が起動のたびにそれへ置き換わっていた）。
     *
     * だから**往復が済むまで吸い上げない**。済んだ印は「コアが返してきた値が、
     * こちらが送った値（コアが落とす番号は `-1` に読み替えたもの）と一致すること」。
     * 落とされる番号はコア自身が `sub_panel_kinds` で申告しているので、
     * こちらで**当てずっぽうに決めない**。
     * @{
     */
    //! 最後に `ui_state` で送った中身。`-2` は「まだ 1 度も送っていない」（`-1` は正しい値）。
    int sent_sub_kinds[kUiSubPanels];
    for (int i = 0; i < kUiSubPanels; ++i) {
        sent_sub_kinds[i] = settings.sub_panel_kind[i];
    }
    bool sub_kinds_synced = false;
    /*! @} */
    std::string round_trip_report = "(まだ)";
    bool round_trip_ok = true;
    int &hover_gx = input_state.hover_gx;
    int &hover_gy = input_state.hover_gy;
    bool &hover_valid = input_state.hover_valid;

    GameFrame frame{};
    frame.title_screen = true; //!< 最初の frame が来るまでの数秒ぶん（コアの `init_angband`）
    std::size_t frame_bytes = 0;
    bool quit_sent = false;
    Uint64 quit_sent_at = 0;
    bool core_died_unexpectedly = false;
    Uint64 core_disconnected_at = 0;
    /*!
     * @name 言語切り替えでコアを起こし直す（追補 A2。`kRunRelaunchCore`）
     * @details コアへ降ろした言語を控えておき、機能メニューで替えられたら気づく。
     * 気づいたら `quit_request` を送り（コアはそこで**保存する**）、`exit` を待って
     * `kRunRelaunchCore` で返る。**保存していない進行は消えない**——決めたこと
     * 「その際自動セーブしてコアを終了させ、コアの再起動時に自動ロードさせる」。
     * @{
     */
    std::string core_lang(i18n::current());
    bool relaunch_for_lang = false;
    /*! @} */
    int result = 0;
    double fps = 0.0;
    Uint64 fps_mark = clock::ticks64();
    int fps_frames = 0;
    int frame_index = 0;
    /*
     * `--shot=` は「地形が出てから」数える。起動からの通し番号で撮ると、
     * セーブを読み込む操作にかかる時間しだいでタイトル画面が写る。
     */
    int frames_with_terrain = 0;
    VideoRecorder recorder; //!< `HD2D_RECORD=<mp4>` のときだけ動く（デストラクタで ffmpeg を閉じる）
    recorder.start_from_env();
    //! 観戦カメラ（設計書 §4.6）の直前の状態。切り替わりの 1 行を stderr に出すためだけ。
    bool arena_watch_before = false;
    /*!
     * 明るい部屋の混ぜ具合（0 = 通路 / 1 = 明るい部屋）。**主ループをまたいで持つ**
     * ——なめらかに動かすための状態なので、フレームごとに作り直しては意味が無い。
     */
    float lit_room_mix = 0.f;
    /*!
     * 光の届く範囲（プレイヤの光源で照らされているマス）の、マスごとの照らされ具合。
     * **主ループをまたいで持つ**——短い時間でなめらかに寄せるための状態なので、
     * フレームごとに作り直しては意味が無い。
     */
    LitRangeField lit_range_field;

    /* --------------------------------------------------------------- 駆動ループ */
    Uint64 last_tick = clock::ticks64();
    /*!
     * 動きの時刻（P6）。**`frame_seconds` を積んだもの**であって `clock::ticks64()` ではない。
     * 起動からの実時刻をそのまま使うと、読み込みで長く止まったぶんだけ風の位相が飛ぶ。
     */
    float world_seconds = 0.f;
    for (;;) {
        //! **1 周ぶん時計を進める。**決め打ちのときだけ効く（`app/app_clock.h`）。
        clock::advance_frame();
        //! **記録から 1 周ぶん配る。**再生モードのときだけ効く（`net/core_link.h`）。
        link.replay_step();
        const Uint64 loop_started = clock::ticks64();
        const Uint64 loop_started_perf = clock::perf();
        if (slow_frame_ms > 0.0) {
            if (diag_prev_loop_perf != 0) {
                const double gap = static_cast<double>(loop_started_perf - diag_prev_loop_perf) * 1000.0 / static_cast<double>(perf_freq);
                if (gap >= slow_frame_ms) {
                    std::fprintf(stderr,
                        "[hd2d] diag t=%llu ms: slow-frame #%d gap=%.1f work=%.1f (decode=%.1f meaning=%.1f build=%.1f shadow=%.1f submit=%.1f post=%.1f present=%.1f) tail=%.1f\n",
                        static_cast<unsigned long long>(clock::ticks64()), frame_index, gap, ms_frame, ms_decode, ms_meaning, ms_build,
                        ms_shadow, ms_submit, ms_post, ms_present, gap - ms_frame);
                }
            } else {
                diag_mark("主ループの最初の周");
            }
            diag_prev_loop_perf = loop_started_perf;
        }
        // なめらか移動に使う経過秒。長すぎる間（読み込み中など）は 0.1 秒で頭打ちにする。
        const float frame_seconds = std::min(0.1f, static_cast<float>(loop_started - last_tick) / 1000.f);
        last_tick = loop_started;
        world_seconds += frame_seconds;
        ++frame_index;
        if (settings.modern_ui && feature_menu.is_open()) {
            menu_before_input = feature_menu;
            menu_before_input_frame = frame_index;
        }

        /*
         * (0') VR のフレームの頭。**`--vr` のときだけ。**
         *
         * ここが**歩調を握る**——`xrWaitFrame` がランタイムの表示周期まで待つ。
         * 怠るとランタイムに切られる `xrPollEvent` も、この中で吸っている（罠 7）。
         *
         * 姿勢だけ先に取り、**絵は (d) でシーンが組み上がってから**描く。
         * 対（`xrBeginFrame` / `xrEndFrame`）はこのループの中で必ず閉じる。
         */
        bool vr_frame_ready = false;
        xr::Frame vr_frame;
        RenderView vr_views[2];
        if (xr.valid()) {
            vr_frame_ready = xr.begin_frame(vr_frame);
        } else if (options.vr_fake) {
            /*
             * 疑似 HMD（§9）。**窓を左右に割って両眼を並べる。**ランタイムもコンポジタも
             * 無いので、`begin_frame`/`end_frame` の対も swapchain も要らない。
             */
            vr_frame = xr::make_fake_frame(screen_w / 2, screen_h);
            vr_frame_ready = true;
        }
        /*
         * Touch の状態を既存のパッドへ流し込む（§8）。**縁も連射も同時押しも
         * `GamePad` が持っているものをそのまま使う。**焦点が無い回は
         * `sync` が偽を返し、こちらは何もしない——「握ったまま」を作らないため。
         */
        if (xr_input.valid()) {
            ExternalPadState touch;
            if (xr_input.sync(touch)) {
                pad.feed_external(touch);
            }
        }
        //! VR の盤を空間へ据える。中身は `app/run_parts.cpp`。
        place_vr_board(vr_frame_ready, vr_frame, xr, camera, view_window, first_person,
            settings, options, screen_w, screen_h, vr_views, vr_board, vr_room, vr_anchor,
            vr_board_placed, vr_board_first_person, vr_board_turn, vr_panel_yaw);

        //! (0) コアから届いた表。中身は `app/run_parts.cpp`。
        take_core_tables(link, tile_catalog, pad_commands, pad_keymap, pad_macros,
            settings, sub_panel_kind_choices);
        //! DCSS: 原作の JSON の行（届いた順に全部）。申告したコアだけ。
        if (dcss_ui) {
            for (const std::string &payload : link.take_webtiles_payloads()) {
                dcss_ui->ingest_payload(payload);
            }
        }

        // (a) 入力 → `input_event`（v1 §8.3）。**到着順を崩さない。**
        InputBatch input_batch;
        auto &input = input_batch.input;
        hud_state.modern.advance(input);
        hud_state.targeting.advance(input);
        hud_state.targeting.set_font_level(settings.modern_font_level);
        hud_state.modern_theme=settings.modern_theme;
        hud_state.window_theme=settings.window_theme();
        hud_state.modern_font_level=settings.modern_font_level;
        hud_state.modern.set_font_level(settings.modern_font_level);
        bool &want_quit = input_batch.want_quit;
        bool &menu_toggled_this_pump = input_batch.menu_toggled_this_pump;
        /*!
         * @brief 見下ろしの視点を 90° 回す（`dir` は −1 = 左回り／+1 = 右回り）。
         *
         * @details **一人称では何もしない**（あちらは自分で向きを持っており、
         * ここで回すと HMD やマウスと喧嘩する。設計書 §6.6 と同じ扱い）。
         * `camera_turn_goal` を巻き戻さずに足すので、右へ 2 回押せば右へ 180° 回る。
         */
        const auto turn_view = [&](int dir) {
            if ((dir == 0) || first_person.active) {
                return;
            }
            /*
             * **VR のジオラマでは盤を回す**。
             * ここでフラットの `camera_turn` を回しても VR の絵は 1 画素も変わらない
             * ——VR の視点行列は `world_to_stage` から来るので `camera.yaw` を見ておらず、
             * 可視窓（`derive_view_window`）だけが無駄に広がる。
             *
             * **振り分けはこの 1 か所**にする。キー（F5/F6）もスティックも
             * ここを通るので、片方だけ盤・片方だけカメラという状態になりようがない。
             */
            if (vr_frame_ready) {
                vr_board_turn = ((vr_board_turn + dir) % xr::kBoardTurnCount + xr::kBoardTurnCount)
                    % xr::kBoardTurnCount;
                return;
            }
            //! 1 押しで **45°**（2026-09-06 に決めた。従来は 90°）。
            camera_turn = ((camera_turn + dir) % kViewTurnCount + kViewTurnCount) % kViewTurnCount;
            camera_turn_goal += static_cast<float>(dir) * kViewTurnStep;
        };
        //! 開いてよいかの規則は `top_menu_available()`（`app_support.cpp`）の 1 か所。-続く- の最中も開かない（修-01）。
        const auto top_menu_available = [&]() {
            return hd2d::top_menu_available(frame, hud_state.modern.active(), hud_state.targeting.active(),
                core_marks_more_at_message_tail(link.hello_ack().core_name));
        };
        const auto perform_action = [&](int action) {
            switch (action) {
            case kActionNone:
                return;
            case kActionFeatureMenu:
                // A child prompt/list owns input exclusively.  In particular,
                // controller Back used to open the root menu over a one-key
                // Rogue selector, leaving both the core and UI waiting.
                if (!top_menu_available()) {
                    return;
                }
                //! **1 巡 1 回**（`menu_toggled_this_pump`）。2 縁目は開き直しに化ける。
                if (menu_toggled_this_pump) {
                    return;
                }
                menu_toggled_this_pump = true;
                //! 開く直前に一覧を渡す（握手のあとに届くので、起動時には無い）。
                feature_menu.set_sub_panel_kinds(sub_panel_kind_choices);
                feature_menu.set_pad_state(pad_commands, pad.connected() || settings.vpad.show,
                    pad.connected() ? pad.name() : std::string("バーチャルパッド"));
                feature_menu.open_features(settings);
                pad.forget_hold();
                return;
            case kActionLayoutCycle: {
                /*
                 * F9。**いま向いている側だけ**を回す（縦横で別に持つ。2026-08-12 に決めた）。
                 * 並びはメニューの「画面の作り」と同じ 4 つ。片方だけ順序を変えると
                 * 「F9 とメニューで出てくる順が違う」になる。
                 */
                LayoutMode &mode = settings.layout_ref(screen_is_portrait());
                mode = (mode == LayoutMode::Full) ? LayoutMode::Hybrid
                    : ((mode == LayoutMode::Hybrid) ? LayoutMode::Split
                                                    : ((mode == LayoutMode::Split) ? LayoutMode::Tall
                                                                                   : LayoutMode::Full));
                return;
            }
            case kActionSubsToggle:
                // 重なる作りでサブパネル 5 枚を開閉する（`Split` では常に出ているので効かない）。
                settings.subs_open = !settings.subs_open;
                return;
            case kActionFpsToggle:
                first_person.set_active(!first_person.active); //!< 一人称（おまけ）
                return;
            case kActionFpsLevelView:
                //! 一人称でなければ何も起きない（見下ろしの絵は元から水平を向いていない）。
                first_person.level_view();
                return;
            case kActionVrRecenter:
                /*
                 * VR の再中心（§5）。**次のフレームの頭で置き直す**——ここで置くと、
                 * この瞬間の姿勢がまだ来ていない（姿勢は `xrWaitFrame` の後で引く）。
                 * VR でなければ誰も見ないので何も起きない。
                 */
                vr_board_placed = false;
                return;
            case kActionTurnLeft:
                turn_view(-1); //!< 見下ろしの 90° 視点回転
                return;
            case kActionTurnRight:
                turn_view(1); //!< 同（既定 F6）
                return;
            case kActionCommandMenu:
                if (!top_menu_available()) {
                    return;
                }
                /*
                 * コマンドメニュー（2026-08-22 に決めた）。**1 巡 1 回**は
                 * `kActionFeatureMenu` と同じ理由（2 縁目が開き直しに化ける）。
                 * 開く直前に一覧を渡すのも同じ——握手のあとに届くので、起動時には無い。
                 */
                if (menu_toggled_this_pump) {
                    return;
                }
                menu_toggled_this_pump = true;
                feature_menu.set_sub_panel_kinds(sub_panel_kind_choices);
                feature_menu.set_pad_state(pad_commands, pad.connected() || settings.vpad.show,
                    pad.connected() ? pad.name() : std::string("バーチャルパッド"));
                feature_menu.open_commands(settings);
                pad.forget_hold();
                return;
            case kActionDiagonalOnly:
                /*
                 * 斜め移動限定（`ui/diagonal_only.h`）。**パッドのボタンを押した縁だけがここへ来る**
                 * （キーボードは `input_controller.cpp` が押している間を直に見るので、ここへは来ない）。
                 * 押している間だけ効くので、ここでは「正しい層の押しだった」印を立てるだけにして、
                 * 離したかどうかは毎フレーム `GamePad::input_down` で見る（下の (a') の頭）。
                 */
                input_state.diagonal_pad_armed = true;
                return;
            case kActionMainPanelToggle:
                /*
                 * メインパネルの 3D ／ 2D アスキー。
                 * **周りの UI は何も変わらない**ので、切り替えても操作の勘は狂わない。
                 */
                settings.main_panel = (settings.main_panel == Hd2dSettings::MainPanel::Ascii)
                    ? Hd2dSettings::MainPanel::Hd2d
                    : Hd2dSettings::MainPanel::Ascii;
                return;
            default:
                break;
            }
            (void)emit_core_command(action, pad_commands, input);
        };
        /*!
         * @brief 一人称の「**向いている方へ**歩く」を効かせてよい場面か。
         *
         * @details 偽のときは方向をそのまま（画面の上下左右のまま）流す。
         *
         * **メニューが出ている間は必ず偽にすること**（2026-08-11 に気づいた
         * 「FPS モードでメニューに入った際、カーソルの移動が FPS で向いている方向に
         * あわせて上下左右がいれかわってしまう」）。メニューのカーソルは
         * **画面の上下左右**で動くものであって、世界の東西南北ではない。
         * 南を向いて店に入ると ↑ が「下の行」になり、選べなくなる。
         *
         * 見るべき場面は 3 通りあって、**どれか 1 つでも落とすと入れ替わりが残る**:
         *   - 機能メニュー（この exe のもの）
         *   - **コアの Term の写し**（通常メニュー・店・建物・持ち物。`frame_shows_map`）
         *   - 選択肢・はい／いいえ・数値入力（`UiCursors`）
         * 実際に 2 つ目が抜けていた。あちらは `UiCursors` を埋めない画面があるので、
         * カーソルの有無だけを見ていると素通りする。
         *
         * @note **毎回呼び直すこと**（値で持たない）。同じフレームの中で
         * 機能メニューが開くことがあり、開いた後の入力は既に「偽」でなければならない。
         */
        const auto fps_drives_movement = [&]() {
            return first_person.active && !feature_menu.is_open() && frame_shows_map(frame)
                && hud_state.cursors.choices().empty()
                && hud_state.cursors.prompt().choices.empty()
                && !hud_state.cursors.numeric().active;
        };
        /*!
         * @brief 見下ろしの視点回転が**方向入力の意味を変えてよい**場面か。
         *
         * @details 見るべき場面は `fps_drives_movement()` とまったく同じもので、
         * 一人称かどうかだけが裏返る。**メニュー・店・持ち物・選択肢の矢印は回さない**
         * ——あれは画面の上下左右であって世界の東西南北ではない（設計書 §3.2-3）。
         * ここを落とすと、90° 回した状態で店に入った瞬間に選べなくなる
         * （一人称で実際に踏んだ穴と同じ形。2026-08-11 に気づいた）。
         */
        const auto turn_drives_movement = [&]() {
            return !first_person.active && (camera_turn != 0) && !feature_menu.is_open() && frame_shows_map(frame)
                && hud_state.cursors.choices().empty()
                && hud_state.cursors.prompt().choices.empty()
                && !hud_state.cursors.numeric().active;
        };
        /*!
         * @brief **画面基準**で作った `move` を世界基準へ回す。**回すのはここ 1 か所だけ。**
         *
         * @details 設計書 罠 1: 矢印・テンキー・十字・バーチャルパッドが別々の場所で
         * 回されると、必ず 1 経路が素の向きのまま残る。画面基準の入力を作る経路は
         * **この関数を通してから積むこと**。
         *
         * 逆に **通してはいけない**経路（もともと世界基準なので二重に回る）:
         *   - クリック移動・ミニマップのクリック（`gx - player_gx` で作っている）
         *   - `ClickPath::advance()`（経路は世界座標）
         *   - 一人称の `FpsMode::direction_delta()`（向いている方角で既に回っている）
         */
        const auto turn_screen_move = [&](presentation::InputEventWire &wire) {
            if ((wire.e != "move") || !turn_drives_movement()) {
                return;
            }
            rotate_screen_delta(camera_turn, wire.dx, wire.dy);
        };
        /*
         * SDL の出来事を全部捌く。中身は `app/input_controller.cpp`（`pump_sdl_events`）。
         * 触るものが多いので束にして渡す（`InputPumpContext` の注記）。
         */
        //! DCSS のタブの欄へのマウス（申告 `dcss-ui` のコアだけ。ほかのコアでは空のまま）。
        std::function<bool(const SDL_Event &)> dcss_mouse_fn;
        if (dcss_ui && dcss_ui->layout_ok() && dcss_ui->in_game() && !vr_frame_ready) {
            dcss_mouse_fn = [&](const SDL_Event &e) -> bool {
                dcss::DcssTabMouse m;
                if (e.type == SDL_MOUSEMOTION) {
                    m.kind = dcss::DcssTabMouse::Kind::Move;
                    m.x = e.motion.x;
                    m.y = e.motion.y;
                } else {
                    m.kind = dcss::DcssTabMouse::Kind::Press;
                    m.x = e.button.x;
                    m.y = e.button.y;
                    m.button = (e.button.button == SDL_BUTTON_RIGHT) ? 3 : 1;
                    if ((e.button.button != SDL_BUTTON_LEFT) && (e.button.button != SDL_BUTTON_RIGHT)) {
                        return false;
                    }
                }
                const SDL_Keymod mod = SDL_GetModState();
                m.mods = (((mod & KMOD_SHIFT) != 0) ? 1 : 0) | (((mod & KMOD_CTRL) != 0) ? 2 : 0);
                const bool consumed = dcss_ui->on_tab_mouse(m);
                for (const dcss::DcssTabOutgoing &o : dcss_ui->take_tab_outgoing()) {
                    if (o.webtiles) {
                        (void)link.send_webtiles_input(o.text);
                    } else {
                        (void)link.send_dcss_message("dcss_action", o.text);
                    }
                }
                return consumed;
            };
        }
        //! DCSS の窓の部品（段 U3。申告 `dcss-ui` のコアだけ）。窓が食った出来事は、窓がコアへ送る行（原作の制御メッセージ）にして送る。
        std::function<bool(const SDL_Event &, bool)> dcss_window_fn;
        if (dcss_ui && !vr_frame_ready) {
            dcss_window_fn = [&](const SDL_Event &e, bool reserved) -> bool {
                const bool consumed = dcss_ui->on_back_key(e) || dcss_ui->on_window_event(e, reserved); // 戻るキー（Android）は Esc
                for (const std::string &line : dcss_ui->take_window_outgoing()) {
                    (void)link.send_webtiles_input(line);
                }
                return consumed;
            };
        }
        //! DCSS の地図のマウス操作（段 U3・U4）。コアへ送る行（`click_cell`・`hd2d_*`）を取り出して送る。
        std::function<bool(const SDL_Event &, bool, int, int)> dcss_map_fn;
        if (dcss_ui && !vr_frame_ready) {
            dcss_map_fn = [&](const SDL_Event &e, bool on_map, int dx, int dy) -> bool {
                const bool consumed = dcss_ui->on_map_event(e, on_map, dx, dy);
                for (const std::string &line : dcss_ui->take_map_outgoing()) {
                    (void)link.send_webtiles_input(line);
                }
                return consumed;
            };
        }
        //! DCSS のタッチ（段 7。申告 `dcss-ui` のコアだけ）。指の出来事を原作の Android 版と同じ決まりのマウスの出来事に直して積み直す（`dcss/dcss_touch.h`）。
        std::function<void(const SDL_Event &, bool)> dcss_touch_fn;
        if (dcss_ui && !vr_frame_ready) {
            dcss_touch_fn = [&](const SDL_Event &e, bool ignore_down) {
                std::vector<SDL_Event> synthesized;
                dcss_ui->on_finger(e, screen_w, screen_h, SDL_GetTicks(), ignore_down, synthesized);
                for (SDL_Event &m : synthesized) {
                    (void)SDL_PushEvent(&m);
                }
            };
        }
        InputPumpContext pump_ctx{
            .screen_w = screen_w,
            .screen_h = screen_h,
            .usable_area = usable_area,
            .settings = settings,
            .layout = layout,
            .camera = camera,
            .camera_turn = camera_turn,
            .pad = pad,
            .vpad = vpad,
            .pad_commands = pad_commands,
            .pad_keymap = pad_keymap,
            .core_name = link.hello_ack().core_name,
            .feature_menu = feature_menu,
            .floor_cutin = floor_cutin,
            .hud_state = hud_state,
            .click_path = click_path,
            .map_click_enabled = core_map_click,
            .text = text,
            .first_person = first_person,
            .sub_panel_kind_choices = sub_panel_kind_choices,
            .frame = frame,
            .targeting_entities = entity_view.targeting_entities,
            .state = input_state,
            .perform_action = perform_action,
            .fps_drives_movement = fps_drives_movement,
            .turn_drives_movement = turn_drives_movement,
            .turn_screen_move = turn_screen_move,
            .batch = input_batch,
            .dcss_mouse = dcss_mouse_fn,
            .dcss_window = dcss_window_fn,
            .dcss_map = dcss_map_fn,
            .dcss_touch = dcss_touch_fn,
        };
        pump_sdl_events(pump_ctx);
#if defined(__ANDROID__)
        if (dcss_ui && dcss_ui->take_keyboard_toggle()) {
            // 原作の `CMD_TOGGLE_KEYBOARD`（システム命令の欄・ゲームメニュー）: 画面のキーボードを出し入れする。
            if (SDL_IsTextInputActive() == SDL_TRUE) {
                SDL_StopTextInput();
            } else {
                SDL_StartTextInput();
                ime_dismissed = false;
            }
        }
#endif
        /*
         * (a') パッド（P8）。**キーと同じ道へ落とす**ので、割り当ても
         * カーソル操作も機能メニューもそのまま効く（専用の経路を作らない）。
         */
        {
            const bool focused = (SDL_GetWindowFlags(window.window) & SDL_WINDOW_INPUT_FOCUS) != 0
                || (!options.test_input_file.empty()&&std::getenv("HD2D_TEST_HIDE_WINDOW")!=nullptr);
            /*
             * DCSS のパッドの出力を行う（段 5。`dcss/dcss_pad.h`）。申告 `dcss-ui` のコアだけが呼ぶ。
             * コアへのキーは原作の制御メッセージ `{"msg":"key","keycode":N}`（負の数はコマンドや特殊キー。`-(CMD_*)` はキーマップを通らずそのコマンドが走る）、
             * 窓の中は窓の部品へ SDL のキーを渡す（窓がコアへの送りを決める）、クリックは疑似カーソルの位置に SDL のマウスの出来事を積む
             * （本物のマウスと同じ道を通る）。
             */
            //! 検査用: 環境変数 HD2D_DCSS_PAD_TRACE があれば、パッドが出したものを標準エラーへ 1 行ずつ出す（tools/dcss/dc_pad_check.py が読む）。
            static const bool dcss_pad_trace = (std::getenv("HD2D_DCSS_PAD_TRACE") != nullptr);
            const auto dcss_pad_apply = [&](const std::vector<dcss::DcssPadAction> &actions) {
                using Kind = dcss::DcssPadAction::Kind;
                for (const dcss::DcssPadAction &a : actions) {
                    if (dcss_pad_trace && (a.kind != Kind::None)) {
                        static const char *const kinds[] = { "none", "key", "window-key", "click", "feature-menu", "turn-view", "open-bindings", "slot-capture" };
                        std::fprintf(stderr, "[dcss-pad] t=%u scene=%d %s key=%d sym=%d button=%d turn=%d slot=%d\n", static_cast<unsigned>(SDL_GetTicks()),
                            static_cast<int>(dcss_ui->scene()), kinds[static_cast<int>(a.kind)], a.key, a.sym, a.button, a.turn, a.slot);
                    }
                    switch (a.kind) {
                    case Kind::CoreKey:
                        (void)link.send_webtiles_input("{\"msg\":\"key\",\"keycode\":" + std::to_string(a.key) + "}");
                        break;
                    case Kind::WindowKey: {
                        if (dcss_window_fn) {
                            SDL_Event key_event{};
                            key_event.type = SDL_KEYDOWN;
                            key_event.key.state = SDL_PRESSED;
                            key_event.key.keysym.sym = a.sym;
                            key_event.key.keysym.scancode = SDL_GetScancodeFromKey(a.sym);
                            key_event.key.keysym.mod = 0;
                            (void)dcss_window_fn(key_event, false);
                        }
                        break;
                    }
                    case Kind::Click: {
                        int cx = 0;
                        int cy = 0;
                        if (!dcss_ui->pad().cursor_position(cx, cy)) {
                            SDL_GetMouseState(&cx, &cy);
                        }
                        SDL_Event click{};
                        click.type = SDL_MOUSEBUTTONDOWN;
                        click.button.timestamp = SDL_GetTicks();
                        click.button.which = dcss::kPadMouseId;
                        click.button.button = (a.button == 3) ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
                        click.button.state = SDL_PRESSED;
                        click.button.clicks = 1;
                        click.button.x = cx;
                        click.button.y = cy;
                        if (dcss_pad_trace) {
                            std::fprintf(stderr, "[dcss-pad] click-at %d,%d\n", cx, cy);
                        }
                        (void)SDL_PushEvent(&click);
                        click.type = SDL_MOUSEBUTTONUP;
                        click.button.state = SDL_RELEASED;
                        (void)SDL_PushEvent(&click);
                        break;
                    }
                    case Kind::FeatureMenu:
                        perform_action(kActionFeatureMenu);
                        break;
                    case Kind::TurnView:
                        turn_view(a.turn);
                        break;
                    case Kind::OpenBindings:
                        (void)dcss_ui->pad_screen().open();
                        break;
                    case Kind::SlotCapture:
                        dcss_ui->pad_screen().on_slot(a.slot);
                        break;
                    case Kind::None:
                        break;
                    }
                }
                for (const std::string &line : dcss_ui->take_window_outgoing()) {
                    (void)link.send_webtiles_input(line);
                }
                if (dcss_ui->pad().take_changed()) {
                    settings.dcss_pad_binds = dcss_ui->pad().save_line(); //!< 割り当ての画面で変えた分を設定へ（cfg に書かれる）
                }
            };
            if (dcss_ui && dcss_ui->pad().take_changed()) {
                settings.dcss_pad_binds = dcss_ui->pad().save_line(); //!< 窓の操作（マウス・キー）で変えた分
            }
            /*
             * ボタン 1 個ぶんの道筋。**物理パッドとバーチャルパッドの両方がここを通る**
             * （2026-08-12 に決めた。入口ごとに書くと片方だけ直された状態に必ずなる）。
             */
            const auto route_pad_press = [&](const PadPress &press) {
                const PadInput pressed = press.input;
                /*
                 * **同時押しの行き先は「画面が何か」で決まる。**
                 *
                 * ここは順番がすべてである。初版は**この lambda の頭で**層の押しを
                 * 処理して `return` していたので、割り当ての変更モードにも配置の編集にも
                 * カットインにも**同時押しが 1 度も届かなかった**（2026-08-14 に気づいた
                 * 「キー割り当てでタッチパッドの同時押しが反応しない」）。しかも変更モードで
                 * LB＋X を押すと、割り当たらないままマクロが**ゲームへ流れて**いた。
                 *
                 * 覆っている画面（編集・幕・メニュー）を**先に**通し、遊んでいる最中の
                 * 割り当て／マクロは**その後**に置く。単独押しと同じ並びである。
                 */
                //! 配置の編集中は B / Back ＝ 閉じるだけ（ほかは編集画面が相手なので捨てる）。
                if (vpad.editor_open()) {
                    if ((pressed == PadInput::B) || (pressed == PadInput::Back)) {
                        vpad.close_editor();
                    }
                    return;
                }
                //! カットインの間は A（決定）だけが効く。ほかは捨てる（キーと同じ扱い）。
                if (floor_cutin.blocks_input()) {
                    if (pressed == PadInput::A) {
                        (void)floor_cutin.confirm();
                    }
                    return;
                }
                if (feature_menu.is_open()) {
                    /*
                     * 割り当ての変更モード。**同時押しもここで受け取る**（LB＋X を
                     * 押せばその枠に割り当たる）。修飾そのものは `PadBinds::assign` が
                     * 層 0 でだけ受けるので、`LB＋LB` のような押しようのない枠は作られない。
                     *
                     * **取消として横取りするのは単独の B / Back だけ**（気づいたこと
                     * 2026-08-14「同時押しなら AB は割り当ててよいのでは？」）。
                     * `LB＋B` は層の中では空いている枠なので、変更モードへ通す。
                     * ここで単独まで通すと、**変更モードから降りる道が無くなる**。
                     */
                    const bool solo_cancel = (press.mods == 0)
                        && ((pressed == PadInput::B) || (pressed == PadInput::Back));
                    if (feature_menu.waiting_for_key()) {
                        if (!solo_cancel
                            && feature_menu.handle_bind_pad(pressed, press.mods, settings)) {
                            return;
                        }
                    }
                    /*
                     * **層の押しはメニュー操作へ回さない**（修飾を握ったまま決定を
                     * 押したつもりが層のコマンドになる、を避ける）。割り当ての変更
                     * モード以外では、メニューを開いている間の同時押しは**捨てる**
                     * ——ここでマクロを流すと、メニューの裏でゲームが動く。
                     */
                    if(press.mods==0) {
                        if(pressed==PadInput::LeftTrigger) {feature_menu.handle(MenuNav::PreviousPane,settings);return;}
                        if(pressed==PadInput::RightTrigger) {feature_menu.handle(MenuNav::NextPane,settings);return;}
                    }
                    if(feature_menu.tab_navigation_active() && press.mods==0) {
                        if(pressed==PadInput::LeftShoulder) {feature_menu.handle(MenuNav::PreviousTab,settings);return;}
                        if(pressed==PadInput::RightShoulder) {feature_menu.handle(MenuNav::NextTab,settings);return;}
                        if(pressed==PadInput::X) {feature_menu.handle(MenuNav::Categories,settings);return;}
                    }
                    if (press.mods != 0) {
                        return;
                    }
                    /*
                     * **メニューの中では B と Back を最初に見る**（既存 UI の K-4）。
                     * 取消をコマンドへ割り当てられる作りにすると、メニューから出られなくなる。
                     */
                    if ((pressed == PadInput::B) || (pressed == PadInput::Back)) {
                        /*
                         * **☰ の取消も 1 巡 1 回**（§15 の 2）。開いた縁と閉じる縁が
                         * 同じ巡に並ぶと、開いてすぐ閉じる。B（取消）を巻き添えにするが、
                         * 1 フレームに 2 回押せる指は無いので実害は無い。
                         */
                        if (menu_toggled_this_pump) {
                            return;
                        }
                        menu_toggled_this_pump = true;
                        feature_menu.handle(MenuNav::Cancel, settings);
                    } else if (pressed == PadInput::A) {
                        feature_menu.handle(MenuNav::Confirm, settings);
                    }
                    return;
                }
                /*
                 * DCSS（申告 `dcss-ui` のコア。段 5）: 原作の日本語版のタイル版の割り当てと動き（`dcss/dcss_pad.h`）。HD2D の割り当て・
                 * 機能メニューの Back・視点の操作は使わない（原作と割り当てがぶつかるため。D12）。ほかのコアはここを通らない。
                 */
                if (dcss_ui) {
                    dcss_pad_apply(dcss_ui->pad().on_press(press, dcss_ui->scene(), dcss_ui->pad_screen().capturing()));
                    return;
                }
                /*
                 * ---- ここから下は遊んでいる最中 ----
                 * **同時押しも単独押しと同じ規則**（2026-08-14 に決めた「同時押しも
                 * 割り当て対象に含められる？」）。割り当てがあればそれ、無ければマクロの
                 * トリガー。層ごとに違う読み方をさせない。
                 */
                if(hud_state.targeting.active()) {
                    if(press.mods==0) {
                        if(pressed==PadInput::A)hud_state.targeting.confirm(input);
                        else if(pressed==PadInput::B||pressed==PadInput::Back)hud_state.targeting.cancel(input);
                        else if(pressed==PadInput::LeftShoulder)hud_state.targeting.cycle(-1,input);
                        else if(pressed==PadInput::RightShoulder)hud_state.targeting.cycle(1,input);
                        else if(pressed==PadInput::Start)hud_state.targeting.toggle_extras(); // 追加操作の一覧（AH-54）
                    }
                    return;
                }
                if(hud_state.modern.active()) {
                    if(press.mods==0) {
                        if(pressed==PadInput::A)hud_state.modern.handle(CursorNav::Confirm,input);
                        else if(pressed==PadInput::B || pressed==PadInput::Back)hud_state.modern.cancel(input);
                        else if(pressed==PadInput::LeftTrigger)hud_state.modern.focus_details(false);
                        else if(pressed==PadInput::RightTrigger)hud_state.modern.focus_details(true);
                        else if(pressed==PadInput::LeftShoulder){if(hud_state.modern.item_window()||hud_state.modern.parent_tab_navigation_active())hud_state.modern.tab(-1);else hud_state.modern.page(-1,input);}
                        else if(pressed==PadInput::RightShoulder){if(hud_state.modern.item_window()||hud_state.modern.parent_tab_navigation_active())hud_state.modern.tab(1);else hud_state.modern.page(1,input);}
                        else if(pressed==PadInput::X)hud_state.modern.show_help(input);
                        else if(pressed==PadInput::Start)hud_state.modern.toggle_commands();
                    } else if(pressed==PadInput::LeftShoulder||pressed==PadInput::RightShoulder)hud_state.modern.scroll_horizontal(pressed==PadInput::LeftShoulder?-1:1);
                    return;
                }
                //! 「設定」タブへ移った直後の肩ボタン（キーボードの Shift と同じ扱い。input_controller.cpp）。
                if(press.mods==0&&(pressed==PadInput::LeftShoulder||pressed==PadInput::RightShoulder)&&hud_state.modern.awaiting_parent_child()){hud_state.modern.tab(pressed==PadInput::LeftShoulder?-1:1);return;}
                if (press.mods == 0 && hud_state.cursors.prompt_uses_shoulders()) {
                    if (pressed == PadInput::LeftShoulder) {
                        hud_state.cursors.step_prompt(-1);
                        return;
                    }
                    if (pressed == PadInput::RightShoulder) {
                        hud_state.cursors.step_prompt(1);
                        return;
                    }
                }
                if (press.mods != 0) {
                    const int chord_action = settings.pad.command[press.mods][static_cast<int>(pressed)];
                    if (chord_action == kActionNone) {
                        (void)emit_pad_macro_trigger(pressed, press.mods, input);
                    } else {
                        perform_action(chord_action);
                    }
                    return;
                }
                if (pressed == PadInput::Back) {
                    /*
                     * ☰ で開く。**1 巡 1 回**（`menu_toggled_this_pump`）——1 回の押しが
                     * 2 つの縁になって届くと、開いた直後に下の「メニュー中の Back ＝ 取消」で
                     * 閉じてしまう（Quest の「一瞬開いて閉じる」。§15 の 2・罠 Q-12/Q-13）。
                     */
                    if (!top_menu_available()) {
                        presentation::InputEventWire wire;
                        wire.e = "cancel";
                        input.events.push_back(wire);
                        return;
                    }
                    if (menu_toggled_this_pump) {
                        return;
                    }
                    menu_toggled_this_pump = true;
                    feature_menu.set_sub_panel_kinds(sub_panel_kind_choices);
                    feature_menu.set_pad_state(pad_commands, pad.connected() || settings.vpad.show,
                        pad.connected() ? pad.name() : std::string("バーチャルパッド"));
                    feature_menu.open_features(settings);
                    pad.forget_hold();
                    return;
                }
                if (pressed == PadInput::A) {
                    if (hud_state.cursors.handle(CursorNav::Confirm, input)) {
                        return; //!< 選択肢・はい／いいえ・数値入力はカーソル層のもの
                    }
                    /*
                     * **A は決定の専任**。以前は「命令を待って
                     * いるだけなら A はコマンドメニューを開く」だったが、コマンドメニューは
                     * いま F1 とパッド Start だけで開く——A が二重の入口にならないよう、
                     * ここでは開かない。
                     *
                     * **命令をただ待っているだけ**（確認する相手が無い。`nothing_to_confirm()`）
                     * なら、A は何もしない——コアへも `confirm` を送らない。変愚系のように
                     * 旗を立てないコアでは常に偽なので、今までどおり `\r` 相当の確認が飛ぶ
                     * ——**あちらの動きは 1 ビットも変わらない**（必守制約 3）。
                     */
                    if (nothing_to_confirm(frame)) {
                        return;
                    }
                    presentation::InputEventWire wire;
                    wire.e = "confirm";
                    input.events.push_back(wire);
                    return;
                }
                if (pressed == PadInput::B) {
                    presentation::InputEventWire wire;
                    wire.e = "cancel";
                    input.events.push_back(wire);
                    return;
                }
                /*
                 * **割り当ての無いボタンはマクロのトリガーになる**（2026-08-14 に決めた。
                 * `ui/game_pad.h` の「マクロのトリガーとしてのパッド」）。
                 * 登録が無ければコアが黙って飲むので、押しても何も起きないだけである。
                 * 割り当て済みのボタンは今までどおり——マクロを乗せたければ先に外す。
                 */
                const int pad_action = settings.pad.command[0][static_cast<int>(pressed)];
                if (pad_action == kActionNone) {
                    (void)emit_pad_macro_trigger(pressed, 0, input);
                    return;
                }
                //! 割り当ては**キーボードと同じ道**（`perform_action`）へ落とす。
                perform_action(pad_action);
            };
            PadPress pad_press;
            while (pad.take_pressed(pad_press)) {
                route_pad_press(pad_press);
            }
            while (vpad.take_pressed(pad_press)) {
                route_pad_press(pad_press);
            }
            /*
             * 斜め移動限定（`kActionDiagonalOnly`。`ui/diagonal_only.h`）。**押している間だけ**効く。
             * パッドのボタンは、押した縁（`perform_action`）で `diagonal_pad_armed` が立ち、押し続けている間だけ
             * `diagonal_pad_held` が真になる。キーボードの割り当ての押し具合（`diagonal_key_held`）も合わせて、
             * 「地図の上を歩く場面」（`diagonal_only_applies`）だけ方向を斜めに限る。
             * 十字キー・スティックの向きを出す `poll_direction` にも、その場面でだけ知らせる。
             */
            {
                const PadPress diagonal_bind = settings.pad.input_for(kActionDiagonalOnly);
                const bool diagonal_pad_down = (diagonal_bind.input != PadInput::Count)
                    && (pad.input_down(diagonal_bind.input) || vpad.input_down(diagonal_bind.input));
                if (!diagonal_pad_down) {
                    input_state.diagonal_pad_armed = false;
                }
                input_state.diagonal_pad_held = input_state.diagonal_pad_armed && diagonal_pad_down;
            }
            const bool diagonal_walk = diagonal_only_active(pump_ctx);
            pad.set_diagonal_only(diagonal_walk);


            /*
             * 方向 1 回ぶんの道筋。**こちらも両方のパッドが通る**（押しの `route_pad_press` と同じ理由）。
             */
            const auto route_pad_direction = [&](int pad_dx, int pad_dy) {
                if (vpad.editor_open()) {
                    return; //!< 編集中に裏のメニューを動かさない（見えない画面への誤操作）
                }
                if (feature_menu.is_open()) {
                    MenuNav nav = MenuNav::None;
                    if (pad_dy < 0) {
                        nav = MenuNav::Up;
                    } else if (pad_dy > 0) {
                        nav = MenuNav::Down;
                    } else if (pad_dx < 0) {
                        nav = MenuNav::Left;
                    } else if (pad_dx > 0) {
                        nav = MenuNav::Right;
                    }
                    feature_menu.handle(nav, settings);
                } else if (dcss_ui) {
                    /*
                     * DCSS（段 5）: 原作の日本語版のタイル版と同じキー（矢印・斜めは Home・PgUp・End・PgDn。窓の中は縦の成分だけ）。
                     * 視点を回しているときは、コマンド待ち・狙いの向きを画面基準で回す（ほかのコアの十字・キーボードと同じ）。
                     */
                    const dcss::DcssPadScene dcss_scene = dcss_ui->scene();
                    presentation::InputEventWire wire;
                    wire.e = "move";
                    wire.dx = pad_dx;
                    wire.dy = pad_dy;
                    if ((dcss_scene == dcss::DcssPadScene::Command) || (dcss_scene == dcss::DcssPadScene::Target)) {
                        turn_screen_move(wire);
                    }
                    if (dcss_scene == dcss::DcssPadScene::Command) {
                        click_path.cancel(); //!< 自分で歩き始めたら経路は捨てる
                    }
                    dcss_pad_apply({ dcss_ui->pad().on_direction(wire.dx, wire.dy, dcss_scene) });
                } else if(hud_state.targeting.active()&&hud_state.targeting.extras_open()) {
                    if(pad_dy!=0)hud_state.targeting.step_extras(pad_dy<0?-1:1); // 一覧のカーソル（AH-54）
                } else if(hud_state.targeting.active()) {
                    presentation::InputEventWire move;move.e="move";move.dx=pad_dx;move.dy=pad_dy;
                    turn_screen_move(move);
                    hud_state.targeting.move(5+move.dx-3*move.dy,input);
                } else if(hud_state.modern.active()) {
                    hud_state.modern.handle(pad_dy<0?CursorNav::Up:(pad_dy>0?CursorNav::Down:(pad_dx<0?CursorNav::Left:CursorNav::Right)),input);
                } else if (fps_drives_movement()) {
                    /*
                     * 一人称（おまけ）。左スティック＝**前後左右**（画面の上が前）。
                     * 十字も同じ道を通る（`poll_direction` が両方まとめて向きにしている）。
                     *
                     * **メニューが出ている間はここへ来ない**（`fps_drives_movement()`）。
                     * 来ると十字が「向いている方角」へ翻訳され、メニューのカーソルの
                     * 上下左右が入れ替わる（2026-08-11 に気づいた）。
                     */
                    int dx = 0;
                    int dy = 0;
                    if (first_person.direction_delta(-pad_dy, pad_dx, dx, dy)
                        && !(diagonal_walk && is_orthogonal_step(dx, dy))) { //!< 斜め移動限定：向いている方へ換えたあとの 1 歩が縦横 1 方向なら何もしない
                        click_path.cancel();
                        presentation::InputEventWire wire;
                        wire.e = "move";
                        wire.dx = dx;
                        wire.dy = dy;
                        input.events.push_back(wire);
                    }
                } else {
                    //! カーソルが出ている画面では**斜めを渡さない**（`cursor_nav_from_key` と同じ理由）。
                    CursorNav nav = CursorNav::None;
                    if ((pad_dx == 0) && (pad_dy < 0)) {
                        nav = CursorNav::Up;
                    } else if ((pad_dx == 0) && (pad_dy > 0)) {
                        nav = CursorNav::Down;
                    } else if ((pad_dy == 0) && (pad_dx < 0)) {
                        nav = CursorNav::Left;
                    } else if ((pad_dy == 0) && (pad_dx > 0)) {
                        nav = CursorNav::Right;
                    }
                    if (hud_state.cursors.handle(nav, input)) {
                        return;
                    }
                    //! 十字も**キーと同じ道**（`handle_text_edit_arrows`）。エディタで数字が増えるのは十字でも同じ。
                    if (handle_text_edit_arrows(frame, nav, input)) {
                        return;
                    }
                    //! 文字入力の間の斜めは渡さない。move はコアで移動キー（数字・hjkl）になり、欄へ字として入る（2026-09-26）。
                    if (frame.text_input_active) {
                        return;
                    }
                    presentation::InputEventWire wire;
                    wire.e = "move";
                    wire.dx = pad_dx;
                    wire.dy = pad_dy;
                    //! 十字（物理パッドもバーチャルパッドも）は**画面基準**（設計書 罠 1）。
                    turn_screen_move(wire);
                    //! 斜め移動限定：視点回転のあとの向きが縦横 1 方向なら何もしない（経路も捨てない）。
                    if (diagonal_walk && is_orthogonal_step(wire.dx, wire.dy)) {
                        return;
                    }
                    click_path.cancel(); //!< 自分で歩き始めたら経路は捨てる
                    input.events.push_back(wire);
                }
            };
            int pad_dx = 0;
            int pad_dy = 0;
            /*
             * **一人称では歩調を一定にする**（2026-08-14 に気づいた「押しっぱなしの場合は
             * ターン毎に動くのではなく、ヌルヌルと継続して移動してほしい」）。
             * ふつうの割り当て（メニューのカーソルなど）は「1 歩目 → 少し待つ → 連射」で
             * よいが、歩くときにその待ちが入ると 1 歩ごとに止まって見える。
             */
            const int walk_repeat_ms = fps_drives_movement() ? settings.fps_step_ms : 0;
            if (pad.poll_direction(static_cast<Uint32>(loop_started), focused, pad_dx, pad_dy, walk_repeat_ms)) {
                route_pad_direction(pad_dx, pad_dy);
            }
            //! バーチャルパッドの LS。焦点は見ない（指の出来事は焦点のある窓にしか来ない）。
            if (vpad.poll_direction(static_cast<Uint32>(loop_started), pad_dx, pad_dy)) {
                route_pad_direction(pad_dx, pad_dy);
            }

            /*
             * スクリプトからのボタン押し（`HD2D_PAD_PRESS=Back,Down,A,Right` ／
             * `HD2D_PAD_PRESS_AT=<フレーム>` ／ `HD2D_PAD_PRESS_STEP=<フレーム>`）。
             * **方向（`Up` `Down` `Left` `Right`）も書ける**——割り当ての変更モードは
             * 「頁へ入って・列を選んで・待ちにする」まで方向が要るので、押しだけでは届かない。
             *
             * **これが無いと、パッドまわりは実機のコントローラーが手元にある人にしか
             * 一度も試せない。**とくに「割り当ての無いボタンがマクロのトリガーになる」道は
             * 押してみるまで通ったか分からず、壊れても誰も気付かない。
             * コア側の `HENGBAND_SDL2_INJECT_KEYS`（`sdl_null_term.cpp`）と同じ、
             * 環境変数で入る調査用のオプションである。**通るのは実機と同じ `route_pad_press`**
             * ——別の道を作ると、試せているのは検査用の道だけになる。
             *
             * 1 回だけ発火する。既定のフレームは 1200（起動とセーブの読み込みが済むころ）。
             */
            {
                /*
                 * **列で送れる**（2026-08-14 に 1 押しから広げた）。`Back,Down,A` のように
                 * カンマで並べ、項目ごとに `LB+X` / `LB+RB+X` と修飾を書ける。
                 *
                 * 広げた理由: 1 押しでは**割り当ての変更モードまで運べない**（メニューを
                 * 開いて・頁へ入って・列を選んで・待ちにして、そこでやっと同時押しを
                 * 試せる）。「割り当てで同時押しが入らない」に気づいたを手元で追うのに、
                 * ここが 1 押しのままだと結局「実機で押してもらう」しか道が無かった。
                 *
                 * 間隔は `HD2D_PAD_PRESS_STEP`（既定 30 フレーム＝画面が落ち着くころ）。
                 */
                static std::vector<PadPress> pad_script;
                static bool pad_script_parsed = false;
                static std::size_t pad_script_at = 0;
                if (!pad_script_parsed) {
                    pad_script_parsed = true;
                    const char *const press_env = std::getenv("HD2D_PAD_PRESS");
                    if ((press_env != nullptr) && (press_env[0] != '\0')) {
                        int fallback_mods = 0;
                        if (const char *const mods_env = std::getenv("HD2D_PAD_MODS")) {
                            fallback_mods = std::atoi(mods_env);
                        }
                        const std::string spec = press_env;
                        std::size_t from = 0;
                        while (from <= spec.size()) {
                            const std::size_t comma = spec.find(',', from);
                            const std::string item = spec.substr(from,
                                (comma == std::string::npos) ? std::string::npos : (comma - from));
                            from = (comma == std::string::npos) ? (spec.size() + 1) : (comma + 1);
                            if (item.empty()) {
                                continue;
                            }
                            //! `LB+RB+X` … 最後が押すボタン、手前は修飾。
                            int mods = 0;
                            std::string name = item;
                            for (std::size_t plus = name.find('+'); plus != std::string::npos; plus = name.find('+')) {
                                const std::string head = name.substr(0, plus);
                                name = name.substr(plus + 1);
                                PadInput modifier = PadInput::Count;
                                if (parse_pad_input(head, modifier) && pad_input_is_modifier(modifier)) {
                                    mods |= pad_modifier_of(modifier);
                                } else {
                                    std::fprintf(stderr, "[hd2d] HD2D_PAD_PRESS: 「%s」は修飾ではありません\n",
                                        head.c_str());
                                }
                            }
                            //! 方向は押しではない（`route_pad_direction` へ回す）。印は `PadInput::Count`。
                            const int dir_x = (name == "Left") ? -1 : ((name == "Right") ? 1 : 0);
                            const int dir_y = (name == "Up") ? -1 : ((name == "Down") ? 1 : 0);
                            if ((dir_x != 0) || (dir_y != 0)) {
                                pad_script.push_back(PadPress{ PadInput::Count, (dir_y * 16) + dir_x });
                                continue;
                            }
                            PadInput synthetic = PadInput::Count;
                            if (!parse_pad_input(name, synthetic)) {
                                std::fprintf(stderr, "[hd2d] HD2D_PAD_PRESS: 「%s」は知らないボタンです\n",
                                    name.c_str());
                                continue;
                            }
                            pad_script.push_back(PadPress{ synthetic, (mods != 0) ? mods : fallback_mods });
                        }
                    }
                }
                if (pad_script_at < pad_script.size()) {
                    int press_at = 1200;
                    if (const char *const at_env = std::getenv("HD2D_PAD_PRESS_AT")) {
                        press_at = std::atoi(at_env);
                    }
                    int step = 30;
                    if (const char *const step_env = std::getenv("HD2D_PAD_PRESS_STEP")) {
                        step = std::max(1, std::atoi(step_env));
                    }
                    if (frame_index >= (press_at + (static_cast<int>(pad_script_at) * step))) {
                        const PadPress &press = pad_script[pad_script_at];
                        ++pad_script_at;
                        if (press.input == PadInput::Count) {
                            const int dir_y = press.mods / 16;
                            const int dir_x = press.mods - (dir_y * 16);
                            std::fprintf(stderr, "[hd2d] HD2D_PAD_PRESS: 方向 (%d,%d)\n", dir_x, dir_y);
                            route_pad_direction(dir_x, dir_y);
                        } else {
                            std::fprintf(stderr, "[hd2d] HD2D_PAD_PRESS: %s%s を押します\n",
                            (press.mods != 0) ? pad_mods_tag(press.mods) : "",
                            pad_input_name(press.input));
                            route_pad_press(press);
                        }
                    }
                }
            }

            /*
             * **右スティック ＝ 視点**。向きを変えるだけなのでコアへは何も送らない
             * （＝ターンを消費しない）。メニューが出ている間は動かさない
             * （背後で景色が回ると、閉じたときにどこを向いているか分からない）。
             *
             * | | 見下ろし（本線） | 一人称 |
             * |---|---|---|
             * | 横 | — | 方位を回す |
             * | 縦 | **見下ろし角**（2026-08-11 に決めた） | 仰角・俯角 |
             */
            if (dcss_ui) {
                /*
                 * DCSS（段 5。D11・D12）: 右スティックは視点ではなく**マウスのカーソル**（原作の `cursor_motion`。円の死区 8000 から 2 乗の曲線で
                 * 1 秒あたり 60〜1400 画素）。カーソルは画面の中の疑似カーソルで、動かすたびに SDL のマウスの動きを積む（本物のマウスと同じ道を通る。
                 * ツールチップ・窓の強調・地図のマスの説明が出る）。本物のマウスが動いたら、そちらを優先して疑似カーソルを消す。
                 * 視点の操作（見下ろし角・回転）は原作が使っていない入力（同時押しの層）へ移した（`dcss/dcss_pad.cpp` の既定）。
                 */
                int mouse_x = 0;
                int mouse_y = 0;
                SDL_GetMouseState(&mouse_x, &mouse_y);
                dcss_ui->pad().observe_mouse(mouse_x, mouse_y);
                if (!feature_menu.is_open() && !vpad.editor_open()) {
                    int raw_x = 0;
                    int raw_y = 0;
                    pad.right_stick_raw(raw_x, raw_y);
                    raw_x = std::clamp(raw_x + static_cast<int>(vpad.right_stick_x() * 32767.f), -32768, 32767);
                    raw_y = std::clamp(raw_y + static_cast<int>(vpad.right_stick_y() * 32767.f), -32768, 32767);
                    int cursor_dx = 0;
                    int cursor_dy = 0;
                    if (dcss_ui->pad().cursor_motion(SDL_GetTicks(), focused, raw_x, raw_y, cursor_dx, cursor_dy)) {
                        dcss_ui->pad().move_cursor(cursor_dx, cursor_dy, screen_w, screen_h);
                        int cursor_x = 0;
                        int cursor_y = 0;
                        if (dcss_ui->pad().cursor_position(cursor_x, cursor_y)) {
                            SDL_Event motion{};
                            motion.type = SDL_MOUSEMOTION;
                            motion.motion.timestamp = SDL_GetTicks();
                            motion.motion.which = dcss::kPadMouseId;
                            motion.motion.x = cursor_x;
                            motion.motion.y = cursor_y;
                            motion.motion.xrel = cursor_dx;
                            motion.motion.yrel = cursor_dy;
                            if (std::getenv("HD2D_DCSS_PAD_TRACE") != nullptr) {
                                std::fprintf(stderr, "[dcss-pad] cursor %d,%d\n", cursor_x, cursor_y);
                            }
                            (void)SDL_PushEvent(&motion);
                        }
                    }
                }
            } else if (!feature_menu.is_open() && !vpad.editor_open() && frame_shows_map(frame)) {
                /*
                 * バーチャルパッドの RS も同じ扱いで**足す**（触れていない側は 0 なので、
                 * 足しても取り合いにならない。両方倒したときだけ丸める）。
                 */
                const float stick_x = std::clamp(pad.right_stick_x() + vpad.right_stick_x(), -1.f, 1.f);
                const float stick_y = std::clamp(pad.right_stick_y() + vpad.right_stick_y(), -1.f, 1.f);
                if (first_person.active && vr_frame_ready) {
                    /*
                     * **一人称 VR では首をスティックで回さない**（§17）。頭がもう向きを
                     * 持っているので、`facing` を触ると HMD と喧嘩する。上下も同じ理由で触らない。
                     *
                     * 代わりに右スティックの左右で**世界そのものを 45° 回す**（スナップ振り向き）。
                     * 座って被る人が後ろを向けるようにするためで、これは酔いにくい作法として
                     * 定着しているやり方である。倒しっぱなしで回り続けないよう、
                     * 中立へ戻るまで次を受け付けない。
                     */
                    constexpr float kSnapEnter = 0.7f;
                    constexpr float kSnapLeave = 0.35f;
                    if (vr_snap_armed && (std::fabs(stick_x) > kSnapEnter)) {
                        vr_snap_armed = false;
                        /*
                         * 世界を回すので、頭から見た向き（`facing`）は次のフレームで勝手に付いてくる。
                         *
                         * **符号は「盤を回す向き」で考える**（2026-08-14 に気づいた
                         * 「左右の視点移動が逆方向」。ここが逆だった）。ステージは右手系 y 上で、
                         * `Ry(+)` は目の前（-z）の点を -x（左）へ送る。**世界が左へ動く ＝
                         * 自分が右を向いた見え方**なので、右へ倒したときに足すのは **正** である。
                         */
                        const float delta = (stick_x > 0.f) ? (kFpsYawStepDeg * 0.0174533f)
                                                            : (-kFpsYawStepDeg * 0.0174533f);
                        vr_board.yaw += delta;
                        /*
                         * **板は基準の頭ごと回す**（§20）。板の置き場所は毎フレーム
                         * `vr_anchor` から組み直されるので、ここを回せば全部の板が
                         * 自分のまわりを一緒に回る。板だけその場で回すと、世界が回った先で
                         * 板が横や後ろに残る。回す中心は盤の置き場所（一人称では足元）。
                         */
                        xr::rotate_anchor(vr_anchor, vr_board.origin, delta);
                    } else if (!vr_snap_armed && (std::fabs(stick_x) < kSnapLeave)) {
                        vr_snap_armed = true;
                    }
                } else if (first_person.active) {
                    first_person.turn_analog(stick_x, frame_seconds);
                    if (settings.fps_vertical_look && (stick_y != 0.f)) {
                        /*
                         * 一人称の上下。**マウスと同じ向き**（スティックを手前 ＝ 下へ倒すと
                         * 下を向く）。`look()` は画素で受けるので、秒あたりの度から画素へ直す。
                         */
                        const float deg = stick_y * kFpsPadPitchDegPerSec * frame_seconds;
                        first_person.look(0, static_cast<int>(std::lround(deg / kFpsMousePitchDegPerPx)));
                    }
                } else {
                    /*
                     * **見下ろし。横 ＝ 90° の視点回転**（-5。
                     * ここは回転を入れるまで空いていた）。一人称 VR のスナップ振り向きと
                     * 同じ**アーム式**にする——倒しっぱなしで回り続けると、
                     * どちらを向いているか分からなくなる。
                     */
                    constexpr float kSnapEnter = 0.7f;
                    constexpr float kSnapLeave = 0.35f;
                    if (turn_snap_armed && (std::fabs(stick_x) > kSnapEnter)) {
                        turn_snap_armed = false;
                        //! 右へ倒したら**右回り**（画面が右へ流れる向き）。
                        turn_view((stick_x > 0.f) ? 1 : -1);
                    } else if (!turn_snap_armed && (std::fabs(stick_x) < kSnapLeave)) {
                        turn_snap_armed = true;
                    }
                    if (stick_y != 0.f) {
                        /*
                         * 見下ろしの「見下ろし角の調整」（2026-08-11 に決めた）。
                         * **設定へ書く**（P8）。カメラへ直に書くと機能メニューの数字とずれ、
                         * cfg にも残らない。丸めの幅はホイールと同じ 10〜85 度。
                         *
                         * **手前へ倒すと角が浅くなる**（地面を横から見る方へ）。スティックを
                         * 引くと視線が起き上がる、という向きに合わせてある。
                         */
                        settings.camera_pitch_deg = std::clamp(
                            settings.camera_pitch_deg - (stick_y * kCameraPitchDegPerSec * frame_seconds), 10.f, 85.f);
                    }
                }
            }
        }

        /*
         * 「ボタン配置」の編集画面を開く（2026-08-12 に決めた）。キーからもパッドの
         * 決定からも要求が立つので、**両方を処理した後の 1 か所**で開く。メニューは
         * 開いたままにする——編集画面が上へ被さり、「戻る」でこの画面へ戻ってくる。
         */
        if (feature_menu.take_vpad_edit_request()) {
            vpad.open_editor();
        }

        /*
         * コマンドメニューで選ばれた命令を実行する（2026-08-22 に決めた）。
         * **キーからもパッドの決定からも要求が立つので、両方を処理した後の 1 か所**で拾う
         * （すぐ上の「ボタン配置」と同じ形）。
         *
         * **流してから閉じる。** 先に閉じると、閉じた拍子の描き直しに紛れて
         * 「押したのに何も起きなかった」ように見える。板の側は選ばれたことしか知らない。
         */
        int menu_command_id = 0;
        /*
         * タブの切り替えの要求を片づける。**フレームを取り込んだ直後にもう一度呼ぶ**
         * （下の (c)）——Rogue の設定一覧を閉じた返事が届いたフレームで次のタブを開かないと、
         * 窓の無いマップの画が 1 フレーム挟まっていた。
         * 次のタブが持ち物一覧なら、機能メニューをそのタブで 1 度も描かずに一覧を開く
         * （描くと「持ち物を読み込み中…」の空の窓が挟まる）。
         */
        const auto service_tab_requests = [&](presentation::InputEventsMessage &out) {
            if(settings.modern_ui&&!modern_handoff&&hud_state.modern.leaving_parent_tab()&&hud_state.modern.active()) {
                modern_handoff=std::make_unique<ModernStoreUi>(hud_state.modern);modern_handoff_since=SDL_GetTicks();
            }
            const auto open_inventory_tab = [&]() {
                hud_state.modern.open_inventory(out,feature_menu.command_tab_labels(),feature_menu.command_tab_index());
                if(settings.modern_ui&&!modern_handoff&&feature_menu.is_open()&&(menu_before_input_frame==frame_index)){menu_handoff=std::make_unique<FeatureMenu>(menu_before_input);menu_handoff_since=SDL_GetTicks();}
                feature_menu.close();
            };
            if(feature_menu.take_inventory_request())open_inventory_tab();
            if(const int inventory_tab=hud_state.modern.take_inventory_tab_request();inventory_tab>=0)
                feature_menu.open_command_tab(inventory_tab,settings);
            if(const int parent_tab=hud_state.modern.take_parent_tab_request();parent_tab>=0) {
                feature_menu.open_command_tab(parent_tab,settings);
                if(feature_menu.take_inventory_request())open_inventory_tab();
            }
        };
        service_tab_requests(input);
        if (feature_menu.take_pending_command_request(menu_command_id)) {
            const auto parent_tabs=feature_menu.command_tab_labels();
            const int parent_tab=feature_menu.command_tab_index();
            const bool rogue_settings_tab=parent_tab>=0&&parent_tab<int(parent_tabs.size())&&parent_tabs[parent_tab]=="設定";
            if (emit_core_command(menu_command_id, pad_commands, input, true)) {
                if(rogue_settings_tab)hud_state.modern.set_parent_command_tabs(parent_tabs,parent_tab);
                if(rogue_settings_tab&&settings.modern_ui&&(menu_before_input_frame==frame_index)){menu_handoff=std::make_unique<FeatureMenu>(menu_before_input);menu_handoff_since=SDL_GetTicks();}
                feature_menu.close();
            }
        }

        /*
         * ---- VR の入切（2026-08-14 に決めた「ゲーム内から VR モードを起動できないか？」）----
         *
         * `--vr` を打たずに、機能メニュー ＞ VR ＞「VR で遊ぶ」から始められる。
         * ここでやることは起動時の `--vr` の枝と**同じ順**である:
         * セッションを立てる → vsync を切る → Touch を繋ぐ → 据え直しの合図。
         *
         * **GL のコンテキストが current な所で呼ぶこと**（`Session::init` の約束）。
         * この主ループの中はいつでも current なので、ここで足りる。
         */
        if (feature_menu.take_vr_toggle_request()) {
            if (xr.valid()) {
                xr_input.shutdown();
                xr.shutdown();
                //! 待ちを戻す（VR 中は `xrWaitFrame` が歩調を握っていた。罠 5）。
                SDL_GL_SetSwapInterval(1);
                vr_board_placed = false;
                std::fprintf(stderr, "[hd2d] XR やめました（平らな画面へ戻ります）\n");
            } else if (options.vr_fake) {
                //! 疑似 HMD と実物は排他（§10）。同時に立てると両方が目を描く。
                show_message("疑似 HMD（--vr-fake）で動いています。\nVR で遊ぶには --vr-fake を外して起動してください。");
            } else {
                std::string xr_err;
                //! 倍率は cfg の値（§5）。**`init` より前**でなければ swapchain に効かない。
                xr.set_render_scale(settings.vr_render_scale);
                if (xr.init(xr_err)) {
                    SDL_GL_SetSwapInterval(0); //!< 歩調は `xrWaitFrame` が握る（罠 5）
                    std::string input_err;
                    if (!xr_input.init(xr.native_instance(), xr.native_session(), input_err)) {
                        std::fprintf(stderr, "[hd2d] %s（Touch は効きません）\n", input_err.c_str());
                    }
                    //! 盤・卓・板は次のフレームで据える（頭の姿勢はまだ引けていない）。
                    vr_board_placed = false;
                    std::fprintf(stderr, "[hd2d] XR を始めました（%s）\n", xr.runtime_name().c_str());
                } else {
                    /*
                     * **立たなくても遊びは続く。**理由を見せて平らな画面のままにする
                     * （HMD を繋いでいない・ランタイムが無い、のどちらかがほとんど）。
                     */
                    show_message("VR を始められませんでした:\n" + xr_err);
                }
            }
        }

        /*
         * クリック移動を 1 歩だけ進める（P8）。**キーの後**に積む（到着順を崩さない）。
         * 直前の 1 歩が実座標で確認できるまで次は積まない（`click_path.h` の約束 2）。
         */
        if (!feature_menu.is_open() && !floor_cutin.blocks_input()) {
            click_path.advance(frame, input);
        } else {
            click_path.cancel();
        }
        /*
         * **カットインの間はコアへ 1 つも流さない**（2026-08-11 に決めた:「決定キー待ちの
         * 間はコアへ入力を流さないこと」）。幕の向こうは見えていないので、ここで通した
         * キーは「見えていない画面への誤コマンド」になる（機能メニューと同じ扱い）。
         * **捨てるのはここ 1 か所**——キーごとに間引くと必ず漏れる経路ができる。
         */
        if (floor_cutin.blocks_input()) {
            input.events.clear();
        }
        if (!input.events.empty()) {
            (void)link.send_input_events(input);
        }

        /*
         * (b) カメラをプレイヤへ追従させる（P2 ③）。**丸めない**（§14-3）。
         * `ox/oy` を整数へ丸めるとフロア端でカメラが張り付き、プレイヤが画面内を泳ぐ。
         * ここは注視点そのものを実数で持ち、コア側も `camera_follow_player = true` で
         * 丸めをやめさせてある。
         */
        /*
         * (b0) 設定を画面へ反映する（P8）。**1 か所でまとめて当てる。**
         * 起動引数・ホイール・機能メニューの 3 つが同じ `settings` を触るので、
         * ここを通さない経路を作ると「メニューの数字と実際の絵が違う」状態になる。
         */
        if (settings.windowed != window_is_windowed) {
            window_is_windowed = settings.windowed;
            (void)SDL_SetWindowFullscreen(window.window,
                window_is_windowed ? 0 : static_cast<Uint32>(SDL_WINDOW_FULLSCREEN_DESKTOP));
            SDL_GetWindowSize(window.window, &screen_w, &screen_h);
            glViewport(0, 0, screen_w, screen_h);
        }
        hud_state.subs_open = settings.subs_open;
        {
            /*
             * **割り当てを変えたらここも変わる**（決め打ちの「F10」を書かない）。
             * 綴りは割り当て表から引く（`ui/key_binds.h`）。
             */
            const auto ui_key = [&settings](int action) {
                const KeyBindEntry entry = settings.key_binds.find(action);
                const std::string name = key_display_name(entry.keycode, entry.mods);
                return name.empty() ? std::string(i18n::tr("hd2d.app.hd2d-app.key-hint-unbound")) : name;
            };
            //! 案内の文言も言語で変える（ここだけ生のリテラルが残っていた）。
            if (first_person.active) {
                //! 一人称の間は操作が丸ごと変わる。出す案内も入れ替える。
                hud_state.ui_key_hint = std::string(i18n::tr("hd2d.app.hd2d-app.key-hint-first-person"))
                    + ui_key(kActionFpsToggle) + i18n::tr("hd2d.app.hd2d-app.key-hint-leave-first-person");
            } else {
                /*
                 * **新 UI では機能メニューとコマンドメニューは同じ 1 つの窓**
                 * （`FeatureMenu::open_features` は
                 * `modern_ui` のとき `open_commands()` へそのまま委ねる）。F10 を
                 * 別の入口として案内すると、遊ぶ人には別物に見えてしまう——ここは
                 * **開く綴り（F1）だけ**を出す。旧 UI（`modern_ui` 偽）では機能の
                 * 窓とコマンドの窓は今までどおり別物なので、この帯には元の
                 * 「機能メニュー」だけを出す（コマンドメニューの案内はここには無い）。
                 * どちらも新しい文言は足さず、既存の `i18n` の綴りを使い回す
                 * （`assets/lang/*.json` は別スレッドの持ち物のため触らない）。
                 */
                const std::string open_menu_hint = settings.modern_ui
                    ? (ui_key(kActionCommandMenu) + ":" + i18n::tr("hd2d.app.hd2d-app.command-menu") + "  ")
                    : (ui_key(kActionFeatureMenu) + i18n::tr("hd2d.app.hd2d-app.key-hint-feature-menu"));
                hud_state.ui_key_hint = open_menu_hint
                    + ui_key(kActionLayoutCycle) + i18n::tr("hd2d.app.hd2d-app.key-hint-layout")
                    + ui_key(kActionSubsToggle) + i18n::tr("hd2d.app.hd2d-app.key-hint-panels")
                    + ui_key(kActionFpsToggle) + i18n::tr("hd2d.app.hd2d-app.key-hint-first-person-toggle");
            }
        }
        /*
         * コントローラーの全ボタンの割り当て（2026-08-11 に決めた）。
         * **毎フレーム組み直す。**割り当てはメニューで変わり、コマンドの一覧は
         * キー配列を切り替えるとコアから再送されるので、どちらも途中で動く。
         */
        /*
         * **修飾を押している間は同時押しの層を出す**（2026-08-14 に決めた の
         * 「マクロと表示させて」の続き）。同時押しは覚えていないと使えないので、
         * 押している間だけでも裏に何が居るか見えないと死ライブラリする。
         *
         * **物理パッドとバーチャルパッドの論理和を 1 つだけ作り、下の帯と板の両方へ渡す**
         * （2026-08-14 に決めた「下部のキー割り当てとバーチャルコントローラーの表示を
         * 同時押しに割り当てられた物に切り替えて」）。板の側が `vpad.chord_mask()` だけを
         * 見ていたので、**物理パッドの LB を押さえても板の字だけ変わらなかった**。
         * 押している場所が違うだけで見える中身が変わってはいけないので、出どころは 1 つにする。
         */
        const int chord_mask = pad.chord_mask() | vpad.chord_mask();
        hud_state.controller_binds = build_pad_bind_items(settings.pad, pad_commands, pad_macros, chord_mask, settings.modern_ui);
        if (dcss_ui) {
            /*
             * DCSS（D19・段 5）: 並べるのは原作の割り当て（`dcss/dcss_pad.h`）。いまの待ち（コマンド・窓・狙い・その他）で替える。
             * 右側のキーの案内も、HD2D の既定のキー（F1 のコマンドメニューなど）ではなく、原作が使っていないキーに置いた機能だけにする。
             */
            hud_state.controller_binds = dcss_ui->pad().guide_items(dcss_ui->scene(), chord_mask, dcss_ui->pad_screen().active());
            const bool ja = (i18n::current() != "en");
            const KeyBindEntry menu_key = settings.key_binds.find(kActionFeatureMenu);
            const KeyBindEntry turn_left = settings.key_binds.find(kActionTurnLeft);
            const KeyBindEntry turn_right = settings.key_binds.find(kActionTurnRight);
            std::string hint;
            if (menu_key.keycode != 0) {
                hint += key_display_name(menu_key.keycode, menu_key.mods) + (ja ? ":機能メニュー  " : ":Feature menu  ");
            }
            if ((turn_left.keycode != 0) && (turn_right.keycode != 0)) {
                hint += key_display_name(turn_left.keycode, turn_left.mods) + "/" + key_display_name(turn_right.keycode, turn_right.mods)
                    + (ja ? ":視点回転" : ":Turn view");
            }
#if defined(__ANDROID__)
            // Android・Quest（指で遊ぶ）: キーの案内は意味がないので、原作の Android 版のタッチの決まりを並べる（「段 7 のタッチ」）。
            hint = ja ? "タップ:移動・攻撃  長押し:調べる  2本指:スクロール  戻る:取消" : "Tap:Move/Attack  Hold:Examine  2 fingers:Scroll  Back:Cancel";
#endif
            hud_state.ui_key_hint = hint;
        }
        //! 上下の視点移動の入／切は設定が持つ（`FpsMode` は毎フレーム受け取るだけ）。
        first_person.vertical_look = settings.fps_vertical_look;
        //! ミニマップの自分を向きのある三角で描くため（一人称のときだけ。決めたこと）。
        hud_state.first_person = first_person.active;
        hud_state.first_person_facing = first_person.yaw;
        /*
         * 北の印。**一人称のときは出さない**
         * ——あちらはミニマップの三角が既に向きを持っており、印が 2 つになると
         * どちらが自分の向きか分からなくなる。
         */
        hud_state.camera_turn = first_person.active ? 0 : camera_turn;
        /*
         * ミニマップの見せ方（2026-08-19 に決めた）。**描く側は `Hd2dSettings` を
         * 持っていない**ので、毎フレームここへ写す（`first_person_facing` と同じ流儀）。
         */
        hud_state.minimap_cell_px = settings.minimap_cell_px;
        hud_state.minimap_opacity_pct = settings.minimap_opacity_pct;
        hud_state.minimap_relative = settings.minimap_relative;
        hud_state.minimap_corner = settings.minimap_corner;
        /*
         * HP/MP の玉の動き。設定と実時計（ms）を毎フレーム写す。
         * 時計は `combat_fx` と同じ `clock::ticks()`（フレームを取りこぼしても速さが変わらない）。
         */
        hud_state.orb_params.wave_enabled = settings.orb_wave;
        hud_state.orb_params.wave_size = static_cast<float>(settings.orb_wave_size_pct) * 0.01f;
        hud_state.orb_params.wave_speed = static_cast<float>(settings.orb_wave_speed_pct) * 0.01f;
        hud_state.orb_params.rise_speed = static_cast<float>(settings.orb_rise_speed_pct) * 0.01f;
        hud_state.orb_params.fall_speed = static_cast<float>(settings.orb_fall_speed_pct) * 0.01f;
        hud_state.orb_now_ms = static_cast<std::uint32_t>(clock::ticks());
        {
            /*
             * マウスを掴む／放す。**一人称で地図が見えている間だけ**掴む。
             * 掴んでいるとカーソルが消えて座標も来ないので、メニュー（機能メニュー・店・建物）
             * が出ている間に掴んだままだと、選ぶことも閉じることもできなくなる。
             * 窓の焦点が外れたときは SDL が自分で放す。
             */
            const bool want_relative = first_person.active && !feature_menu.is_open() && frame_shows_map(frame);
            if (want_relative != mouse_is_relative) {
                //! 失敗したら**掴んでいないことにする**（掴めていないのに掴んだ気でいるほうが困る）。
                mouse_is_relative = (SDL_SetRelativeMouseMode(want_relative ? SDL_TRUE : SDL_FALSE) == 0)
                    && want_relative;
            }
        }
        //! 割り当ての変更モードの時間切れ（15 秒）。**毎フレーム必ず時計を渡す。**
        feature_menu.update(static_cast<Uint32>(clock::ticks64()));
        camera.pitch = settings.camera_pitch_deg * 0.0174533f;
        camera.fov_x = settings.camera_fov_deg * 0.0174533f;
#if defined(__ANDROID__)
        /*
         * 切り欠きの余白を採り直す（`platform/android/android_safe_area.h`）。
         * **毎フレームは要らないが、1 度きりでも足りない**——起動直後は窓がまだ画面に
         * 付いておらず採れないし、回転すれば寄る辺が変わる。半秒に 1 度で追随する。
         */
        if ((frame_index % 30) == 0) {
            int l = 0;
            int t = 0;
            int r = 0;
            int b = 0;
            if (platform_android::get_safe_insets(l, t, r, b)
                && ((l != safe_inset[0]) || (t != safe_inset[1]) || (r != safe_inset[2]) || (b != safe_inset[3]))) {
                safe_inset[0] = l;
                safe_inset[1] = t;
                safe_inset[2] = r;
                safe_inset[3] = b;
                /*
                 * **変わったときだけ**出す（起動時と回転時の 2 回程度）。端末ごとに
                 * 切り欠きの寸法が違い、「画面の端が欠けている」の相談はこの数字を
                 * 見ないと切り分けられない。
                 */
                std::fprintf(stderr, "[hd2d] 切り欠きを避ける余白: 左%d 上%d 右%d 下%d（窓 %dx%d）\n",
                    l, t, r, b, screen_w, screen_h);
            }
        }
        /*
         * ---- ソフトキーボード（AH-02。2026-08-13 に決めた）--------------------
         *
         * **出し入れは「コアが自由文字入力に入ったか」だけで決める。**画面の見た目から
         * 推測しない（`GameFrame::text_input_active` はコアの `text_input_state_hook`
         * がそのまま運んできたもので、`askfor` と自前の編集ループ 2 つ＝生い立ち・
         * 自動拾いを漏れなく覆っている。`src/core/asking-player.h`）。
         *
         * **必ず変わり目でだけ呼ぶ。**「入力中なら毎フレーム出す」と書くと戻るキーで
         * 閉じられなくなる（`ime_dismissed` の注記）。
         */
        /*
         * **数量の欄（`frame.numeric`）では出さない**。数量は新しい UI でも従来の UI でも
         * 十字キーの上下（従来の UI は左右で桁も）で変えられるので、キーボードは要らない。出すと画面の下半分を覆い、
         * 使える高さが縮んで Term の高さまで変わっていた（コアの側は問いの最中に高さを変えないよう直した。141ee3efdf ほか）。
         */
        const bool want_ime = wants_soft_keyboard(frame) || (dcss_ui && dcss_ui->wants_soft_keyboard()); //!< `input_controller.h`。DCSS は名前の問いも
        if (want_ime != ime_prompt_open) {
            ime_prompt_open = want_ime;
            ime_dismissed = false;
            if (ime_prompt_open) {
                SDL_StartTextInput();
            } else {
                SDL_StopTextInput();
            }
        } else if (ime_prompt_open && !ime_dismissed && (SDL_IsTextInputActive() == SDL_FALSE)) {
            //! 戻るキーで閉じられた（SDL が `SDL_StopTextInput` まで済ませている）。
            ime_dismissed = true;
        }
        /*
         * 覆われている高さ。**閉じる途中も追う**ので、開いている間と、まだ余白が
         * 残っている間は短い間隔で読む（キーボードはせり上がる／下がる）。
         * それ以外は切り欠きと同じ半秒に 1 度でよい。
         */
        const bool ime_moving = ime_prompt_open || (ime_inset_bottom > 0);
        if ((frame_index % (ime_moving ? 6 : 30)) == 0) {
            int ime_b = 0;
            if (platform_android::get_ime_inset(ime_b) && (ime_b != ime_inset_bottom)) {
                ime_inset_bottom = ime_b;
            }
        }
#endif
        /*
         * **毎フレーム向きを見て作りを選び直す。**回転は窓の大きさが変わるだけなので、
         * `layout_for()` を通しておけば回した瞬間にそれぞれの作りへ入れ替わる
         * （2026-08-12 に決めた）。切り替えの合図を別に作らないこと——
         * 合図を作ると「合図を出し忘れた道」が必ずできる。
         */
        feature_menu.set_orientation(screen_is_portrait());
        /*
         * **VR の節は VR のときだけ並べる**（2026-08-14 に決めた「VR モードの設定
         * メニューが欲しい」）。毎フレーム渡すのは、VR が立たなくてフラットへ落ちた回に
         * 節が残らないようにするため（`vr_frame_ready` は立っていなければ偽）。
         */
        feature_menu.set_vr_available(xr.valid());
        /*
         * **状態列の桁数はコアの申告で決める**（`GameFrame::status_col_cols`）。
         * 変愚は 13・幻想蛮怒は 20 で、画面側に数を持たせない（必守制約 1）。
         * 申告が無いフレーム（古いコア）は 0 が入り、`set_status_col_cols()` が
         * 「申告なし」に戻すので従来どおり 13 桁で組まれる。
         */
        set_status_col_cols(frame.status_col_cols);
        /*
         * **状態列の左右**。
         * `自動` はコアの申告（`frame.status_col_side`。0 = 左・1 = 右）に従い、
         * `左 / 右` を選んだら人の選択が勝つ。ここで 0/1 に畳んでから渡す
         * （layout 側に `Auto` は持ち込まない）。**5 本すべてのコアで効く**。
         */
        set_status_col_side((settings.status_col_side == Hd2dSettings::StatusColSide::Auto)
                ? frame.status_col_side
                : ((settings.status_col_side == Hd2dSettings::StatusColSide::Right) ? 1 : 0));
        /*
         * **Term ミラーの小窓の高さもコアに合わせる。**行数はコアで違う
         * （変愚 24 ／ 幻想蛮怒・Sil-Q 27）。決め打ちの 24 で組むと下 3 行が切れ、
         * 店のコマンド行（`p) 商品を買う` ほか）が画面から消える。
         * 空のフレーム（地図を出している間）では覚えている値を保つ。
         */
        if (!frame.menu_term_lines.empty()) {
            set_term_mirror_rows(static_cast<int>(frame.menu_term_lines.size()));
        }
        layout = UiLayout::compute(settings.layout_for(screen_is_portrait()), usable_area().w, usable_area().h,
            int(text.cell_w()*(settings.modern_ui?menu_font_scale(settings.modern_font_level):1.f)+.5f),
        int(text.cell_h()*(settings.modern_ui?menu_font_scale(settings.modern_font_level):1.f)+.5f), hud_state.subs_open,
            settings.sub_split, usable_area().x, usable_area().y,
            settings.vpad.show,
            //! VR ではミニマップを右下へ（2026-08-14 に決めた。板は見上げる位置にある）。
            vr_frame_ready,
            settings.minimap_corner, settings.minimap_size_pct);
        if (!vr_frame_ready) {
            apply_dcss_layout(layout);
        }
        if(frame.targeting.preview_only&&hud_state.modern.active()&&!feature_menu.is_open()&&!vr_frame_ready)
            layout_effect_preview(layout,menu_font_scale(settings.modern_font_level));
        /*
         * ---- 入力欄の桁を SDL へ渡す（`SDL_SetTextInputRect`）--------------------
         *
         * Windows ではここが**変換中の文字と候補一覧が出る場所**になる
         * （`SDL_HINT_IME_SHOW_UI` を立ててあるので、素の IME が自分で描く）。
         * Android では SDL の見えない編集ビューの置き場所になる。
         *
         * 桁は**コアが持っているカーソル**から採る（記憶 `hengband-core-owns-the-cursor`）。
         * `prompt_bar` を使ってはならない——文字入力の場面（名前・生い立ち・エディタ）は
         * どれも全画面のターミナルで、入力行はあそこに無い（タップの当たりで 1 度踏んだ）。
         * 割り付けの式は描く側（`game_hud.cpp` の `draw_term_mirror`）と**同じもの**を使う。
         *
         * カーソルが出ていない場面（遊んでいる最中の `[y/n]` など）は `prompt_bar` へ落とす。
         */
        if (!frame.text_input_active) {
            //! 文字入力の場面を抜けた。未確定の表示を残さない（次の場面に残ると幽霊になる）。
            ime_edit_text.clear();
        }
        if (frame.text_input_active) {
            const bool term_full = frame.title_screen || frame.pre_game_menu;
            const float menu_scale=settings.modern_ui?menu_font_scale(settings.modern_font_level):1.f;
            auto menu_layout=menu_logical_layout(layout,menu_scale);
            menu_layout.cell_w=text.cell_w();menu_layout.cell_h=text.cell_h();
            const RectPx term_area = menu_text_area(menu_layout,term_full,settings.window_theme());
            SDL_Rect want{ 0, 0, 0, 0 };
            if (!term_area.empty() && (frame.menu_term_curs_row >= 0) && (frame.menu_term_curs_col >= 0)) {
                const RectPx body = panel_body(term_area,menu_layout.cell_w);
                const int first=settings.modern_ui?hud_state.native_scroll_row:0,offset=settings.modern_ui?hud_state.native_scroll_col:0;
                const auto caret=menu_scaled_rect({body.x+(frame.menu_term_curs_col-offset)*menu_layout.cell_w,
                    body.y+(frame.menu_term_curs_row-first)*menu_layout.cell_h,menu_layout.cell_w,menu_layout.cell_h},menu_scale);
                want={caret.x,caret.y,caret.w,caret.h};
            } else if (!layout.prompt_bar.empty()) {
                want = SDL_Rect{ layout.prompt_bar.x, layout.prompt_bar.y,
                    layout.prompt_bar.w, layout.prompt_bar.h };
            }
            //! **変わったときだけ**渡す（毎フレーム渡すと OS への投函が積み上がる）。
            if(hud_state.modern.active()&&hud_state.modern.text_mode()) {
                const auto rect=hud_state.modern.text_edit_rect(layout);
                want={rect.x,rect.y,rect.w,int(text.cell_h()*menu_scale)};
            }
            if ((want.w > 0)
                && ((want.x != ime_text_rect.x) || (want.y != ime_text_rect.y)
                    || (want.w != ime_text_rect.w) || (want.h != ime_text_rect.h))) {
                ime_text_rect = want;
                SDL_SetTextInputRect(&ime_text_rect);
            }
        }
        /*
         * 階が変わったときのカットイン演出（P10。`ui/floor_cutin.h`）。
         * **割り付けが決まった後**に進める（字の大きさを地図の縦幅から採るので）。
         */
        floor_cutin.update(frame, frame_seconds, layout, frame_shows_map(frame));
        /*
         * 環境音。**場面はもうフレームで届いている**ので、
         * コアへは何も頼まずにベッドを選べる。`Ambience` 以外のときは名前を空にして止める
         * ——`ui_state` 側でコアの曲も止めてあるので、鳴る音は常にどちらか一方だけになる。
         */
        {
            const bool want_ambience = (settings.bgm_mode == Hd2dSettings::BgmMode::Ambience);
            std::string bed;
            std::vector<audio::AmbienceLayerGain> layers;
            if (want_ambience) {
                /*
                 * 周囲の内訳。
                 * **数えていないコアでは `valid()` が偽**になり、層を使わずベッド 1 本に落ちる。
                 */
                audio::AmbienceSurroundings sur;
                sur.grass = frame.surroundings.grass;
                sur.tree = frame.surroundings.tree;
                sur.dirt = frame.surroundings.dirt;
                sur.swamp = frame.surroundings.swamp;
                sur.water = frame.surroundings.water;
                sur.deep_water = frame.surroundings.deep_water;
                sur.lava = frame.surroundings.lava;
                sur.rock = frame.surroundings.rock;
                sur.glass = frame.surroundings.glass;
                sur.wall = frame.surroundings.wall;
                sur.radius = frame.surroundings.radius;
                sur.counted = frame.surroundings.counted;

                const auto band = audio::time_band(frame.lighting.day_minute);
                audio::AmbienceScene scene;
                scene.floor_kind = frame.floor.kind;
                scene.dungeon_id = frame.floor.dungeon_id;
                scene.depth = frame.floor.dun_level;
                scene.town_id = frame.floor.town_id;
                scene.wild = frame.floor.wild_mode;
                scene.day_minute = frame.lighting.day_minute;
                scene.ground = static_cast<int>(audio::dominant_ground(sur));
                scene.time_band = static_cast<int>(band);
                bed = ambience_table.bed_for(scene);
                //! **空の下だけ**生き物の層を乗せる（洞窟で鳥が鳴くと台無しになる）。
                const bool outdoors = (frame.floor.kind == static_cast<int>(FloorKind::Surface));
                layers = audio::decide_layers(sur, band, outdoors);
            }
            /*
             * **`play_bed` を直に呼ばない。**1 歩ごとに答えが変わればベッドも 1 歩ごとに
             * 入れ替わり、どれも鳴り切らないまま次へ行く＝「寸断」になる。
             * `AmbienceDirector` が落ち着き待ちと入れ替え中の抑止を持つ（§8.4）。
             */
            ambience_director.update(frame_seconds, bed, layers, audio_engine, "assets/audio/bed");
            //! 段（0〜10）→ 音の倍率。**0 は無音**（設定の側の約束と揃える）。
            audio_engine.set_bed_gain(static_cast<float>(settings.music_volume)
                / static_cast<float>(Hd2dSettings::kVolumeMax));
            audio_engine.update(frame_seconds);
        }
        /*
         * 効果音。**コアが言ってきた出来事を鳴らす。**
         * 向きは「聞こえてくるマス」＝回折の曲がり角（無ければ実際のマス）から、
         * 音量は「道のり」＝壁を回り込んだ歩数（無ければマスの差）から作る。
         * **直線距離で減衰させない**——壁 1 枚隔てた隣の敵は、通路を回った分だけ遠い。
         */
        if (!frame.sounds.empty()) {
            audio_engine.set_sfx_gain(settings.sound_enabled
                    ? (static_cast<float>(settings.sound_volume) / static_cast<float>(Hd2dSettings::kVolumeMax))
                    : 0.f);
            //! 聞き手の向きは**水平だけ**採る（見下ろしでは前向きがほぼ真下を向くので、外積が潰れる）。
            const Vec3 listen_dir = camera.forward();
            float fx = listen_dir.x;
            float fy = listen_dir.y;
            const float flen = std::sqrt((fx * fx) + (fy * fy));
            if (flen < 0.001f) {
                fx = 0.f;
                fy = -1.f; //!< 真下を向いている（一人称の見上げ・見下ろし）。北向きとみなす
            } else {
                fx /= flen;
                fy /= flen;
            }
            for (const SoundEvent &sound : frame.sounds) {
                const std::string file = sfx_catalog.path_for(sound.name, sfx_pick++);
                if (file.empty()) {
                    continue; //!< 表に無い音。**鳴らさないだけ**
                }
                //! 向きと歩数の決め方は `place_sound()` 1 か所に閉じてある（検査もそこを突く）。
                const audio::SoundPlacement placed = audio::place_sound(sound,
                    static_cast<float>(frame.player_gx) + 0.5f, static_cast<float>(frame.player_gy) + 0.5f);
                const audio::ListenerOffset at
                    = audio::to_listener_space(placed.dx, placed.dy, 0.f, fx, fy, 0.f, 0.f, 0.f, 1.f);
                const float gain
                    = (static_cast<float>(sound.gain) / 255.f) * audio::distance_gain(placed.steps);
                (void)audio_engine.play_sfx(file, at, gain);
            }
            /*
             * **鳴らしたら消す。**`frame` は次のフレームが届くまで居座る（v1 §6.4「無ければ
             * 最後のもので描き続ける」）ので、消さないと**毎ループ同じ音を鳴らし直す**。
             * 60fps で撃ち直された音は頭の 16ms しか鳴らず、耳には潰れて届かない
             * （2026-08-21 に気づいた「効果音がたまにしかならなくなった」の片割れ）。
             * 出来事は一度きりの縁なので、消すのが正しい。
             */
            frame.sounds.clear();
        }
        camera.viewport_w = layout.scene.w;
        camera.viewport_h = layout.scene.h;
        camera.distance = distance_for_cell_px(camera, settings.camera_cell_px);
        if(!layout.preview_menu.empty()&&!first_person.active&&!frame.targeting.area.empty()) {
            int min_x=frame.targeting.source.x,max_x=min_x,min_y=frame.targeting.source.y,max_y=min_y;
            for(const auto &cell:frame.targeting.area){min_x=std::min(min_x,cell.point.x);max_x=std::max(max_x,cell.point.x);min_y=std::min(min_y,cell.point.y);max_y=std::max(max_y,cell.point.y);}
            const float span=std::hypot(float(max_x-min_x+1),float(max_y-min_y+1))+2.f;
            const float fitted=std::min(float(settings.camera_cell_px),float(std::min(layout.scene.w,layout.scene.h))*.75f/span);
            camera.distance=distance_for_cell_px(camera,std::max(1.f,fitted));
        }
        /*
         * **ふつうは @ を追う。コアが離れて見ているときだけ、そちらを見る**
         * （`GameFrame::camera_detached`。2026-08-26 に気づいた
         * 「動かない。左を入れると少し離れた場所のブロックが透過した」）。
         *
         * Sil-Q の `L`（地図を動かす）は **@ を動かさずに見る場所だけ動かす**。
         * カメラが @ を追ったままだと、**送られてくるマスだけが移って絵は動かず**、
         * 窓から外れたブロックが消える——透けて見えたのはそれである。
         *
         * **`cam` と @ の差では見分けない。** 階の端ではコアがふつうに丸めるので、
         * 差はいつでも出る。**コアが言うときだけ**切り替える。
         */
        camera.target = frame.camera_detached
            ? Vec3{ static_cast<float>(frame.cam_x) + 0.5f, static_cast<float>(frame.cam_y) + 0.5f, 0.f }
            : Vec3{ static_cast<float>(frame.player_gx) + 0.5f,
                  static_cast<float>(frame.player_gy) + 0.5f, 0.f };
        /*
         * 闘技場の観戦カメラ（設計書 §4.6）。中身は `app/run_parts.cpp`。
         * 当たると注視点・見下ろし角・距離を書き換える。
         */
        const ArenaWatch arena = apply_arena_watch(
            frame, force_arena_watch, first_person.active, layout, camera);
        const bool arena_watch = arena.active;
        const int arena_w = arena.width;
        const int arena_h = arena.height;
        if (arena_watch != arena_watch_before) {
            arena_watch_before = arena_watch;
            std::fprintf(stderr, "[hd2d] 観戦カメラ %s（場内 %dx%d / kind=%d / mm=%dx%d）\n",
                arena_watch ? "ON" : "OFF", arena_w, arena_h, frame.floor.kind,
                frame.minimap.width, frame.minimap.height);
        }
        /*
         * 被弾の揺れ（設計書 §8）。**注視点をずらす**のが一番安い（絵ぜんぶが一緒に動く）。
         * 酔う人がいるので設定で切れる。切ってもフラッシュは残るので、気づけなくはならない。
         */
        combat_fx.update(clock::ticks());
        /*
         * 揺れは**ここでは当てない**。この後に「なめらか移動」が注視点を丸ごと書き直すので、
         * ここで足すと消える（実際に消えていた。2026-08-12 に気づいた「揺れも出ない」）。
         * 可視窓（`ui_state`）を揺らさないためにも、当てるのは視錐台を出した後にする。
         */
        float shake_dx = 0.f;
        float shake_dz = 0.f;
        if (settings.damage_shake) {
            combat_fx.shake_offset(shake_dx, shake_dz);
        }
        /*
         * 攻撃の視覚効果の揺れ（轟音など）。**前のフレームの評価結果**
         * （`attack_fx_frame` は後ろで毎フレーム作り直す）を使うので、1 フレーム遅れるが揺れは短い。
         * 被弾の揺れと別に、`fx_screen_shake` で切れる。
         */
        if (settings.fx_screen_shake && (attack_fx_frame.shake > 0.f)) {
            const Vec3 fx_shake = fx_shake_offset(attack_fx_frame.shake, clock::ticks());
            shake_dx += fx_shake.x;
            shake_dz += fx_shake.y;
        }
        /*
         * 一人称（おまけ）。**設定を当てた後・可視窓を出す前**に上書きする。
         * `apply()` は `active` が偽なら**本線の値へ戻す**（`first_person` を下ろし、
         * 方位を北へ戻す）。ここで自前に `camera.first_person = false` を書くと、
         * 「戻す責任が 2 か所」になって片方だけ直された状態になる（`yaw` で実際になった）。
         */
        /*
         * **見下ろしの 90° 視点回転を実効値へ寄せる**。
         * 瞬間で入れ替えると、どちらへ回ったのか・いま北がどちらなのかが分からない。
         * 0.2 秒で回し切る速さにしてある（仮値。実物を見て詰める）。
         *
         * **`camera_turn_goal` へ寄せる**（巻き戻さない角）ので、右へ 2 回押せば右へ 180° 回る。
         * 差が小さくなったら値そのものを入れて止める——`turn` が 0 のとき
         * `camera.yaw` が厳密に 0 でないと、改修前と 1 ビット違う絵になる（必守制約 4）。
         */
        {
            constexpr float kTurnRadPerSec = 1.57079633f / 0.2f;
            const float diff = camera_turn_goal - camera_turn_yaw;
            const float step = kTurnRadPerSec * frame_seconds;
            if (std::fabs(diff) <= step) {
                camera_turn_yaw = camera_turn_goal;
            } else {
                camera_turn_yaw += (diff > 0.f) ? step : -step;
            }
        }
        /*
         * 一人称を抜けたときの戻り先は**回転した先**である（§3.5）。
         * `apply()` は `active` が偽ならここを `camera.yaw` へ入れる。
         */
        first_person.base_yaw = camera_turn_yaw;
        if(hud_state.targeting.active())first_person.turn_dir=0;
        first_person.advance(frame_seconds);
        if(first_person.active&&hud_state.targeting.active()) {
            first_person.turn_dir=0;
            hud_state.targeting.aim_camera(camera.target.x,camera.target.y,kFpsEyeHeight,frame_seconds,
                first_person.vertical_look,first_person.yaw,first_person.facing,first_person.pitch_deg);
        }
        first_person.apply(camera);
        /*
         * @note ここで `apply()` した `camera.yaw` は、VR の一人称では**この後で頭の向きに
         * 差し替わる**（(0') の `head_azimuth`）。順序がこうなっているのは、可視窓の導出
         * （すぐ下）が `camera` を読むのに対し、頭の姿勢は `xrWaitFrame` の後でしか
         * 引けないためである。1 フレームの遅れは可視窓の大きさにしか効かない
         * （窓は注視点対称なので、向きが 1 フレーム古くても欠けない）。
         */
        /*
         * 可視窓の打ち切り。見下ろし 5° では視錐台の上側の光線が地面と交わらないので、
         * 打ち切り距離がそのまま窓の大きさになる。本線の 60 マスを一人称でも使うと
         * 120 マス角を要求してしまう（コアのフロアは 198×66 なので行が足りない）。
         */
        /*
         * **2D アスキーのときは枠のマスの数で窓を決める**。
         * 3D の視錐台から導いた窓（`derive_view_window`）は 70 マス角にもなるので、
         * そのまま使うと枠に入らないぶんを毎フレーム捨てることになる。
         * 逆に狭すぎると枠の端が黒く残る。**枠に映るマスの数をそのまま要求する**のが正しい。
         *
         * VR では効かせない（あちらにメインパネルという場所が無く、世界そのものが視界である）。
         */
        const bool ascii_panel = (settings.main_panel == Hd2dSettings::MainPanel::Ascii) && !vr_frame_ready;
        /*
         * **アスキー地図のマスを決める。**設定が 0 なら UI の文字と同じマス、そうでなければ
         * 専用のアトラスを焼く。ここで決めたマスは**可視窓の要求と描画の両方**が使う
         * ——別々に決めると「要求したマスと描いたマスが違う」ので端が欠けるか余る。
         */
        const int requested_ascii_px=settings.modern_ui?int(kTextPx*menu_font_scale(settings.modern_font_level)+.5f):settings.ascii_panel_px;
        if(ascii_panel && requested_ascii_px==0 && ascii_text_px>0){ascii_text.shutdown();ascii_text_px=0;}
        if (ascii_panel && (requested_ascii_px > 0) && !ascii_text_failed
            && (ascii_text_px != requested_ascii_px)) {
            ascii_text.shutdown();
            std::string ascii_err;
            if (ascii_text.init(requested_ascii_px, ascii_err)) {
                ascii_text_px = requested_ascii_px;
                std::fprintf(stderr, "[hd2d] 2D アスキー地図の文字: %dpx（マス %d×%d）\n", ascii_text_px,
                    ascii_text.cell_w(), ascii_text.cell_h());
            } else {
                //! **UI の文字へ落とす。**地図が出ないより字が小さいほうがまし。
                ascii_text_failed = true;
                ascii_text_px = 0;
                std::fprintf(stderr, "[hd2d] 2D アスキー地図の文字を用意できません: %s（UI のマスで描きます）\n",
                    ascii_err.c_str());
            }
        }
        //! 実効の文字。専用が焼けていなければ UI の文字。
        TextOverlay &ascii_glyphs = (ascii_text_px > 0) ? ascii_text : text;
        if (ascii_panel && (ascii_glyphs.cell_w() > 0) && (ascii_glyphs.cell_h() > 0) && !layout.scene.empty()) {
            ViewWindow panel_window;
            panel_window.cols = std::max(1, layout.scene.w / ascii_glyphs.cell_w());
            panel_window.rows = std::max(1, layout.scene.h / ascii_glyphs.cell_h());
            view_window = panel_window;
            //! 機能メニューの値の欄に「枠に何マス入るか」を出す（`set_ascii_grid`）。
            feature_menu.set_ascii_grid(panel_window.cols, panel_window.rows);
        } else {
            view_window = derive_view_window(camera, kMaxPropHeight,
                first_person.active ? kFpsViewDistance : 60.f);
            /*
             * 闘技場の観戦（設計書 §4.6）。注視点はフロア中心だが、**コアの窓はプレイヤ中心・
             * 対称**（§14-2）のまま。プレイヤはフロアのどこかに居るのだから、**寸法の 2 倍**を
             * 要求すれば必ず全域を覆える。闘技場は小さいので、この倍取りは安い。
             */
            if (arena_watch) {
                /*
                 * プレイヤは場内に立っているので、**場内の矩形の 2 倍**を要求すれば
                 * 窓（プレイヤ中心・対称。§14-2）が必ず場内を覆う。フロア寸法の 2 倍だと
                 * 幻想蛮怒で 396×132 を要求してしまう（場内は 66×22 しかない）。
                 */
                view_window.cols = std::max(view_window.cols, arena_w * 2 + 2);
                view_window.rows = std::max(view_window.rows, arena_h * 2 + 2);
            }
        }

        // (b') 可視窓の変化 → `ui_state`（変化したときだけ。v1 §7）。
        ui_state = build_ui_state(view_window, layout, settings, audio_engine.is_open());
        /*
         * The core-facing mode is the user's requested mode, not the previous
         * frame's local Modern overlay state.  Folding hud_state.modern.enabled()
         * back into this value made Modern -> Original self-latching for at
         * least one protocol round trip (and longer while a modal stayed open),
         * leaving native targeting enabled after the visible mode had changed.
         * The local overlay may finish its current frame, but the core must see
         * the requested capability transition immediately.
         */
        ui_state.modern_ui=settings.modern_ui;
        ui_state.targeting_ui=settings.modern_ui;
        std::string encoded_ui_state = presentation::encode_ui_state(ui_state);
        if (encoded_ui_state != last_ui_state) {
            last_ui_state = std::move(encoded_ui_state);
            (void)link.send_ui_state(ui_state);
            /*
             * **中身を変えて送ったら、往復はやり直し**（上の註記）。
             * ほかの欄（可視窓など）だけが変わったときは印を落とさない
             * ——落とすと、機能メニューを触っていないのに毎フレーム待ちに入る。
             */
            for (int i = 0; i < kUiSubPanels; ++i) {
                if (sent_sub_kinds[i] != settings.sub_panel_kind[i]) {
                    for (int k = 0; k < kUiSubPanels; ++k) {
                        sent_sub_kinds[k] = settings.sub_panel_kind[k];
                    }
                    sub_kinds_synced = false;
                    break;
                }
            }
        }

        //! (c) 最新フレームを取り込む。中身は `app/run_parts.cpp`。
        //! 新しい窓の操作の返事（`pending`）が届いた回は、描画をもう 1 回待たずにすぐ次の操作を送る（調-08。原作のカーソルを十数手動かす窓が、
        //! 1 手あたり描画 2 回ぶんかかっていた）。
        const bool modern_was_pending = hud_state.modern.pending();
        take_new_frame(link, frame, frame_bytes, hud_state, combat_fx, attack_fx, settings,
            sub_kinds_synced, sub_panel_kind_choices, sent_sub_kinds, perf_freq, ms_decode);
        /*
         * 天候。`HD2D_FORCE_WEATHER` はコアが申告しているときだけ値を差し替える（検査用）。
         * なめらかな値を進める。**申告の無いコアは `weather_view` が初期値のまま動かない**（何も変わらない）。
         */
        if (force_weather && frame.weather.declared) {
            frame.weather = forced_weather_state;
        }
        update_weather_view(weather_view, frame.weather, frame_seconds, std::cos(camera.azimuth()), std::sin(camera.azimuth()));
        /*
         * 地図の右クリック・長押しが開く文脈メニューは、押した点の近くへ小さな窓で出す（AH-58）。窓にする
         * かどうかはコアが決める（`frame.menu_over_map`。`ModernStoreUi::context_popup_active()`）。
         * アンカーは右クリックのたびに書き換わるだけなので、毎フレーム渡せばよい（地図のクリックを申告しない
         * コアはアンカーが偽のまま＝窓は今までどおり）。
         */
        hud_state.modern.set_context_anchor(input_state.map_click.anchor_valid,
            input_state.map_click.anchor_x, input_state.map_click.anchor_y);
        {
            presentation::InputEventsMessage late_input;
            if (modern_was_pending && !hud_state.modern.pending() && !floor_cutin.blocks_input()) {
                hud_state.modern.advance(late_input);
            }
            service_tab_requests(late_input);
            if (!late_input.events.empty() && !floor_cutin.blocks_input()) {
                (void)link.send_input_events(late_input);
            }
        }
        if(hud_state.modern.take_scope_changed()) {
            // A held stick/touch direction belongs to the screen on which it
            // began.  Carrying its repeat into the next birth page made the
            // race cursor continue moving without a new operation.
            // Forget only the direction hold: a shoulder button (LB/RB) held across the
            // change must still fire on release (F-rogue36-08).
            pad.forget_direction_hold();
            vpad.forget_direction_hold();
        }

        // (d) 描く。**present は 1 フレーム 1 回**（設計書 §14-5）。
        if (!post.resize(layout.scene.w, layout.scene.h, err)) {
            show_message("画面の FBO を作れませんでした:\n" + err);
            result = 1;
            break;
        }
        /*
         * 3D を出す矩形を GL の約束（**左下原点**）へ直す。UI の矩形は左上原点なので、
         * `screen_h - y - h` が下端になる。ここを取り違えると `Split` で絵が上下に飛ぶ。
         */
        const int scene_gl_x = layout.scene.x;
        const int scene_gl_y = screen_h - layout.scene.y - layout.scene.h;
        /*
         * **新 UI の窓の後ろにはマップを描き続ける**（決めたこと 2026-09-26
         * 「すべてのメニューで背景にマップを残して」）。新 UI の窓（`ModernStoreUi`）は
         * 自分のパネルの地を敷いて描くので、原作の Term の写し（`menu_term_lines`）が
         * 積んであっても字がマップと重なることはない。持ち物一覧だけがマップを残して
         * いたのは、一覧をコアの側で組み Term の写しを 1 行も積まないからだった。
         * タイトル・キャラクター作成（マップが無い）とオリジナル UI は従来どおり。
         */
        const bool modern_over_map = settings.modern_ui && hud_state.modern.active()
            && modern_window_keeps_hud(frame);
        /*
         * 前のタブの写しは、次の窓が描ける（中身が届いた）・機能メニューが開き直された・
         * 1.5 秒経った、のどれかで捨てる（届かないときに前の画が居座らない）。
         */
        if (menu_handoff && (feature_menu.is_open() || !settings.modern_ui
                || (SDL_GetTicks() - menu_handoff_since > 1500u)
                || !(hud_state.modern.opening_placeholder() || hud_state.modern.awaiting_parent_child()))) {
            menu_handoff.reset();
        }
        if (modern_handoff && (feature_menu.is_open() || !settings.modern_ui
                || (SDL_GetTicks() - modern_handoff_since > 1500u)
                || (hud_state.modern.active() && !hud_state.modern.opening_placeholder() && !hud_state.modern.leaving_parent_tab()))) {
            modern_handoff.reset();
        }
        const bool modern_window_drawn = hud_state.modern.active() && !hud_state.modern.opening_placeholder();
        const bool modern_handoff_drawn = modern_handoff && !feature_menu.is_open() && !modern_window_drawn;
        const bool menu_drawn = feature_menu.is_open() || static_cast<bool>(menu_handoff) || modern_handoff_drawn;
        const bool map_visible = frame_shows_map(frame) || modern_over_map;
        const bool showing_term = !map_visible;
        /*
         * ---- 空中の板を組む（§20・§22。2026-08-14 に決めた）----
         *
         * **画面をそのまま 1 枚で出す。**矩形ごとにばらして頭のまわりへ散らす作りを
         * 一度やったが、実機では「位置がぐちゃぐちゃ」だった。指示は
         * 「分割も一旦やめて、そのまま並べて」「無理に HMD の方に向けなくていい」
         * 「向いている方角の頭上に固定」の 3 つ。
         *
         * 3D の穴（`layout.scene`）は**塞がない**——VR では板の下敷きを出さない
         * （`set_panel_transparent`）ので、描かなかった所はそのまま世界が透ける。
         */
        vr_panels.clear();
        //! VR の画面の板を空間へ据える。中身は `app/run_parts.cpp`。
        place_vr_hud_panel(vr_frame_ready, xr, vr_frame, vr_anchor, vr_fake_ui,
            settings, layout, screen_w, screen_h, frame_seconds, vr_panel_yaw, vr_panels);
        /*
         * **2D アスキーのメインパネルでは 3D を 1 枚も描かない**（§6）。
         * 地形の組み立ても影も後処理も要らないので、`showing_term` と同じ枝へ落として
         * 窓を消すだけにする。描くのは下の UI のパスである（`draw_ascii_map_panel`）。
         */
        const bool skip_scene = showing_term
            || ((settings.main_panel == Hd2dSettings::MainPanel::Ascii) && !vr_frame_ready);
        if (skip_scene) {
            /*
             * コアの Term をそのまま写している間（タイトル・店・メニュー）は 3D が無い。
             * **画面用の FBO を通さず**、既定のフレームバッファを消すだけにする
             * （通しても中身が消去色 1 色なので、トーンマップとビネットが掛かるだけ損）。
             */
            glViewport(0, 0, screen_w, screen_h);
            glClearColor(0.04f, 0.05f, 0.07f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            /*
             * VR でも**目は必ず塗る**。3D が無い場面（タイトル・店・メニュー）で塗らないと、
             * ランタイムへ渡す swapchain の中身が前のフレームのままになり、HMD に
             * 前の絵が固まって残る。文字はまだ載らない（M2 のクワッドレイヤ）ので、
             * この間は無地が正しい。
             */
            if (vr_frame_ready && xr.valid()) {
                for (int eye = 0; eye < vr_frame.view_count; ++eye) {
                    if (!xr.bind_eye(eye)) {
                        continue;
                    }
                    glClearColor(0.04f, 0.05f, 0.07f, 1.f);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                }
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(0, 0, screen_w, screen_h);
            }
        } else {
            /*
             * `Split` では 3D が窓の一部でしかないので、**まず窓全体を消す**。
             * 消さないと、パネルの下敷きが半透明でない所（枠と枠の隙間）に前のフレームが残る。
             */
            glViewport(0, 0, screen_w, screen_h);
            glClearColor(0.02f, 0.02f, 0.03f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            const Uint64 t0 = clock::perf();
            const bool floor_changed = !same_floor(meaning.identity, frame.floor);
            if (rebuild_floor_meaning(frame, meaning)) {
                ++meaning_rebuilds;
                if (floor_changed) {
                    entities.reset(); // 別の階。覚えている位置は意味を持たない
                    attack_fx.clear(); // 前の階の座標で飛んでいる効果は捨てる
                }
                /*
                 * 町の読み直し（§10）。**意味づけが変わったときだけ**でよい
                 * （敷地の形はミニマップ由来なので、意味づけと同じときにしか動かない）。
                 * 町でなければ中で空を返し、置く側は従来の経路を通る。
                 */
                /*
                 * **岩山だけは覚えておく**（`world/town_plan.h` の `CragMemory`）。
                 * 既知が少ない場面（ダンジョンから上がった直後）で、山の塊が
                 * 敷地の条件を満たして家に化けるのを止める。町の地図は動かないので、
                 * 一度「岩山」と読めたマスは永久に岩山でよい。
                 */
                //! データの入口の印（(4c)）。町でなければ空で、`rebuild_town_plan` は素通りする。
                const std::vector<std::uint8_t> town_entrances = town_entrance_mask(frame, library);
                if (rebuild_town_plan(meaning, town_plan, &crag_memory, &town_entrances)) {
                    std::fprintf(stderr, "[hd2d] %s\n", town_plan_summary(town_plan).c_str());
                }
            }
            terrain_memory.update(frame); //!< 地形の細別の記憶（P10。フロアが変われば中で捨てる）
            /*
             * 実体（P4）。**地形より先に組む**（P10 レビュー 5）。プレイヤのなめらかな位置が
             * ここで決まり、`MoveSmoothing::All` ではカメラがそれを追うので、
             * 地形の視錐台を作る前に camera.target を確定させる必要がある。
             */
            /*
             * 一人称では**歩調ぶんの最低速度**を渡す（2026-08-14 に決めた）。
             * 1 マス ÷ 1 歩の間隔。これで押している間は流れ続け、離すとマスへ収まる。
             */
            const float step_seconds = static_cast<float>(std::max(1, settings.fps_step_ms)) / 1000.f;
            /*
             * 板の向き（2026-08-15 に決めた）。**一人称のときだけ**キャラへ正対させる。
             * 向く先は `camera.target`（＝なめらかに追っているキャラの位置）で、
             * **視線ではない**——視線に正対させると首を振るたびに世界じゅうの板が回る。
             */
            SlabFacing slab_facing;
            slab_facing.active = first_person.active;
            slab_facing.at_x = camera.target.x;
            slab_facing.at_y = camera.target.y;
            slab_facing.snap8 = (settings.fps_slab_turn == Hd2dSettings::SlabTurn::Snap8);
            /*
             * 見下ろしで回したぶんだけ板も回す。
             * 板の厚みは 4 ボクセルしか無いので、南向きのままだと 90° 回した瞬間に
             * **縁しか見えなくなる**（実質キャラが消える）。
             * @note 回転が 0 のときは 0 なので、改修前と 1 ビットも変わらない。
             */
            slab_facing.fixed_yaw = vr_frame_ready ? xr::board_turn_slab_yaw(vr_board_turn) : camera_turn_yaw;
            /*
             * 新しい「画面に正対」モード。**各板からカメラ位置へ向けるのではない。**
             * 全板へカメラ自身の yaw/pitch を同じように与えることで、画面のどこに居ても
             * 板面が投影面と平行になる。VR は左右の目で投影面が別なので従来向きを維持する。
             */
            if (settings.entity_slab_screen_aligned && !vr_frame_ready) {
                slab_facing.screen_aligned = true;
                slab_facing.active = false;
                slab_facing.fixed_yaw = camera.yaw;
                slab_facing.screen_pitch = camera.pitch;
            }
            /*
             * **アスキー実体**。要ると分かってから用意する。
             * 一度失敗したら二度と試さない（毎フレーム TTF を叩かない）。
             */
            const bool want_ascii = (settings.entity_style == Hd2dSettings::EntityStyle::Ascii);
            /*
             * **TRON の面の字も同じフォントを使う**。
             * `GlyphAtlas` を 2 つ開くと TTF を 2 度叩くことになるので、用意の枝に相乗りする。
             * 実体がアスキーでなくても、TRON なら字が要る（軸が違う。§2）。
             */
            const bool want_face_glyph
                = (settings.scene_look == SceneLookKind::Tron) && settings.tron_face_glyph;
            /*
             * **一過性の重ね書き**（SQ-2。`world/overlay_view.h`）。ダメージの数字や
             * 飛跡は「字」なので、実体を板で描いていても字の目録が要る。
             * 出さないコアでは `map_overlay` が常に空なので、ここは 1 度も真にならない。
             */
            const bool want_overlay_glyph = !frame.map_overlay.empty();
            /*
             * **板の道の受け皿**。
             * 目録に無い実体を字の板へ落とすための字の目録。既定 入なので、
             * 実質どのコアでも字の目録は用意される（用意は 1 度きり・板の実体には
             * 1 画素も影響しない——受け皿は板が引けなかったときにしか使われない）。
             */
            const bool want_fallback_glyph = settings.entity_glyph_fallback;
            if ((want_ascii || want_face_glyph || want_overlay_glyph || want_fallback_glyph)
                && !glyph_ready && !glyph_failed) {
                std::string glyph_err;
                if (glyph_atlas.init(GlyphAtlas::kDefaultPx, glyph_err)
                    && glyph_boards.init(glyph_err)) {
                    glyph_ready = true;
                    std::fprintf(stderr, "[hd2d] アスキー実体を用意しました（%dpx / アトラス %d）\n",
                        GlyphAtlas::kDefaultPx, glyph_atlas.side());
                } else {
                    glyph_failed = true;
                    glyph_atlas.shutdown();
                    //! **板へ落とす。**文字が出ないまま実体が消えるより従来の絵のほうがまし。
                    std::fprintf(stderr, "[hd2d] アスキー実体を用意できません: %s（板で描きます）\n",
                        glyph_err.c_str());
                }
            }
            const bool ascii_entities = want_ascii && glyph_ready;
            if (player_model_loaded && !player_model_attempted) {
                player_model_attempted = true;
                std::string model_error;
                if (!optional_render.player_model.init(player_model_asset, model_error)) {
                    std::fprintf(stderr, "[model] keeping existing player display: %s\n", model_error.c_str());
                }
            }
            // First milestone: the ordinary overhead view. Existing ASCII/FPS/VR modes remain usable.
            const bool model_player = optional_render.player_model.ready()
                && !first_person.active && !vr_frame_ready && !want_ascii;
            entities.build(frame, tile_catalog, slab_library, renderer, frame_seconds, entity_view,
                first_person.active,
                first_person.active ? (1.f / step_seconds) : 0.f, slab_facing,
                ascii_entities ? &glyph_atlas : nullptr,
                static_cast<float>(settings.entity_glyph_pct) / 100.f,
                (settings.entity_glyph_fallback && glyph_ready) ? &glyph_atlas : nullptr,
                model_player, dcss_ui ? &dcss_ui->ring_tints() : nullptr,
                dcss_ui ? 0.2f : 0.f); //!< DCSS（D24）: 板をマスの中で 2 割奥へ
            /*
             * 地面の凹凸（§6.3）。立っているマスの地面の持ち上げ量だけ実体も持ち上げる（足が埋まらないように）。
             * 量は地形が前のフレームで決めたもの（`terrain` はこの後で作り直す。階が替わった最初の 1 フレームだけずれる）。
             * 申告の無いコアは `ground_lift` が空で、どれも 0 を足すだけ。
             */
            if (!terrain.ground_lift.empty()) {
                const auto lift_at = [&terrain](float x, float y) {
                    return terrain.ground_lift_at(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)));
                };
                for (auto &slot : entity_view.slabs) {
                    slot.inst.z += lift_at(slot.cx, slot.cy);
                }
                for (auto &glyph : entity_view.glyphs) {
                    glyph.z += lift_at(glyph.x, glyph.y);
                }
                for (auto &ring : entity_view.rings) {
                    ring.z += lift_at(ring.x, ring.y);
                }
            }
            for(auto &body:entity_view.targeting_entities)if(body.glyph) {
                body.normal=vr_frame_ready
                    ? normalize(Vec3{entity_view.player_x-body.x,entity_view.player_y-body.y,0.f})
                    : Vec3{-std::sin(camera.yaw),std::cos(camera.yaw),0.f};
            }
            /*
             * 攻撃の視覚効果が対象の見た目を変える（押しつぶし・震え・のけぞり・浮き上がり・灰色・二重。
             * `world/entity_fx_mod.h`）。前のフレームの評価結果を当てる（`attack_fx_frame` は後ろで作り直す）。
             * 空なら何もしないので、効果が無いときの絵は従来と同じ。
             */
            fx_apply_entity_mods(entity_view, attack_fx_frame.entity_mods);
            const bool draw_player_model = model_player && entity_view.player_valid;
            const FxEntityMod *player_fx_mod = attack_fx_frame.entity_mods.empty() ? nullptr
                : fx_find_mod(attack_fx_frame.entity_mods, static_cast<int>(std::floor(entity_view.player_x)),
                      static_cast<int>(std::floor(entity_view.player_y)));
            const Mat4 player_model_transform = (player_fx_mod != nullptr)
                ? fx_mod_model_matrix(*player_fx_mod, Vec3{ entity_view.player_x, entity_view.player_y,
                      0.02f + terrain.ground_lift_at(static_cast<int>(std::floor(entity_view.player_x)), static_cast<int>(std::floor(entity_view.player_y))) })
                : translation({entity_view.player_x, entity_view.player_y,
                      0.02f + terrain.ground_lift_at(static_cast<int>(std::floor(entity_view.player_x)), static_cast<int>(std::floor(entity_view.player_y)))});
            if (draw_player_model && !player_model_reported) {
                std::fprintf(stderr, "[model] player GLB active: %zu vertices\n", player_model_asset.vertex_count);
                player_model_reported = true;
            }
            /*
             * **一過性の重ね書き**（SQ-2）。実体とは別の列に積む——板で描いていても
             * 重ね書きは字だからで、`EntityView::glyphs` は板と排他である
             * （`world/overlay_view.h` の註記）。
             */
            overlay_glyphs.clear();
            if (glyph_ready) {
                (void)build_overlay_glyphs(frame, glyph_atlas,
                    static_cast<float>(settings.entity_glyph_pct) / 100.f, overlay_glyphs);
            }
            /*
             * **足元のリング**（SQ-1）。輪は `entities.build()` が組んである
             * （なめらかな位置を知っているのがあそこだけなので）。ここでは
             * 要るようになった 1 度だけシェーダを用意する。
             */
            if (!terrain.light_shafts.empty() && !light_shafts.ready() && !shaft_failed) {
                std::string shaft_err;
                if (!light_shafts.init(shaft_err)) {
                    std::fprintf(stderr, "[hd2d] %s\n", shaft_err.c_str());
                    light_shafts.shutdown();
                    shaft_failed = true; //!< **一度だけ言う**（毎フレーム吠えない）
                }
            }
            if ((frame.targeting.active||frame.targeting.preview_only||!frame.element_scope.empty()) && !optional_render.targeting.ready() && !optional_render.targeting_failed) {
                std::string targeting_error;
                if (!optional_render.targeting.init(targeting_error)) {
                    optional_render.targeting_failed = true;
                    optional_render.targeting.shutdown();
                    std::fprintf(stderr, "[hd2d] Targeting renderer: %s\n", targeting_error.c_str());
                }
            }
            if (!entity_view.rings.empty() && !ground_rings.ready() && !ring_failed) {
                std::string ring_err;
                if (!ground_rings.init(ring_err)) {
                    ring_failed = true;
                    ground_rings.shutdown();
                    std::fprintf(stderr, "[hd2d] 足元のリングを用意できません: %s\n", ring_err.c_str());
                }
            }
            /*
             * 移動のなめらかさ（P10 レビュー 5。「切り替えられるように」と決めた）。
             * | 設定 | 地形 | 実体 |
             * |---|---|---|
             * | `Entities`（従来） | マスへ吸着 | なめらか（＝実体が地形を追う「ひょこひょこ」） |
             * | `All` | **なめらか** | なめらか（全部が一緒に流れる） |
             *
             * カメラの注視点だけを差し替える。**可視窓（`ui_state`）はマス単位のまま**で、
             * ここを毎フレーム動かすとコアへの要求が毎フレーム変わる（v1 §7 の「変化した
             * ときだけ」が意味を失う）。
             */
            //! 闘技場の観戦中は書き戻さない（設計書 §4.6。注視点はフロア中心のまま）。
            //! コアが離れて見ているとき（`camera_detached`。位置の確認 L）も書き戻さない（修-64。そこで決めた注視点を @ の位置で打ち消さない）。
            if (!arena_watch && !frame.camera_detached && (settings.move_smoothing == MoveSmoothing::All) && entity_view.player_valid) {
                camera.target.x = entity_view.player_x;
                camera.target.y = entity_view.player_y;
            }
            /*
             * 被弾の揺れ。**注視点を書き直す処理より後**。
             * ここが最後の書き手なので、これ以降に注視点を触る処理を足すときは順番に注意する。
             */
            camera.target.x += shake_dx;
            camera.target.y += shake_dz;
            const Uint64 t1 = clock::perf();
            /*
             * 夜の度合い（P10 第 2 期）。窓と街灯の火の強さに掛ける。
             * **`HD2D_FORCE_TIME` に従う**（夕暮れの町を見るのに半日歩かずに済む）。
             */
            LightingState place_light = frame.lighting;
            if (force_time >= 0) {
                place_light.day_minute = force_time;
            }
            /*
             * **未探知を真っ黒な塊で埋めるのは、一人称かつ地下のときだけ**
             * （2026-08-11 に決めた「FPS モードの時のみダンジョン内は視界が通っている
             * 未踏破マスは真っ黒ブロックにしよう」）。
             *
             * 地上で埋めない: 未探知の野原まで黒い壁になると、町の外が箱の中に見える。
             * 見下ろしで埋めない: 俯瞰では「まだ行っていない所」と「壁」が同じ色になり、
             * 地図の形が読めなくなる。
             */
            const bool black_unknown = first_person.active
                && (frame.floor.kind == static_cast<int>(FloorKind::Dungeon));
            /*
             * **小物のロック**（`world/terrain_view.h` の `PropLatch`。2026-08-11 に決めた:
             * 「ダンジョンの小物は一度生成されたら書き換わらないように」）。
             * 階が変わったら中で捨てる。掛かるのは地下だけ。
             */
            prop_latch.follow(frame.floor);
            /*
             * 一人称の 2 段目と天井（2026-08-11 に決めた）。**一人称かつ地下のときだけ**
             * ——見下ろしは従来どおり 1 段・天井なし（同日の指示「見下ろしモードの時は
             * 今まで通り 1 ブロックの高さを維持」）。入切は機能メニュー（カメラの分類）。
             */
            const bool fps_dungeon = first_person.active
                && (frame.floor.kind == static_cast<int>(FloorKind::Dungeon));
            /*
             * 視錐台カリング。**VR ではカメラの視錐台で切らない**（設計書 §6）。
             * 平らな画面のカメラの視錐台で切ると、盤に載るのは「その 1 つの視点から
             * 見えるぶん」だけになる。VR では頭を動かして横から覗けるので、
             * **覗いた先が台形に切れて抜けて見える**。
             *
             * 代わりに**ジオラマでは卓の天板で切る**（2026-08-14 に決めた
             * 「テーブルの範囲がマップの描画範囲として、テーブルからはみ出ると
             * マップは消えてほしい」。§21）。卓と盤は**同じ中心**で、向きは
             * 90° の倍数しか違わないので、世界（マス）では依然**軸に沿った矩形**である
             * ——平面 4 枚で足りる。
             *
             * **ただし奇数段（90°／270°）では縦横が入れ替わる**
             * 畳 1 畳は 1.82×0.91m と縦横比が 2 倍
             * 違うので、入れ替えを忘れると**回した瞬間に地図が短辺で切られて細長く消える**。
             *
             * 一人称では切らない（世界の中に立っているので、卓は無い）。
             */
            Frustum terrain_frustum = Frustum::from(camera.view_projection());
            if (vr_frame_ready) {
                terrain_frustum = Frustum::everything();
                if (vr_room.visible && (vr_board.tile_m > 1e-6f)) {
                    const bool table_swapped = ((vr_board_turn % 2) != 0);
                    const float table_x_m = table_swapped ? vr_room.table_d : vr_room.table_w;
                    const float table_y_m = table_swapped ? vr_room.table_w : vr_room.table_d;
                    const float half_x = (table_x_m * 0.5f) / vr_board.tile_m;
                    const float half_y = (table_y_m * 0.5f) / vr_board.tile_m;
                    const float min_x = camera.target.x - half_x;
                    const float max_x = camera.target.x + half_x;
                    const float min_y = camera.target.y - half_y;
                    const float max_y = camera.target.y + half_y;
                    terrain_frustum = Frustum::slab_xy(min_x, min_y, max_x, max_y);
                    /*
                     * **実体も同じ矩形で落とす。**地形だけ切ると、卓の外の宙に
                     * モンスターや道具のかきわりだけが浮いて残る。
                     */
                    entity_view.slabs.erase(
                        std::remove_if(entity_view.slabs.begin(), entity_view.slabs.end(),
                            [&](const SlabInstance &b) {
                                return (b.cx < min_x) || (b.cx > max_x) || (b.cy < min_y) || (b.cy > max_y);
                            }),
                        entity_view.slabs.end());
                    //! **文字の実体も同じ矩形で落とす**（片方だけ残ると卓の外に字が浮く）。
                    entity_view.glyphs.erase(
                        std::remove_if(entity_view.glyphs.begin(), entity_view.glyphs.end(),
                            [&](const BillboardInstance &b) {
                                return (b.x < min_x) || (b.x > max_x) || (b.y < min_y) || (b.y > max_y);
                            }),
                        entity_view.glyphs.end());
                }
            }
            /*
             * **TRON ではマス 1 つにつき立方体 1 つ**（
             * 2026-08-19 に決めた「全てシンプルな立方体」「小物オブジェクトは不要」）。
             * ライブラリも装飾層も止まるので、アスキー地図が言っているものだけが建つ。
             * 標準では偽なので**従来の絵と 1 ビットも変わらない**。
             */
            const bool tron_blocks = (settings.scene_look == SceneLookKind::Tron);
            if (!terrain_models_attempted && (wall_model_loaded || floor_model_loaded)) {
                terrain_models_attempted=true;
                std::string model_error;
                for(std::size_t i=0;i<kTerrainModelCount;++i) if(terrain_part_loaded[i]) {
                    if(!optional_render.terrain_parts[i].init(terrain_part_assets[i],model_error))
                        std::fprintf(stderr,"[model] keeping original %s: %s\n",kTerrainModelFiles[i],model_error.c_str());
                }
                if (wall_model_loaded && !optional_render.wall_model.init(wall_model_asset,model_error))
                    std::fprintf(stderr,"[model] keeping existing wall display: %s\n",model_error.c_str());
                if (floor_model_loaded && !optional_render.floor_model.init(floor_model_asset,model_error))
                    std::fprintf(stderr,"[model] keeping existing floor display: %s\n",model_error.c_str());
            }
            TerrainModels terrain_models; // Deferred: retain the existing voxel terrain.
            build_terrain_view(meaning, terrain_frustum, terrain,
                &terrain_memory, library_ptr, town_plan.valid() ? &town_plan : nullptr,
                lamp_night_factor(place_light),
                first_person.active ? kFpsWallInset : 0.f, black_unknown, &prop_latch,
                fps_dungeon && settings.fps_wall_upper, fps_dungeon && settings.fps_ceiling, tron_blocks, terrain_models);
            if (!terrain_models_reported && (!terrain.model_walls.empty() || !terrain.model_floors.empty()
                || !terrain.model_blocks.empty() || !terrain.model_ground.empty())) {
                std::fprintf(stderr,"[model] terrain GLB active: walls=%zu floors=%zu\n",
                    terrain.model_walls.size(),terrain.model_floors.size());
                std::fprintf(stderr,"[model] common terrain: blocks=%zu ground=%zu rubble=%zu water=%zu lava=%zu shores=%zu\n",
                    terrain.model_blocks.size(),terrain.model_ground.size(),terrain.model_parts[0].size(),
                    terrain.model_parts[1].size(),terrain.model_parts[2].size(),terrain.model_parts[3].size());
                terrain_models_reported=true;
            }
            if (terrain.drawn_cells > 0) {
                ++frames_with_terrain;
            }
            /*
             * 光（P5）。時刻とフロアの種別から太陽と環境光を作り、松明と溶岩を拾う。
             * **実体は影のパスにも要る**ので、先に組み立てておく（P4 では描く直前だった）。
             */
            light_state = frame.lighting;
            if (force_time >= 0) {
                light_state.day_minute = force_time; // `HD2D_FORCE_TIME`。コアには触らない
                light_state.daytime = (force_time >= (6 * 60)) && (force_time < (18 * 60));
            }
            lighting = make_scene_lighting(light_state, frame.floor);
            /*
             * **町の夜の明るさ**（町の表の `night_light`。Angband の町。決めたこと 2026-09-24）。
             * 町の中（地上）で、その町の表に 1 より大きい値があるときだけ。ほかの町と地下は
             * `terrain.night_light` が 1.0 なので何も掛からない。
             */
            if ((terrain.night_light > 1.001f) && (frame.floor.kind == static_cast<int>(FloorKind::Surface))) {
                apply_town_night_light(lighting, light_state, terrain.night_light);
            }
            /*
             * **明るい部屋に居るあいだは環境光を上げる**。
             * 判定は @ のマスの `CELL_FEAT_GLOWING`（＝コアの `CAVE_GLOW`）。
             * **1 フレームで飛ばさない**——部屋と通路を行き来するたびに画面全体の
             * 明るさが跳ねると、歩いているだけで目が痛い。`lit_room_mix` を
             * 秒 4 の速さで目標へ寄せる（出入りに 0.25 秒ほど掛かる）。
             */
            {
                const float target = lit_room_target(frame);
                const float rate = std::min(1.f, frame_seconds * 4.f);
                lit_room_mix += (target - lit_room_mix) * rate;
                apply_lit_room_ambient(lighting, frame.floor, lit_room_mix);
            }
            /*
             * RogueClone2 は原作に松明・部屋ごとの照明という概念がなく、見えている範囲を
             * 常に同じ明るさで表示する。HD2D でも地下用の暗い照明へ寄せず、変愚蛮怒の
             * 地上・正午と同じ照明計算を使う。地形の扱いは Dungeon のまま変えない。
             */
            if (frame.floor.place_name_utf8 == "RogueClone2"
                || frame.floor.place_name_utf8 == "Rogue 3.6"
                || frame.floor.place_name_utf8 == "Rogue 5.4") {
                LightingState noon = light_state;
                noon.daytime = true;
                noon.day_minute = 720;
                FloorIdentity surface = frame.floor;
                surface.kind = static_cast<int>(FloorKind::Surface);
                lighting = make_scene_lighting(noon, surface);
            }
            /*
             * **覆いの町の中の自然光**（決めたこと 2026-08-18 その3:「町の中の明かりは
             * 自然光をオレンジっぽくしてもっと明るくしよう」）。提灯と灼熱地獄の照り返しが
             * 空気を染めている、という読みで環境光（空と照り返し）を暖色に振り、
             * 底を持ち上げる——山の中は時刻で明るさが変わらない（太陽が届かないので、
             * 夜に落とす理由も無い）。方向光と点光源には触らない（山の背の日向と
             * ガス灯の明かりの対比はそのまま）。
             */
            if (terrain.covered) {
                lighting.sky_color = Vec3{ 0.88f, 0.62f, 0.40f };
                lighting.bounce_color = Vec3{ 1.00f, 0.70f, 0.46f };
                lighting.ambient_scale = std::max(lighting.ambient_scale, 1.15f);
            }
            /*
             * **空間の明るさ**（`Hd2dSettings::dungeon_light`）。環境光にだけ掛ける。
             * 実体をボクセルの板にして暗くなったぶんを、板の描画を弄らずにここで戻す
             * （2026-08-15 に決めた）。方向光と点光源には触らないので、
             * 松明の丸い明かりと日向の対比はそのまま残る。
             */
            lighting.ambient_scale *= settings.dungeon_light;
            //! 日中の天候の光（コアが天候を申告したときだけ。夜は変えない。地上でも地下でも）。
            apply_weather_light(lighting, weather_view, light_state);
            /*
             * **画調**。**光を組み終えた最後**に掛ける
             * ——上の町の補正や `dungeon_light` の前に置くと、TRON で落とした光を
             * あとから持ち上げ直すことになる。
             *
             * 標準では `apply_look_to_lighting` が何も触らず、`LookParams::enabled` も 0 なので
             * **従来の絵と 1 ビットも変わらない**（`--look-check` が毎回確かめる）。
             */
            look = make_look_params(settings.scene_look);
            look.face_glyph = settings.tron_face_glyph;
            apply_look_to_lighting(settings.scene_look, settings.tron_sky, lighting);
            /*
             * **面のアスキー文字の材**。
             *
             * 中身は `TerrainMemory` の記憶から詰める——`frame.cells` から直に詰めると
             * **可視窓の外の覚えた壁にだけ字が無い**ことになり、地図の読み方が場所で変わる。
             * 記憶はフロアが変われば中で捨てられるので、階を降りても前の階の字は残らない。
             *
             * 色はここで引く（`term_color_to_rgb`）。索引のまま覚えて引くのは毎回、という
             * 約束は `TerrainMemory::fg` の注記のとおりである（色表は `&` や pref で変わる）。
             */
            if (want_face_glyph && glyph_ready && terrain_memory.valid()) {
                if (!look_sheet_ready && !look_sheet_failed) {
                    std::string sheet_err;
                    if (glyph_atlas.bake_sheet(sheet_err)) {
                        look_sheet_ready = true;
                    } else {
                        look_sheet_failed = true;
                        //! **字だけ諦める。**線と黒い面は出るので、TRON そのものは成立する。
                        std::fprintf(stderr, "[hd2d] TRON の面の字を用意できません: %s（線だけで描きます）\n",
                            sheet_err.c_str());
                    }
                }
                if (look_sheet_ready) {
                    const int cw = terrain_memory.width;
                    const int ch = terrain_memory.height;
                    if ((look_cells_tex == 0) || (cw != look_cells_w) || (ch != look_cells_h)) {
                        if (look_cells_tex != 0) {
                            glDeleteTextures(1, &look_cells_tex);
                            look_cells_tex = 0;
                        }
                        glGenTextures(1, &look_cells_tex);
                        glBindTexture(GL_TEXTURE_2D, look_cells_tex);
                        /*
                         * **整数テクスチャは必ず `GL_NEAREST`。**線形補間は整数形式では
                         * 許されておらず、既定の `GL_LINEAR_MIPMAP_LINEAR` のままだと
                         * テクスチャが不完全になって黙って 0 が返る。
                         */
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(GL_NEAREST));
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(GL_NEAREST));
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, static_cast<GLint>(GL_CLAMP_TO_EDGE));
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, static_cast<GLint>(GL_CLAMP_TO_EDGE));
                        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8UI), cw, ch, 0,
                            GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);
                        glBindTexture(GL_TEXTURE_2D, 0);
                        look_cells_w = cw;
                        look_cells_h = ch;
                    }
                    look_cells_rgba.assign(
                        static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch) * 4u, 0u);
                    for (std::size_t i = 0;
                         i < (static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch)); ++i) {
                        const std::uint8_t code = terrain_memory.ascii[i];
                        if (code == 0u) {
                            continue; //!< まだ見ていないマス。**捏造しない**
                        }
                        const RgbColor rgb = term_color_to_rgb(terrain_memory.fg[i]);
                        look_cells_rgba[(i * 4u) + 0u] = code;
                        look_cells_rgba[(i * 4u) + 1u] = rgb.r;
                        look_cells_rgba[(i * 4u) + 2u] = rgb.g;
                        look_cells_rgba[(i * 4u) + 3u] = rgb.b;
                    }
                    glBindTexture(GL_TEXTURE_2D, look_cells_tex);
                    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cw, ch, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE,
                        look_cells_rgba.data());
                    glBindTexture(GL_TEXTURE_2D, 0);

                    look_glyphs = LookGlyphSource{};
                    look_glyphs.sheet = glyph_atlas.sheet_texture();
                    look_glyphs.cells = look_cells_tex;
                    //! 記憶はフロア全域（ミニマップと同じ寸法）なので、原点は 0。
                    look_glyphs.width = cw;
                    look_glyphs.height = ch;
                } else {
                    look_glyphs = LookGlyphSource{};
                }
            } else {
                look_glyphs = LookGlyphSource{};
            }
            renderer.set_look_glyphs(look_glyphs);
            /*
             * **ボクセルとビルボードへ同じものを渡す。**片方だけ TRON にすると、
             * 同じ場所に立つ壁と人物で画調が食い違う（`lighting.h` の「式は 1 か所」と同じ理由）。
             */
            renderer.set_look(look);
            glyph_boards.set_look(look);
            /*
             * **面の汚し**（`render/surface_wear.h`。2026-08-23 に決めた）。
             * ボクセルにだけ掛ける——実体の板（かきわり）は描き込んであるタイルなので、
             * 上から汚すと絵師の描いた濃淡と喧嘩する。
             *
             * `wear_pct` が 0 なら `amount` も 0 で、シェーダが早く抜ける
             * ＝**従来と 1 ビットも変わらない**。
             */
            WearParams wear;
            wear.amount = static_cast<float>(settings.wear_pct) / 100.f;
            wear.materials = settings.wear_materials;
            renderer.set_wear(wear);
            WearParams model_wear=wear;
            model_wear.seed=static_cast<float>(cell_seed(0,0,meaning.identity)&0xffffu)/65536.f;
            optional_render.wall_model.set_wear(model_wear,MaterialClass::Stone);
            optional_render.floor_model.set_wear(model_wear,MaterialClass::Stone);
            for(std::size_t i=0;i<kTerrainModelCount;++i) {
                const bool clean=i==static_cast<std::size_t>(TerrainModelKind::Water) || i==static_cast<std::size_t>(TerrainModelKind::Lava);
                optional_render.terrain_parts[i].set_wear(model_wear,clean?MaterialClass::Clean:MaterialClass::Stone);
            }
            //! **木の葉**（`render/leaf_detail.h`）。葉の面にだけ掛かる。
            LeafParams leaf_detail;
            leaf_detail.amount = static_cast<float>(settings.leaf_pct) / 100.f;
            renderer.set_leaf(leaf_detail);
            /*
             * カラーグレーディングの LUT（`look_grade`）。**画調が変わったときだけ**焼く
             * ——32³ の格子を毎フレーム焼く仕事ではない。`--lut=` で外から読ませたときは
             * **触らない**（「今回だけ」の指定を画調が塗り潰さない）。
             */
            /*
             * **セピアも LUT の側**である（2026-08-23 に決めた）。画調と同じく
             * 「変わったときだけ焼く」——32³ の格子は毎フレーム焼く仕事ではない。
             */
            if (((settings.scene_look != look_lut_baked) || (settings.sepia != sepia_lut_baked))
                && options.lut_path.empty()) {
                std::string lut_err;
                if (post.set_grade(sepia_grade(look_grade(settings.scene_look), settings.sepia), lut_err)) {
                    look_lut_baked = settings.scene_look;
                    sepia_lut_baked = settings.sepia;
                } else {
                    std::fprintf(stderr, "[hd2d] 画調の LUT を焼けませんでした: %s\n", lut_err.c_str());
                    look_lut_baked = settings.scene_look; //!< 毎フレーム試し続けない
                    sepia_lut_baked = settings.sepia;
                }
            }
            //! 空（地上のみ）。**光と同じ `light_state` から**引く（太陽の位置を 1 か所に保つ）。
            sky_state = sky_state_from(light_state, frame.floor);
            /*
             * 松明は**実体と同じなめらかな位置**に置く（2026-08-14 に気づいた）。
             * `camera.target` は使えない——揺れ（被弾）が足してあるし、
             * `MoveSmoothing::Entities` ではマスへ吸着したままだからである。
             */
            Vec3 torch_pos{ entity_view.player_x, entity_view.player_y, 0.f };
            /*
             * DCSS（`dcss-ui` のコア。D24）: 板を画面へ正対させると、板がカメラ側へ傾いて松明の前へ出る
             * （松明はマスの中心の腰の高さ。板の足元はマスの手前の辺から 2 割奥）。すると自分の松明が板の正面を照らし、
             * プレイヤーの板だけが白く飛ぶ（ほかの怪物には松明が無い）。松明を板の向こう側（カメラから遠い側）へ半マスほどずらして、
             * 板を背にする。床や壁を照らす明るさは半マスのずれでほとんど変わらない。ほかのコアは通らない（従来どおり）。
             */
            if (dcss_ui && settings.entity_slab_screen_aligned && !vr_frame_ready) {
                constexpr float kDcssTorchBack = 0.6f;
                torch_pos.x += kDcssTorchBack * std::sin(camera.yaw);
                torch_pos.y -= kDcssTorchBack * std::cos(camera.yaw);
            }
            /*
             * **光の届く範囲**。松明の色と揺らぎは光源の種類
             * （`light_kind`）で変わる。揺らぎは松明だけで、`torch_flicker=0` なら止まる。
             * `player_lit` が空（送らないコア）なら場は無効で、絵は今までと同じ。
             */
            const int light_kind = static_cast<int>(light_state.light_kind);
            LightRangeParams range_params;
            range_params.strength = static_cast<float>(settings.light_range_pct) / 100.f;
            range_params.fade_seconds = static_cast<float>(settings.light_range_fade_ms) / 1000.f;
            range_params.flicker = settings.torch_flicker;
            const float light_gain = range_params.flicker ? light_flicker_gain(light_kind, world_seconds) : 1.f;
            /*
             * **攻撃の視覚効果**（矢・弾・投げた物・魔法の玉・爆発）。
             * 実時計（`clock::ticks()`。被弾の演出と同じ）で今の絵を作る。点光源は下の
             * `collect_point_lights()` へ足し（枠を先に確保する）、ボクセルと板は `draw_scene` が描く。
             * 起きていなければ何も作らず、絵は従来と 1 画素も変わらない。
             */
            attack_fx_frame = FxFrame{};
            if (attack_fx.active()) {
                if (!optional_render.attack_fx.ready() && !optional_render.attack_fx_failed) {
                    std::string fx_err;
                    if (!optional_render.attack_fx.init(fx_err)) {
                        optional_render.attack_fx_failed = true; //!< 一度だけ言う（毎フレーム吠えない）
                        std::fprintf(stderr, "[hd2d] 攻撃の視覚効果を用意できません: %s\n", fx_err.c_str());
                    }
                }
                FxViewParams fx_view;
                fx_view.has_slab = [&](std::uint16_t item_id) {
                    FxSlabSource source;
                    return attack_fx_slab_resolver(item_id, source);
                };
                fx_view.face_yaw = slab_facing.fixed_yaw;
                fx_view.face_pitch = slab_facing.screen_aligned ? slab_facing.screen_pitch : 0.f;
                fx_view.face_toward = slab_facing.active;
                fx_view.toward_x = slab_facing.at_x;
                fx_view.toward_y = slab_facing.at_y;
                {
                    //! 画面の右と上（絵・輪・タイルの写しを画面へ正対させる）。
                    const Vec3 fx_forward = camera.forward();
                    const Vec3 fx_right = normalize(cross(Vec3{ 0.f, 0.f, 1.f }, fx_forward));
                    fx_view.cam_right = fx_right;
                    fx_view.cam_up = normalize(cross(fx_forward, fx_right));
                }
                //! 対象のマスにいる実体のタイルの画素（分解の「タイルの写し」。読んだ絵は索引ごとに覚える）。
                fx_view.tile_at = [&](int gx, int gy, FxTileImage &out) {
                    static std::unordered_map<std::uint16_t, FxTileImage> tile_images;
                    for (const MapCellView &cell : frame.cells) {
                        if ((cell.gx != gx) || (cell.gy != gy) || (cell.tile_index == 0)
                            || ((cell.monster_id == 0) && ((cell.feature_flags & CELL_FEAT_PLAYER) == 0u))) {
                            continue;
                        }
                        auto found = tile_images.find(cell.tile_index);
                        if (found == tile_images.end()) {
                            FxTileImage image;
                            const TileEntry *entry = tile_catalog.find(cell.tile_index);
                            /*
                             * **配布版には PNG のタイルが入っていない**（板だけを配る）ので、実際に描いている板（`.vox`）の正面から
                             * 画素を起こす。板が読めないときだけ PNG（開発版）を試す。板は足元の空白を詰めてあるので、
                             * 高さは画素の比率に合わせる（幅は従来どおり 0.8 マス）。
                             */
                            bool read_ok = false;
                            if (entry != nullptr) {
                                read_ok = slab_library.front_pixels(entry->path, image.width, image.height, image.rgba);
                                if (read_ok) {
                                    image.height_cells = image.width_cells * (static_cast<float>(image.height) / static_cast<float>(std::max(image.width, 1)));
                                } else {
                                    read_ok = fx_load_png(entry->path, image);
                                }
                            }
                            if (!read_ok) {
                                image = FxTileImage{};
                            }
                            found = tile_images.emplace(cell.tile_index, std::move(image)).first;
                        }
                        if (found->second.width <= 0) {
                            return false;
                        }
                        out = found->second;
                        return true;
                    }
                    return false;
                };
                attack_fx.evaluate(clock::ticks(), VoxelFxParams{}, fx_view, attack_fx_frame);
            }
            //! DCSS の立体の雲（段 3・D14）。半透明のボクセルとして攻撃の視覚効果のボクセルへ足す（描き手は同じ）。申告 `dcss-ui` のコアだけ。
            if (dcss_ui && dcss_ui->in_game()) {
                (void)dcss_ui->append_cloud_voxels(attack_fx_frame.voxels, world_seconds);
                {
                    //! DCSS の攻撃の演出（段 4。ビーム・矢・爆発・閃光の原作のタイルの絵。`dcss/dcss_fx.h`）。同じく攻撃の視覚効果のボクセルと点光源へ足す。
                    const Vec3 dcss_forward = camera.forward();
                    const Vec3 dcss_right = normalize(cross(Vec3{ 0.f, 0.f, 1.f }, dcss_forward));
                    (void)dcss_ui->append_fx(attack_fx_frame, dcss_right, normalize(cross(dcss_forward, dcss_right)));
                }
                if (!attack_fx_frame.voxels.empty() && !optional_render.attack_fx.ready() && !optional_render.attack_fx_failed) {
                    std::string fx_err;
                    if (!optional_render.attack_fx.init(fx_err)) {
                        optional_render.attack_fx_failed = true;
                        std::fprintf(stderr, "[hd2d] 攻撃の視覚効果を用意できません: %s\n", fx_err.c_str());
                    }
                }
            }
            dropped_lights = collect_point_lights(frame, camera.target, lighting,
                (frame.floor.kind != static_cast<int>(FloorKind::Surface)) ? 1.f
                                                                          : lamp_night_factor(place_light),
                entity_view.player_valid ? &torch_pos : nullptr, light_gain,
                attack_fx_frame.lights.empty() ? nullptr : &attack_fx_frame.lights);
            lit_range_field.update(frame, frame_seconds, range_params);
            lit_range_field.bind(lighting, range_params, light_kind, light_gain);
            /*
             * **祠のライトアップ**（2026-08-18 に決めた「龍神像を夜は下から
             * ライトアップ」）。terrain_view が積んだ光のリクエストへ夜の度合いを掛けて足す。
             * 枠（`kMaxPointLights`）が埋まっていたら諦める——像の演出のために
             * 近くの松明や店明かりを蹴落とすほどのものではない。
             */
            {
                float wish_night = (frame.floor.kind == static_cast<int>(FloorKind::Surface))
                    ? lamp_night_factor(place_light)
                    : 0.f;
                //! 覆いの町は昼も点す（岩天井の下に太陽は届かない。terrain_view と同じ判断）。
                if (terrain.covered) {
                    wish_night = std::max(wish_night, 0.85f);
                }
                if ((wish_night > 0.02f) && !terrain.lights.empty()) {
                    /*
                     * **近い順に足す**（2026-08-18 その3）。ガス灯が交差点ごとに立つので、
                     * 走査順（北西から）のままだと画面の端の灯が枠（16）を先に食い、
                     * プレイヤの足元の交差点が暗いままになる。リクエストが 1〜2 個だった頃
                     * （龍神像）は順序に意味が無かった。
                     */
                    std::vector<const TerrainViewLight *> wishes;
                    wishes.reserve(terrain.lights.size());
                    for (const TerrainViewLight &wish : terrain.lights) {
                        wishes.push_back(&wish);
                    }
                    const float ref_x = entity_view.player_valid ? entity_view.player_x : camera.target.x;
                    const float ref_y = entity_view.player_valid ? entity_view.player_y : camera.target.y;
                    std::sort(wishes.begin(), wishes.end(),
                        [ref_x, ref_y](const TerrainViewLight *a, const TerrainViewLight *b) {
                            const float da = ((a->x - ref_x) * (a->x - ref_x)) + ((a->y - ref_y) * (a->y - ref_y));
                            const float db = ((b->x - ref_x) * (b->x - ref_x)) + ((b->y - ref_y) * (b->y - ref_y));
                            return da < db;
                        });
                    for (const TerrainViewLight *wish : wishes) {
                        if (static_cast<int>(lighting.points.size()) >= kMaxPointLights) {
                            break;
                        }
                        PointLight point;
                        point.x = wish->x;
                        point.y = wish->y;
                        point.z = wish->z;
                        point.radius = wish->radius;
                        point.r = wish->r;
                        point.g = wish->g;
                        point.b = wish->b;
                        point.intensity = wish->intensity * wish_night;
                        lighting.points.push_back(point);
                    }
                }
            }
            //! 溶岩・発光地形（P7）。ライブラリが生きていれば溶岩は素材側が光る（P10）。
            /*
             * 発光地形は**地上では夜だけ**（P10 第 2 期）。店の入口は地形に `GLOW` を
             * 持っているので、掛けないと真昼の店先に白く飛んだ四角が出る。
             */
            const bool underground = frame.floor.kind != static_cast<int>(FloorKind::Surface);
            /*
             * 地上ではさらに半分に落とす。**面そのものが光る**のは洞窟の発光地形の絵で、
             * 町の店先では「白く塗り潰した四角」に見える（夜の絵で確かめた）。
             * 店先の明るさは、この弱い自発光と同じマスの点光源が受け持つ。
             */
            collect_emissive_slabs(frame, emissive_slabs, /*skip_lava=*/library_ptr != nullptr,
                underground ? 1.f : (lamp_night_factor(place_light) * 0.30f));
            /*
             * 動き（P6）。**地下では風を止める**（洞窟の草が揺れると嘘くさい）。
             * いま世界に置いているのは仮の箱と板で `motion.kind` はすべて静止なので、
             * ここを渡しても絵は変わらない。**プレハブが入る P10 で効き始める。**
             */
            WindParams wind;
            if (frame.floor.kind == static_cast<int>(FloorKind::Surface)) {
                wind.dir_x = 0.92f;
                wind.dir_y = 0.39f;
                wind.amplitude = 0.08f;
            }
            renderer.begin_motion(wind, world_seconds);
            if (library_ptr != nullptr) {
                //! 剛体の動き（P6 → P10 で世界に出た）。**1 回だけ作り、影と本描画が同じものを読む。**
                library_ptr->update_motions(world_seconds);
            }
            /*
             * 覆いの町（岩天井）では直方体を**天井の天面まで**持ち上げる（2026-08-18 その3）。
             * 高さ 1 マスのままだと、6 マスの蓋が光の空間で枠の外に出て影が欠け、
             * 山の中に真昼の日が差し込む。覆いが無ければ従来の値＝ 1 テクセルも変わらない。
             */
            const float shadow_height = terrain.covered ? std::max(kMaxPropHeight, terrain.cave_top) : kMaxPropHeight;
            const ShadowFit fit = fit_shadow(camera, lighting.sun_dir, shadow_height, shadow.side(), 0.f);
            //! `build` はここまで（地形の置き場所・実体・光・影の当てはめ）。
            const Uint64 t2 = clock::perf();

            /*
             * (d-1) 影のパス。設計書 §13 の注のとおり**幾何をもう一度描く**。
             * 影の濃さが 0（＝夜。太陽が地平線の下）なら書かない。読まれないので無駄になる。
             */
            const bool want_shadow = shadow.ready() && (lighting.shadow_strength > 0.001f);
            if (want_shadow) {
                shadow.begin();
                renderer.draw_instanced_depth(floor_gpu, fit.view_projection, terrain.slabs.data(), terrain.slabs.size());
                renderer.draw_instanced_depth(wall_gpu, fit.view_projection, terrain.boxes.data(), terrain.boxes.size());
                if (library_ptr != nullptr) {
                    draw_library_depth(renderer, *library_ptr, terrain, fit.view_projection); //!< P10
                }
                /*
                 * 実体も影を落とす。**板の形そのもの**が落ちる（α の輪郭ではなくなった）。
                 * **アスキー実体は影を落とさない**——
                 * 字の形の影は情報ではなくノイズである。`entity_view.slabs` は
                 * アスキーのときは空なので、この行はそのまま何もしない。
                 */
                draw_entity_slabs(renderer, slab_library, entity_view, &fit.view_projection);
                if (draw_player_model) {
                    optional_render.player_model.draw_depth(fit.view_projection, player_model_transform);
                }
                optional_render.wall_model.draw_depth(fit.view_projection,Mat4::identity(),
                    nullptr,terrain.model_walls.size(),terrain.model_walls.data());
                optional_render.floor_model.draw_depth(fit.view_projection,Mat4::identity(),
                    nullptr,terrain.model_floors.size(),terrain.model_floors.data());
                optional_render.wall_model.draw_depth(fit.view_projection,Mat4::identity(),nullptr,
                    terrain.model_blocks.size(),terrain.model_blocks.data());
                optional_render.floor_model.draw_depth(fit.view_projection,Mat4::identity(),nullptr,
                    terrain.model_ground.size(),terrain.model_ground.data());
                for(std::size_t i=0;i<kTerrainModelCount;++i)
                    optional_render.terrain_parts[i].draw_depth(fit.view_projection,Mat4::identity(),nullptr,
                        terrain.model_parts[i].size(),terrain.model_parts[i].data());
                shadow.end(screen_w, screen_h);
            }
            if (gpu_sync) {
                glFinish(); // 影のパスの**実際の**コストを測る（`HD2D_GPU_SYNC`）
            }
            const Uint64 t3 = clock::perf();
            //! 積みの終わり。`draw_scene` の中で打つ（VR では 2 回目の目の値が残る）。
            Uint64 t4 = t3;

            /*
             * (d-2)(d-3) シーンを 1 回ぶん描く。**フラットは 1 回・VR は左右で 2 回**、
             * 同じものを回す。視点まわりの値は
             * `RenderView` で受け取る——VR の視点は `Camera` からは作れないため
             * （位置も画角もランタイムが決める）。
             *
             * 影のパス（d-1）は**左右で共用**する。方向光の当てはめは目に依らないし、
             * 目ごとに焼き直すのは丸ごと無駄になる。
             */
            const auto draw_scene = [&](const RenderView &view, GLuint target_fbo, int vp_x, int vp_y,
                                        bool enable_cutaway, bool enable_dof) {
                /*
                 * 本描画。**画面用の FBO へ**（P7）。影のパスの後に始めるのは、
                 * `ShadowMap::end()` が既定のフレームバッファへ戻してしまうためである。
                 */
                {
                    //! 画調の消去色（標準では従来の 0.04/0.05/0.07 がそのまま返る）。
                    const Vec3 clear = look_clear_color(
                        settings.scene_look, settings.tron_sky, Vec3{ 0.04f, 0.05f, 0.07f });
                    post.begin_scene(clear.x, clear.y, clear.z);
                }
                const Mat4 view_projection = view.view_projection();
                /*
                 * **順は「不透明 → 空 → 半透明」である**（2026-08-17。
                 * 空は下の `sky.draw` で描く）。
                 * 以前は空がいちばん先で、そのあと部屋・地形・実体がその上を塗り直していた
                 * ——ジオラマでは壁と卓が視界を覆うので**ほぼ全画素が無駄塗り**だった。
                 */
                /*
                 * VR の部屋と卓（§20。2026-08-14 に決めた）。**不透明の先頭**に描く。
                 * 深度を書くので、盤の床より手前にある天板がちゃんと盤を隠す。窓のガラスは
                 * 柄であって半透明ではない（`xr_room.cpp` は α を持たない）ので、
                 * ここで空より前に置いてよい。
                 * 一人称では `visible` が偽なので何も出ない（ゲーム空間の中に立つ見せ方）。
                 */
                if (view.stage_valid && vr_room.visible && vr_room_ready) {
                    vr_room_renderer.draw(view.projection * view.stage_view, vr_room);
                }
                const GLuint shadow_tex = want_shadow ? shadow.texture() : 0;
                renderer.begin(view_projection, lighting, fit.view_projection, shadow_tex, shadow.side());
                //! 影の描画と下拵えまでの分（ここより前で出たものはここに出る）。
                gl::probe_gl_errors("3D の下拵え");
                /*
                 * **描く量を報せる**（変わったとき＋3 秒ごと）。
                 *
                 * 「3D が出ない」には 2 つの筋がある——**GL が失敗している**のと、
                 * **そもそも積むものが無い**のと。上の検出は前者しか捉えないので、
                 * 後者を切り分ける目をここに置く。0 が並んでいれば描画より上流
                 * （フレームの取り込みか `terrain_view`）の話になる。
                 */
                {
                    static std::size_t last_slabs = static_cast<std::size_t>(-1);
                    static std::size_t last_boxes = static_cast<std::size_t>(-1);
                    /*
                     * **数が変わらなくても定期的に出す。** 変化したときだけにすると、
                     * 「1 度も来ていない」のか「ずっと同じ」のかが区別できない
                     * （2026-08-21 に実際に区別できず、判断を誤りかけた）。
                     * 3 秒に 1 行なら、遊びの邪魔にもログの読みにもならない。
                     */
                    static std::uint64_t beat = 0;
                    const bool changed = (terrain.slabs.size() != last_slabs)
                        || (terrain.boxes.size() != last_boxes);
                    if (changed || ((beat++ % 180u) == 0u)) {
                        last_slabs = terrain.slabs.size();
                        last_boxes = terrain.boxes.size();
                        /*
                         * **`slabs` / `boxes` だけを見ても分からない。** 地形の大半は
                         * プレハブ（`lib`）で建っていて、あの 2 つは 0 のまま絵が出る
                         * （2026-08-21 にエミュレータで実測。0 を見て「描くものが無い」と
                         * 早合点しかけた）。だから**走査したマスとプレハブの数も一緒に出す**。
                         */
                        std::fprintf(stderr,
                            "[hd2d] 描く量: マス %d（捨て %d）/ プレハブ %d / 板 %zu / 箱 %zu"
                            " / 下の板 %zu / 装飾 %d（画面 %dx%d）\n",
                            terrain.drawn_cells, terrain.culled_cells, terrain.lib_instances,
                            terrain.slabs.size(), terrain.boxes.size(), terrain.under_slabs.size(),
                            terrain.prop_count, view.width, view.height);
                        std::fflush(stderr);
                    }
                }
                /*
                 * カットアウェイ（P7）。**プレイヤの腰の高さを投影した所**を中心にする。
                 * 足元（z=0）にすると、抜ける範囲がプレイヤの下半分に偏る。
                 * 深度もここから作る（深度バッファは「隠している壁」の値なので使えない。§13）。
                 *
                 * **VR では掛けない**（`enable_cutaway == false`）。この抜きは画面座標の円で
                 * できているので、目ごとに別の円になって立体視が壊れる。そもそも VR では
                 * 「頭を動かして覗き込む」のが遮蔽への答えなので、要るかどうかから
                 * 実機で決め直す。
                 */
                std::vector<Cutaway> cutaways;
                if (enable_cutaway) {
                    /*
                     * カットアウェイ（P7 → P10 レビュー 4）。**視線が通っているときは抜かない。**
                     * 2026-08-09 に決めた:「壁の横にいる際に透過は不要。視線がブロックや
                     * オブジェクトで通らないときのみ透過を発生させて」。遮蔽の有無は
                     * `line_of_sight_blocked()` が地形の高さから判定する（描く前の CPU 側で決まる）。
                     */
                    const bool occluded = line_of_sight_blocked(terrain, camera.eye(), frame.player_gx, frame.player_gy);
                    /*
                     * **隠れているのはプレイヤだけではない**（P10 第 4 期。2026-08-10 に気づいた:
                     * 「視界の範囲内にあるモンスター、アイテムが壁ブロック等の 1 マス北にある場合
                     * （隠れて見えない場合）は透過して見えるように」）。
                     *
                     * 実体のぶんの穴は**プレイヤより小さく**（`kEntityCutawayScale`）。実体は 1 マス
                     * なので、同じ半径で抜くと部屋の壁が丸ごと消えて地形が読めなくなる。
                     */
                    static constexpr float kEntityCutawayScale = 0.42f;
                    /*
                     * **南にあるものだけ抜く**（2026-08-11 に決めた:「透過すべきはキャラクターより
                     * 南にあるオブジェクトで視界に被るオブジェクト。透過範囲は円形で問題ない。
                     * 真横のオブジェクトを透過しない」）。円と深度だけだと真横の壁の南面まで
                     * 網目になっていた。一人称では掛けない——カメラが真南とは限らないので、
                     * 「南」という向きの前提が成り立たない（従来どおり円と深度で抜く）。
                     */
                    /*
                     * **一人称では半平面を掛けない。**あちらはカメラが対象そのものの位置に
                     * 居るので「間」に何も入らない（掛けると 1 画素も抜けなくなる）。
                     */
                    const bool cutaway_camera_side_only = !first_person.active;
                    cutaways.reserve(VoxelRenderer::kMaxCutaways);
                    if (occluded) {
                        cutaways.push_back(make_player_cutaway(camera, frame, settings.cutaway_radius, cutaway_camera_side_only));
                    }
                    append_entity_cutaways(camera, terrain, entity_view,
                        settings.cutaway_radius * kEntityCutawayScale, cutaways, cutaway_camera_side_only);
                    /*
                     * **層 2 = 屋根外し**（2026-08-18 に決めた。`make_roof_cutaway` の註記）。
                     *
                     * 屋内かどうかの判定は**まだ無い**（地形から導く。`town_id` では決めない）。
                     * それが決まるまでは環境変数で強制できるようにしてある——値を実物で
                     * 合わせるのに要るし、**間取り型の町に屋根を架ける仕事より先に
                     * この層だけ確かめられる**ようにしておきたい。
                     *
                     * `HD2D_ROOF_CUTAWAY=<半径の倍率>`（例 `1.5`）。0 か未設定なら**何もしない**
                     * ＝従来と 1 画素も違わない。屋外で入れると周りの木や隣家の屋根まで抜ける。
                     */
                    static const float roof_scale = []() -> float {
                        const char *const env = std::getenv("HD2D_ROOF_CUTAWAY");
                        if ((env == nullptr) || (env[0] == '\0')) {
                            return 0.f;
                        }
                        const float value = std::strtof(env, nullptr);
                        std::fprintf(stderr, "[hd2d] HD2D_ROOF_CUTAWAY=%s: **屋根外しの層を常時入れます**"
                                             "（屋内判定が入るまでの調整用。屋外では周りの屋根も抜けます）\n",
                            env);
                        return (value > 0.f) ? value : 0.f;
                    }();
                    /*
                     * **覆いの町（岩天井）では常時入れる**（2026-08-18 その3。旧地獄街道）。
                     * 屋内判定は `TerrainView::covered`——意匠の表（データ）から導かれるので、
                     * `town_id` をコードで見ない約束はそのまま守られる。環境変数は
                     * 半径の値合わせ用の上書きとして残す。刳り抜く下限は表の `cave_min_z`
                     * （2 階建の町屋と提灯の頭上）。
                     * 半径は通常の透過の **3.0 倍**（2026-08-18 に決めた その3
                     * 「透過の範囲はいまの 1.5 倍に」——初版の 2.0 倍から広げた）。
                     */
                    /*
                     * **屋敷の中でも入れる**（デザイン4 第 2 段。紅魔館の前庭の彫り込み）。
                     * プレイヤが屋敷の外接矩形の中の歩けるマスに居るなら、覆いの町と同じ
                     * 屋根外しを入れる。刳り抜く下限は本館の壁の頭（3.2 マス）——
                     * 内壁（2 マス）と印は残り、屋根の蓋と塔が抜ける。
                     */
                    bool manor_inside = false;
                    if (town_plan.valid()) {
                        for (const auto &mbox : town_plan.manors) {
                            if ((frame.player_gx >= mbox[0]) && (frame.player_gx <= mbox[2])
                                && (frame.player_gy >= mbox[1]) && (frame.player_gy <= mbox[3])
                                && (town_plan.role_at(frame.player_gx, frame.player_gy)
                                    != TownRole::Manor)) {
                                manor_inside = true;
                                break;
                            }
                        }
                    }
                    const float cave_scale = (roof_scale > 0.f)
                        ? roof_scale
                        : ((terrain.covered || manor_inside) ? 3.0f : 0.f);
                    if ((cave_scale > 0.f) && (cutaways.size() < VoxelRenderer::kMaxCutaways)) {
                        cutaways.push_back(make_roof_cutaway(camera, frame,
                            settings.cutaway_radius * cave_scale,
                            terrain.covered ? terrain.cave_min_z : (manor_inside ? 2.3f : 1.f)));
                    }
                }
                renderer.begin_cutaways(cutaways.data(), cutaways.size());
                //! 床の板には掛けない（掛けると手前の地面に穴が開く）。
                renderer.set_cutaway_scale(0.f);
                renderer.draw_instanced(floor_gpu, terrain.slabs.data(), terrain.slabs.size());
                optional_render.floor_model.draw(view_projection,Mat4::identity(),lighting,view.eye,
                    fit.view_projection,shadow_tex,shadow.side(),nullptr,terrain.model_floors.size(),nullptr,0,terrain.model_floors.data());
                optional_render.floor_model.draw(view_projection,Mat4::identity(),lighting,view.eye,
                    fit.view_projection,shadow_tex,shadow.side(),nullptr,terrain.model_ground.size(),nullptr,0,terrain.model_ground.data());
                gl::probe_gl_errors("地面の板");
                /*
                 * 壁の下の地面（P7）。抜いた所から背景が見えないように敷いてある。
                 * **影は受けない**（P10 レビュー 10）。壁の真下なので受けると必ず真っ黒になり、
                 * 抜いた瞬間に「そこが何だったか」の色が読めない（2026-08-09 に気づいた）。
                 */
                renderer.set_shadow_scale(0.f);
                renderer.draw_instanced(floor_gpu, terrain.under_slabs.data(), terrain.under_slabs.size());
                optional_render.floor_model.draw(view_projection,Mat4::identity(),lighting,view.eye,
                    fit.view_projection,0,shadow.side(),nullptr,terrain.model_under_floors.size(),nullptr,0,terrain.model_under_floors.data());
                renderer.set_shadow_scale(1.f);
                if (library_ptr != nullptr) {
                    //! P10。ライブラリのバケット（地面はカットアウェイ 0・立つものは 1。中で切り替える）。
                    //! DCSS（D24）: 実体のあるマスの地形のオブジェクト（扉・階段・祭壇・像など）は、壁などを描いた後に深度を書かずに描き、実体を手前にする。
                    dcss_deferred.items.clear();
                    dcss_occupied.clear();
                    if (dcss_ui) {
                        for (const auto &cell : frame.cells) {
                            if ((cell.monster_id != 0) || (cell.object_id != 0) || ((cell.feature_flags & CELL_FEAT_PLAYER) != 0u)) {
                                dcss_occupied.insert(occupied_cell_key(cell.gx, cell.gy));
                            }
                        }
                    }
                    draw_library_color(renderer, *library_ptr, terrain, dcss_ui ? &dcss_occupied : nullptr, dcss_ui ? &dcss_deferred : nullptr);
                    if (dcss_ui && (std::getenv("HD2D_DCSS_LAYER_TRACE") != nullptr)) { //!< 検査用（tools/dcss/dc_d24_check.py）
                        static std::size_t last_deferred = ~static_cast<std::size_t>(0);
                        if (dcss_deferred.instance_count() != last_deferred) {
                            last_deferred = dcss_deferred.instance_count();
                            std::fprintf(stderr, "[dcss-layers] occupied=%zu deferred=%zu\n", dcss_occupied.size(), last_deferred);
                        }
                    }
                }
                renderer.set_cutaway_scale(1.f);
                renderer.draw_instanced(wall_gpu, terrain.boxes.data(), terrain.boxes.size());
                optional_render.wall_model.draw(view_projection,Mat4::identity(),lighting,view.eye,
                    fit.view_projection,shadow_tex,shadow.side(),nullptr,terrain.model_walls.size(),
                    cutaways.data(),cutaways.size(),terrain.model_walls.data());
                optional_render.wall_model.draw(view_projection,Mat4::identity(),lighting,view.eye,
                    fit.view_projection,shadow_tex,shadow.side(),nullptr,terrain.model_blocks.size(),
                    cutaways.data(),cutaways.size(),terrain.model_blocks.data());
                for(std::size_t i=0;i<kTerrainModelCount;++i) {
                    const bool solid=i==static_cast<std::size_t>(TerrainModelKind::Rubble);
                    optional_render.terrain_parts[i].draw(view_projection,Mat4::identity(),lighting,view.eye,
                        fit.view_projection,shadow_tex,shadow.side(),nullptr,terrain.model_parts[i].size(),
                        solid?cutaways.data():nullptr,solid?cutaways.size():0,terrain.model_parts[i].data());
                }
                gl::probe_gl_errors("壁の箱");
                /*
                 * 光る床（P7。積み残し #6）。**床の板の後に、同じ板で上書きする。**
                 * 手前にあるので抜く対象からは外す（溶岩がプレイヤを隠すことはない）。
                 */
                if (!emissive_slabs.empty()) {
                    renderer.set_cutaway_scale(0.f);
                    renderer.draw_instanced(floor_gpu, emissive_slabs.data(), emissive_slabs.size());
                }
                if (!dcss_deferred.items.empty() && (library_ptr != nullptr)) {
                    draw_library_deferred(renderer, *library_ptr, dcss_deferred);
                }
                // 実体は地形の後（地形を先に置くほうが素直）。
                /*
                 * **板は回らない**（2026-08-15。ビルボードから置き換えた）。
                 * 面は南（+y）へ固定で、`InstanceData` に回転が無いので向きを持てない
                 * （設計書 §11.5-6 の穴）。見下ろしの本線では真正面から見るので問題ないが、
                 * **一人称と VR では横から見ると薄い板の縁が見える。**
                 * 直すには実体ごとのヨーが要る——ここは素材ではなく描画側の宿題である。
                 */
                renderer.set_cutaway_scale(0.f);
                /*
                 * **足元のリング**（SQ-1。警戒度と照準）。**実体より前**に描く——
                 * 深度を書かない飾りなので、後から描くと板や字の上に乗ってしまう。
                 * 床は既に描いてあるので、輪はちゃんと地面に貼りついて見える。
                 */
                if (!entity_view.rings.empty() && ground_rings.ready()) {
                    ground_rings.draw(view_projection, entity_view.rings.data(), entity_view.rings.size());
                }
                const auto targeting_visual = build_targeting_visual(frame.targeting, view.eye,
                    entity_view.targeting_entities, world_seconds);
                optional_render.targeting.draw(view_projection, targeting_visual.area, true);
                optional_render.targeting.draw(view_projection, targeting_visual.dots, true);
                /*
                 * **エレメントスコープの四角**（TOband。決めたこと 2026-10-18）。原作がスコープ中のときだけ、
                 * コアが `element_scope` に見えているマスの色を積んで送る（空なら何も描かない＝ほかのコアの絵は変わらない）。
                 * 狙いの範囲と同じ描画器で、地面から少し浮かせた半透明の四角を重ねる。地面の凹凸があるコアは持ち上げた高さを足す。
                 */
                if (!frame.element_scope.empty()) {
                    const auto scope_quads = build_element_scope_quads(frame.element_scope,
                        [&terrain](int gx, int gy) { return terrain.ground_lift_at(gx, gy); });
                    optional_render.targeting.draw(view_projection, scope_quads, true);
                }
                /*
                 * **光の柱**。地形と輪の後・実体より前に描く。
                 *
                 * 後ろにすると板や字の上に光が乗って字が読めなくなる（加算なので白く飛ぶ）。
                 * 前にすれば、柱の中に立った実体は**柱に隠されずに**そのまま見える——
                 * 光は物の手前にあるものではない、という当たり前のほうへ倒す。
                 */
                if (!terrain.light_shafts.empty() && light_shafts.ready()) {
                    light_shafts.draw(view_projection, view.azimuth, terrain.light_shafts.data(),
                        terrain.light_shafts.size());
                }
                /*
                 * **実体は層の順に描く**（決めたこと 2026-09-23「タイルの向きに関わらず
                 * タイル描画の順番を 下 地面 オブジェクト アイテム モンスター キャラクター 上」）。
                 *
                 * 深度の順に任せると、板を画面へ正対させたときなどに、同じマスや隣のマスの
                 * 実体どうしがカメラからの近さで前後する（アイテムが敵の足に被さる・敵が
                 * キャラの頭を隠す）。そこで地形まで描いた深度を写しに取り、層を 1 つ描くたびに
                 * その写しへ戻す。後の層は地形にだけ隠れ、前の層の実体には隠れない——
                 * **後に描いた層が必ず上に出る**。層の中では従来どおり深度で前後する。
                 *
                 * 最後に深度を写しへ戻してから、全部の実体を**深度だけ**描き直す。空・霧・
                 * 被写界深度が実体の深度を読むので、ここを抜くと実体の上に空が塗られる。
                 * 層が 1 つしか無いときは写しを取らない（従来と同じ描き方になる）。
                 */
                std::array<std::vector<BillboardInstance>, kEntityLayerCount> layer_glyphs;
                std::array<bool, kEntityLayerCount> layer_used{};
                for (const SlabInstance &slab : entity_view.slabs) {
                    if (slab.slab >= 0) {
                        layer_used[static_cast<std::size_t>(slab.layer)] = true;
                    }
                }
                const bool draw_glyph_entities = !entity_view.glyphs.empty() && glyph_ready;
                if (draw_glyph_entities) {
                    for (std::size_t i = 0; i < entity_view.glyphs.size(); ++i) {
                        const auto layer = static_cast<std::size_t>((i < entity_view.glyph_layers.size())
                                ? entity_view.glyph_layers[i] : EntityLayer::Item);
                        layer_glyphs[layer].push_back(entity_view.glyphs[i]);
                        layer_used[layer] = true;
                    }
                }
                if (draw_player_model) {
                    layer_used[static_cast<std::size_t>(EntityLayer::Player)] = true;
                }
                const bool layered = std::count(layer_used.begin(), layer_used.end(), true) > 1;
                if (layered) {
                    post.save_scene_depth();
                }
                const Vec3 glyph_face{ entity_view.player_x, entity_view.player_y, 1.f };
                bool drew_layer = false;
                for (int layer = 0; layer < kEntityLayerCount; ++layer) {
                    if (!layer_used[static_cast<std::size_t>(layer)]) {
                        continue;
                    }
                    if (layered && drew_layer) {
                        post.restore_scene_depth();
                    }
                    drew_layer = true;
                    draw_entity_slabs(renderer, slab_library, entity_view, nullptr, layer);
                    if (draw_player_model && (layer == static_cast<int>(EntityLayer::Player))) {
                        optional_render.player_model.draw(view_projection, player_model_transform,
                            lighting, view.eye, fit.view_projection, shadow_tex, shadow.side());
                    }
                    /*
                     * **アスキー実体**。板の代わりに文字を立てる。
                     * `entity_view.glyphs` と `slabs` は排他なので、どちらか一方だけが描かれる。
                     *
                     * 軸拘束ビルボードは**常にカメラの方位へ正対する**ので、見下ろし・一人称・
                     * 視点回転・VR のどれでも追加の向き管理が要らない（板の `fixed_yaw` は不要）。
                     * VR だけは**キャラのマスへ正対**させる（頭に正対させると首を振るたびに
                     * 世界じゅうの字が回る。2026-08-14 に踏んだ話）。
                     *
                     * フルブライトで、影は落とさず受けない（§5.3）。抜き（カットアウェイ）も
                     * 掛けない——字は細く、後ろを隠さないので薄める理由が無い。
                     */
                    const auto &glyphs_here = layer_glyphs[static_cast<std::size_t>(layer)];
                    if (!glyphs_here.empty()) {
                        glyph_boards.draw(view_projection, lighting, view.azimuth, view.eye,
                            fit.view_projection, 0, shadow.side(), glyph_atlas.texture(), glyph_atlas.side(),
                            glyphs_here.data(), glyphs_here.size(),
                            vr_frame_ready ? &glyph_face : nullptr, /*fullbright=*/true);
                    }
                }
                if (layered) {
                    post.restore_scene_depth();
                    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                    draw_entity_slabs(renderer, slab_library, entity_view, nullptr);
                    if (draw_player_model) {
                        optional_render.player_model.draw_depth(view_projection, player_model_transform);
                    }
                    if (draw_glyph_entities) {
                        glyph_boards.draw(view_projection, lighting, view.azimuth, view.eye,
                            fit.view_projection, 0, shadow.side(), glyph_atlas.texture(), glyph_atlas.side(),
                            entity_view.glyphs.data(), entity_view.glyphs.size(),
                            vr_frame_ready ? &glyph_face : nullptr, /*fullbright=*/true);
                    }
                    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                }
                /*
                 * **一過性の重ね書き**（SQ-2。ダメージの数字・飛跡・爆風・聞き耳の `*`・
                 * 照準）。実体の字と**同じ道具で別の列**を描く——実体が板でも出るように。
                 * 実体より高い所（`kOverlayLift`）に置いてあるので、敵の板に埋まらない。
                 */
                optional_render.targeting.draw(view_projection, targeting_visual.reticles, false);
                if (!overlay_glyphs.empty() && glyph_ready) {
                    const Vec3 face{ entity_view.player_x, entity_view.player_y, 1.f };
                    glyph_boards.draw(view_projection, lighting, view.azimuth, view.eye,
                        fit.view_projection, 0, shadow.side(), glyph_atlas.texture(), glyph_atlas.side(),
                        overlay_glyphs.data(), overlay_glyphs.size(),
                        vr_frame_ready ? &face : nullptr, /*fullbright=*/true);
                }
                /*
                 * 空（2026-08-11 に決めた）。**不透明を全部描いた後・雲より前**に、
                 * 深度を読んで（LEQUAL）書かずに描く（2026-08-17。§15.5。`sky_dome.h`）。
                 * まだ誰も塗っていない画素だけが通るので、**絵は前と 1 画素も変わらない**
                 * まま、隠れる所を塗らずに済む。
                 * 地下では `SkyState::visible` が偽なので `draw()` は何もしない。
                 */
                /*
                 * **画調が TRON の黒空なら、ここから下の 5 つを 1 つも描かない**
                 * 雲も霧も湯気も「もやもやした塊」で
                 * あって線画ではないので、1 つでも出ていると画調が崩れる。
                 * 標準では `look_draws_sky` が常に真なので、**従来と同じ順で全部描く**。
                 */
                if (look_draws_sky(settings.scene_look, settings.tron_sky)) {
                    sky.draw(view_projection, view.eye, sky_state);
                    /*
                     * ブロックの雲（2026-08-11 に決めた）。**不透明な世界を全部描いた後**。
                     * 半透明なので、深度を読んで塔の向こうに隠れつつ、下の空が透けて見える。
                     * 地下では `SkyState::visible` が偽なので何もしない（空と同じ約束）。
                     * 層はカメラより上（高さ 40）なので、**見下ろしでは視錐台が全タイルを弾いて
                     * 何も出ない**（「見下ろし時は雲が見えない高さに」と決めた）。
                     */
                    clouds.draw(view_projection, view.eye, sky_state, world_seconds);
                    /*
                     * 湖の霧（デザイン4・2026-08-18。紅魔館の霧の湖）。深水のマスは
                     * `terrain_view` が意匠の表を見て積んである（無い町では空のまま）。
                     * 空の雲と同じく半透明なので、不透明の後・`end_scene()` の前。
                     */
                    clouds.draw_patch(view_projection, terrain.mist_cells, terrain.mist_height, sky_state);
                    /*
                     * **地表の霧**（デザイン8 その2・2026-08-19。こう決めた——「きりであれば
                     * 半透明にして雲と同じく動かそう」）。空の雲と同じ網を低い所へ敷き、
                     * 同じ向きへ**半分の速さ**で流す。意匠が高さを持つ町だけ。
                     */
                    clouds.draw_ground(view_projection, view.eye, sky_state, world_seconds,
                        terrain.ground_mist_height, terrain.ground_mist_alpha, 0.5f);
                    /*
                     * 配管の蒸気（デザイン7 その2・2026-08-19。河童のバザー）。継ぎ手の口は
                     * `terrain_view` が意匠の旗を見て積んである（無い町では空のまま）。
                     * 雲・霧と同じ半透明の描き手なので、ここで一緒に描く。
                     */
                    clouds.draw_steam(view_projection, terrain.steam_jets, sky_state, world_seconds);
                } //!< 画調の門（`look_draws_sky`）
                /*
                 * **攻撃の視覚効果**（矢・弾・投げた物・魔法の玉・爆発）。
                 * 不透明を全部描いた後・`end_scene()` の前。深度は読む（壁の向こうの爆発は壁に隠れる）。
                 * 煙は半透明で、奥から手前の順に混ぜる。自己発光の強い粒をブルームが掴む（線形の HDR）。
                 * 投げた物の板は床のアイテムと同じ `VoxelRenderer` で描く（同じ光・同じ板）。
                 * 起きていなければ何も描かない。
                 */
                if (!attack_fx_frame.voxels.empty() && optional_render.attack_fx.ready()) {
                    (void)optional_render.attack_fx.draw(view_projection, view.eye, lighting, fit.view_projection,
                        shadow_tex, shadow.side(), attack_fx_frame.voxels.data(), attack_fx_frame.voxels.size());
                }
                if (!attack_fx_frame.slabs.empty()) {
                    (void)draw_fx_slabs(renderer, attack_fx_frame.slabs, attack_fx_slab_resolver);
                }
                gl::probe_gl_errors("攻撃の視覚効果");
                /*
                 * **空中に浮かぶ埃**。半透明なので不透明の後、
                 * `end_scene()` の前——**線形の HDR へ加算する**ので、ブルームと
                 * 被写界深度が後から掛かる（浮遊物が光の中で滲む）。
                 *
                 * 色は環境光を掛ける（夜は青く・松明の間では暖かい）。時計は世界の秒を
                 * そのまま渡すので、`--motion-time=` で止めれば撮り比べが揃う。
                 */
                {
                    DustParams dust_params;
                    dust_params.amount = settings.dust;
                    if (weather_view.active()) {
                        //! 天候を申告するコアだけ。雨・雪が降るあいだは埃を雨・雪に譲り、降っていなければ風で横に流す。
                        dust_params.amount *= 1.f - std::min(1.f, weather_view.precipitation());
                        dust_params.wind_x = static_cast<float>(std::fmod(weather_view.wind_x, static_cast<double>(dust_params.span)));
                        dust_params.wind_y = static_cast<float>(std::fmod(weather_view.wind_y, static_cast<double>(dust_params.span)));
                    }
                    /*
                     * **粒は自分でぼけた大きさへ広がる**（2026-08-23 に決めた）。
                     * 錯乱円の測りは後処理と同じ `measure_dof()`——ここで別に測ると
                     * 粒だけ違うぼけ方をする。
                     */
                    const Vec3 dust_to_target = camera.target - camera.eye();
                    const DofView dust_dof = measure_dof(post_params, view_projection, camera.target.x,
                        camera.target.y, dust_to_target.x, dust_to_target.y);
                    dust.draw(view_projection, view.eye, lighting.sky_color, world_seconds, dust_params,
                        dust_dof, camera.target, view.height);
                    //! 雨と雪（降っていなければ何も描かない）。
                    weather_particles.draw(view_projection, camera.target, lighting.sky_color, world_seconds, weather_view,
                        WeatherParticleParams{}, view.width, view.height);
                }
                //! ここまでが 3D の描画。**段の名前つきで**吸う（gl_core.h の註）。
                gl::probe_gl_errors("3D の描画");
                post.end_scene(view.width, view.height, vp_x, vp_y, target_fbo);
                if (gpu_sync) {
                    glFinish();
                }
                t4 = clock::perf();

                /*
                 * ポスト処理 → 出し先（フラットは窓・VR は目の swapchain）。
                 * フォグの色は**その時刻の空の色**（固定色にすると夕焼けの中で 1 か所だけ昼になる）。
                 * 地下では空が無いので、`make_scene_lighting()` が返す暗い `sky_color` がそのまま
                 * 「奥の闇」として効く。
                 */
                post_params.fog_color = lighting.sky_color;
                /*
                 * **VR ではフォグの距離をメートルへ直す**（§20）。`fog_start` も
                 * `fog_range` も「マス」で書かれているのに、VR の視点空間はメートルである。
                 * 直さないと、1 マス 3m の一人称で**5 マス先から霧が掛かって**世界が消える
                 * （「完全にゲーム空間に入り込む」と決めたの妨げになっていた）。
                 */
                if (view.stage_valid) {
                    post_params.fog_start = kFogStartCells * vr_board.tile_m;
                    post_params.fog_range = kFogRangeCells * vr_board.tile_m;
                }
                //! 被写界深度の強さは機能メニューが持つ（P10 レビュー 4）。
                post_params.dof_strength = settings.dof_strength;
                /*
                 * **画面全体の色処理**（2026-08-23 に決めた）。機能メニュー ＞ 効果が持つ。
                 * ビネットの強さ・HDR 風・露出はここで送る（セピアは LUT なので下で焼く）。
                 * **画調（TRON）より前に置く**——TRON はビネットを自分の値へ寄せるので、
                 * 後ろに置くと遊ぶ人の値がそれを塗り潰す。
                 */
                post_params.vignette_strength = settings.vignette_strength;
                post_params.hdr = settings.hdr;
                post_params.exposure = settings.exposure;
                /*
                 * **画調**（軸 E）。ブルーム・フォグ・ビネットを TRON へ寄せる。
                 * **`fog_color` を入れた後・VR の距離換算の後**に呼ぶこと
                 * （`apply_look_to_post` の注記。順を逆にすると黒いはずの奥が空の色に戻る）。
                 * 標準では 1 つも触らない。
                 */
                apply_look_to_post(settings.scene_look, settings.tron_sky, post_params);
                //! 焦点は視点から注視点までの**実距離**（`Camera::distance` は水平距離）。
                const Vec3 to_target = camera.target - camera.eye();
                /*
                 * 被写界深度は**キャラからの前後の隔たり**で効く（P10 レビュー 8）。
                 * 画素をワールドへ戻す行列・キャラの水平位置・**奥行きを測る軸**を渡す。
                 * 軸はカメラの水平前方向（視点 → 注視点の xy）で、左右にはぼけが効かない。
                 */
                /*
                 * 一人称（おまけ）と VR では**被写界深度を切る**。
                 *
                 * 一人称: このぼけは「キャラからの前後の隔たり」で効く（P10 レビュー 8）。
                 * キャラがカメラそのものなので、奥行きを測る軸（視点 → 注視点の xy）が
                 * **長さ 0 に縮退**し、数マス先から先が一様にぼけて霧のようになる（実測）。
                 *
                 * VR: 焦点は**目が選ぶもの**になるので、画面側で選ぶと不快になる。本物の
                 * 立体視が疑似ミニチュア効果を置き換える。
                 *
                 * どちらも**設定そのものは触らない**ので、抜ければ元の強さのまま戻る。
                 */
                PostFlags scene_post = settings.post;
                //! 闘技場の観戦も切る（設計書 §4.6。引いた画で試合がぼけるため）。同じく設定は触らない。
                if (first_person.active || !enable_dof || arena_watch) {
                    scene_post.dof = false;
                }
                post.resolve(scene_post, post_params, view.z_near, view.z_far,
                    std::sqrt(dot(to_target, to_target)), view.width, view.height, vp_x, vp_y,
                    view_projection, camera.target.x, camera.target.y, to_target.x, to_target.y, target_fbo);
                gl::probe_gl_errors("ポスト処理");
            };

            if (vr_frame_ready) {
                /*
                 * **VR（M1）。** 左右の目それぞれの視点で同じシーンを 2 回描き、
                 * ランタイムの swapchain へ直接出す。UI はまだ載せない（M2）。
                 *
                 * 目の寸法はランタイムの推奨値に従うので、画面用の FBO はそちらへ合わせる
                 * （両目とも同じ寸法なので、作り直しは最初の 1 回だけ）。
                 */
                if (!post.resize(vr_frame.views[0].width, vr_frame.views[0].height, err)) {
                    show_message("VR の FBO を作れませんでした:\n" + err);
                    result = 1;
                    break;
                }
                for (int eye = 0; eye < vr_frame.view_count; ++eye) {
                    GLuint target = 0;
                    int vp_x = 0;
                    int vp_y = 0;
                    if (xr.valid()) {
                        if (!xr.bind_eye(eye)) {
                            continue;
                        }
                        target = xr.eye_framebuffer();
                    } else {
                        //! 疑似 HMD（§9）。窓の左半分／右半分へ並べて出す。
                        vp_x = (eye == 0) ? 0 : (screen_w - vr_frame.views[eye].width);
                    }
                    draw_scene(vr_views[eye], target, vp_x, vp_y,
                        /*enable_cutaway=*/false, /*enable_dof=*/false);
                    /*
                     * 疑似 HMD の板（§20）。実物ではコンポジタがクワッドレイヤとして
                     * 重ねてくれるが、こちらにはそれが無いので**同じ置き場所へ矩形を
                     * 描いて代用する**。貼るのは 1 フレーム前に作った面（UI は目の絵より
                     * 後に描かれるので、この時点では前の回のものしか無い）。
                     *
                     * ポスト処理の**後**に描くのは、文字をにじませないため（実物の
                     * クワッドレイヤも合成の後に乗る）。
                     */
                    if (!xr.valid() && options.vr_fake && vr_room_ready && (vr_fake_ui.texture() != 0)) {
                        const Mat4 stage_vp = vr_views[eye].projection * vr_views[eye].stage_view;
                        const float inv_w = 1.f / static_cast<float>(std::max(1, vr_fake_ui.width()));
                        const float inv_h = 1.f / static_cast<float>(std::max(1, vr_fake_ui.height()));
                        glViewport(vp_x, vp_y, vr_views[eye].width, vr_views[eye].height);
                        for (std::size_t i = 0; i < vr_panels.size(); ++i) {
                            const xr::UiPanel &panel = vr_panels[i];
                            xr::PanelPose pose;
                            pose.center = Vec3{ panel.center[0], panel.center[1], panel.center[2] };
                            for (int k = 0; k < 4; ++k) {
                                pose.orientation[k] = panel.orientation[k];
                            }
                            pose.width_m = panel.width_m;
                            pose.height_m = panel.height_m;
                            //! **v は下が 0**（GL のテクスチャ）。矩形は左上原点なので引っくり返す。
                            const float u0 = static_cast<float>(panel.rect_x) * inv_w;
                            const float u1 = static_cast<float>(panel.rect_x + panel.rect_w) * inv_w;
                            const float v0 = 1.f - (static_cast<float>(panel.rect_y + panel.rect_h) * inv_h);
                            const float v1 = 1.f - (static_cast<float>(panel.rect_y) * inv_h);
                            vr_room_renderer.draw_panel(stage_vp, vr_fake_ui.texture(), pose, u0, v0, u1, v1);
                        }
                    }
                }
            } else {
                RenderView flat_view;
                flat_view.view = camera.view();
                flat_view.projection = camera.projection();
                flat_view.eye = camera.eye();
                flat_view.azimuth = camera.azimuth();
                flat_view.width = layout.scene.w;
                flat_view.height = layout.scene.h;
                camera.depth_range(flat_view.z_near, flat_view.z_far);
                draw_scene(flat_view, 0, scene_gl_x, scene_gl_y, /*enable_cutaway=*/true, /*enable_dof=*/true);
            }
            if (gpu_sync) {
                glFinish();
            }
            const Uint64 t5 = clock::perf();

            ms_meaning = static_cast<double>(t1 - t0) * 1000.0 / static_cast<double>(perf_freq);
            ms_build = static_cast<double>(t2 - t1) * 1000.0 / static_cast<double>(perf_freq);
            ms_shadow = static_cast<double>(t3 - t2) * 1000.0 / static_cast<double>(perf_freq);
            ms_submit = static_cast<double>(t4 - t3) * 1000.0 / static_cast<double>(perf_freq);
            ms_post = static_cast<double>(t5 - t4) * 1000.0 / static_cast<double>(perf_freq);
        }

        /*!
         * @brief 窓へ何も出さない回か。
         * @details 機内では 2D の Surface は**誰にも見えない**。見えない Surface の
         * swap が何に同期するかは機と OS 版で揺れるので、鏡窓も swap も丸ごと飛ばす
         * ——歩調は `xrWaitFrame` だけが握るべきである。**VR が立たなかった回は
         * 今までどおり出す**（2D パネルとして生きる＝切り分けの退路）。
         * イベントの pump は毎フレームのままで、ここは触らない。
         */
#if defined(__ANDROID__)
        const bool skip_window_present = xr.valid();
#else
        constexpr bool skip_window_present = false;
#endif
        /*
         * (d-4) VR のフレームの締め。
         *
         * **鏡窓へ写してから畳む。** `end_frame()` が swapchain を返してしまうので、
         * 順序を逆にすると窓が真っ黒になる。窓には左目を出す（この上に UI が乗るので、
         * フラットと同じ見た目で `--shot=` の検証路がそのまま生きる）。
         *
         * 地図が出ていない間（タイトル・店）は `vr_frame_ready` でも絵を描いていないので、
         * 鏡は写さず、レイヤも積まずに畳む（`end_frame` が中で判断する）。
         */
        if (vr_frame_ready && xr.valid() && !skip_window_present) {
            xr.blit_mirror(0, 0, 0, screen_w, screen_h);
        }

        /*
         * (e) UI（P8）。**下敷き（`paint`）を先に、文字（`text`）を後に流す。**
         * どちらも**ポスト処理は通さない**（合成の後、既定のフレームバッファへ直に描く。
         * 通すと文字も枠もにじんでぼける。`post_process.h` の順序の表）。
         */
        /*
         * VR では**文字 UI をクワッドレイヤの板へ**描く（§7）。窓ではなく板の FBO へ
         * 向けるだけで、`ui_paint`/`text_overlay`/`game_hud`/`floor_cutin` は無改修で載る。
         * 透明で消してあるので、描かなかった所はシーンが透ける。
         *
         * 板が作れなかった（あるいは VR でない）ときは 0 が返り、今までどおり窓へ描く。
         */
        /*
         * VR では文字を板へ描く。実物はクワッドレイヤの swapchain（`bind_ui`）、
         * 疑似 HMD は自前の面（`vr_fake_ui`）——**窓には出さない**。窓そのものが
         * HMD の代わりなので、フラットの UI を重ねると盤が下段パネルの裏に隠れる（罠 21）。
         */
        GLuint ui_target = 0u;
        if (vr_frame_ready && xr.valid()) {
            ui_target = xr.bind_ui();
        } else if (vr_frame_ready && options.vr_fake && vr_fake_ui.ensure(screen_w, screen_h)) {
            ui_target = vr_fake_ui.bind();
        }
        paint.begin(screen_w, screen_h);
        text.begin(screen_w, screen_h);
        images.begin(screen_w, screen_h);
        /*
         * **疑似 HMD では窓そのものが HMD の代わり**なので、フラットの UI は窓へ
         * 重ねない（設計書 §9・罠 21）。ただし**板の面（`vr_fake_ui`）へは描く**
         * ——そこへ作った絵を空中の板に貼って、実機なしで置き場所を見るためである（§20）。
         * 実物の `--vr` では窓はただの鏡なので、今までどおり窓にも重ねる。
         */
        /*
         * **VR では板の下敷きを出さない**（2026-08-14 に決めた「VR モードの場合は
         * パネル背景は無し。背景は透過させて」）。字だけが空中に浮き、描かなかった所は
         * 世界が透ける。背面の 1 枚絵（タイトル画・パネルの絵）も同じ理由で外す。
         */
        set_panel_transparent(vr_frame_ready);
        if (!options.vr_fake || (ui_target != 0u)) {
            /*
             * 一人称の VR では**出さないと決めた部位を消す**（2026-08-14 に決めた
             * 「FPS モードの際の各パネルの ON/OFF」）。割り付けの矩形を空にすれば、
             * 描く側は「置き場所が無い」と見て何も描かない——描く側を触らずに済む。
             */
            UiLayout vr_layout = layout;
            if (vr_frame_ready && first_person.active) {
                const auto hide = [&](xr::PanelSlot slot, RectPx &rect) {
                    if (!settings.vr_fps_show[static_cast<int>(slot)]) {
                        rect = RectPx{};
                    }
                };
                hide(xr::PanelSlot::Status, vr_layout.status_col);
                for (int i = 0; i < kUiSubPanels; ++i) {
                    hide(static_cast<xr::PanelSlot>(static_cast<int>(xr::PanelSlot::Sub1) + i), vr_layout.sub[i]);
                }
                hide(xr::PanelSlot::Minimap, vr_layout.minimap);
                hide(xr::PanelSlot::Message, vr_layout.message_bar);
                hide(xr::PanelSlot::Prompt, vr_layout.prompt_bar);
                hide(xr::PanelSlot::Bottom, vr_layout.bottom_bar);
            }
            /*
             * **2D のアスキー地図**。
             * `draw_game_ui` より**先**に描く——板は自分の下敷きを持っているので、
             * 後から描けばちゃんと地図の上に乗る。
             *
             * Term の写しが出ている画面（タイトル・店・メニュー）では描かない。
             * あそこには地図が無いので、古いマスが残って見えると嘘になる。
             */
            if ((settings.main_panel == Hd2dSettings::MainPanel::Ascii) && !vr_frame_ready
                && map_visible) {
                /*
                 * マスは**可視窓を決めたときと同じもの**を使う（上の `ascii_glyphs`）。
                 * 専用のアトラスは自分で `begin`／`flush` する必要がある——UI の文字とは
                 * 別のテクスチャなので、同じ積みには載らない。
                 */
                TextOverlay &glyphs_for_map = (ascii_text_px > 0) ? ascii_text : text;
                if (ascii_text_px > 0) {
                    glyphs_for_map.begin(screen_w, screen_h);
                }
                (void)draw_ascii_map_panel(paint, glyphs_for_map, vr_layout.scene, frame, &hud_state.targeting, world_seconds);
                /*
                 * `--shot-after`（正の値）は「世界が描かれたフレーム」を数える（下の `shot_gate`）。
                 * 3D では地形の組み立てで数えるが、2D アスキーのメインパネルは 3D を 1 枚も
                 * 描かない（`skip_scene`）ので、ここで数えないと撮影が永遠に来なかった
                 * （2026-09-25。ASCII で --shot-after=600 を待つ検証が時間切れになり、その
                 * `timeout` がコアごと止めて「ゲームコアが予期せず停止しました」に見えていた）。
                 */
                ++frames_with_terrain;
                if(hud_state.targeting.visible()||hud_state.modern.active()||(settings.modern_ui&&menu_drawn)) {
                    paint.flush();glyphs_for_map.flush();
                } else if (ascii_text_px > 0) {
                    /*
                     * **下敷き（`paint`）より後に流す。**`paint` はこの後まとめて流されるので、
                     * ここで字を流すと下敷きが字の上に来る……ように見えるが、
                     * `UiPaint` と `TextOverlay` は別の描画で、`paint` の流しは
                     * 深度を持たない上書きである。だから**字は `paint.flush()` の後**に
                     * 流さなければならない。ここでは積むだけにして、流すのは下の 1 か所に任せる。
                     */
                    ascii_text_pending = true;
                }
            }
            /*
             * **新 UI ではメニューを開いている間もサブパネルを描く**（決めたこと 2026-09-26
             * 「メニュー表示中サブパネルが消える。出したままに」）。機能メニュー（コマンドメニュー・
             * 設定）は `fixed_menu_rect()` のまま上に重ね、隠れる所は隠れてよい。機能メニューが
             * 前にあるときは新 UI の窓（後ろに残っている持ち物一覧など）だけは描かない（従来どおり）。
             * パネルの字が機能メニューの上に出ないよう、ここで一度流す。オリジナル UI は従来どおり。
             */
            /*
             * DCSS の区画（ステータス・メッセージ・ミニマップ・タブの欄）。**窓や HD2D の部品より先に**描いて流す
             * （原作は窓の下にも区画を描き続ける）。自分の字は別の字なので、ここで下敷きのあとに流す。
             */
            if (dcss_ui && dcss_ui->layout_ok() && dcss_ui->in_game() && !vr_frame_ready) {
                dcss_ui->draw(paint, images, screen_w, screen_h);
                dcss_ui->flush_text();
            }
            const bool menu_in_front = feature_menu.tab_navigation_active() || (hud_state.modern.active() && feature_menu.is_open());
            const bool hud_under_menu = settings.modern_ui && modern_window_keeps_hud(frame);
            if (!menu_in_front || hud_under_menu) {
                draw_game_ui(paint, text, vr_layout, frame, hud_state,
                    vr_frame_ready ? nullptr : (settings.backdrops ? &backdrops : &backdrops_orb_only),
                    !feature_menu.is_open());
                if (settings.modern_ui && menu_drawn) {
                    paint.flush();
                    text.flush();
                }
                /*
                 * **カーソルの右下の小さなポップアップ**（TOband。原作が狙う・調べるときの 1 行目に出す
                 * マスの属性の値とモンスターの属性の名前。`TargetingState::info`）。コアが送ったときだけ立つので、
                 * ほかのコアでは `has_cursor_info()` が偽のまま何も描かない。カーソルのマスの画面位置は
                 * 3D では `camera.project`、2D のアスキー地図では `map_projection` から求める。
                 */
                if (hud_state.targeting.has_cursor_info() && !vr_frame_ready) {
                    const TargetingPoint cursor = hud_state.targeting.state().selected;
                    int anchor_x = 0;
                    int anchor_y = 0;
                    bool anchored = false;
                    if (settings.main_panel == Hd2dSettings::MainPanel::Ascii) {
                        const TargetingMapProjection &grid = hud_state.targeting.map_projection;
                        if ((grid.cell_w > 0) && (grid.cell_h > 0)) {
                            anchor_x = grid.x + ((cursor.x - grid.cam_x + (grid.cols / 2)) * grid.cell_w) + (grid.cell_w / 2);
                            anchor_y = grid.y + ((cursor.y - grid.cam_y + (grid.rows / 2)) * grid.cell_h) + (grid.cell_h / 2);
                            anchored = true;
                        }
                    } else {
                        float projected_x = 0.f;
                        float projected_y = 0.f;
                        if (camera.project(Vec3{ static_cast<float>(cursor.x) + 0.5f, static_cast<float>(cursor.y) + 0.5f, 0.f },
                                projected_x, projected_y)) {
                            anchor_x = static_cast<int>(std::lround(projected_x - 0.5f)) + layout.scene.x;
                            anchor_y = static_cast<int>(std::lround(projected_y - 0.5f)) + layout.scene.y;
                            anchored = true;
                        }
                    }
                    if (anchored) {
                        hud_state.targeting.draw_cursor_info(paint, text, vr_layout, hud_state.modern_theme, anchor_x, anchor_y);
                    }
                }
            }
            /*
             * DCSS の窓の部品（段 U3。メニュー・説明・作成の格子ほか）。HD2D の部品・DCSS の区画より**あと**（上）に描く
             * （原作は窓の下にも区画を描き続け、窓は暗幕をかけて重なる）。下の層の四角・字を先に流す。
             * 描きながら決まる送り先（`menu_scroll` ほか）は、ここで取り出して送る。
             */
            if (dcss_ui && !vr_frame_ready) {
                paint.flush();
                text.flush();
                dcss_ui->draw_windows(paint, images, screen_w, screen_h);
                for (const std::string &line : dcss_ui->take_window_outgoing()) {
                    (void)link.send_webtiles_input(line);
                }
            }
        }
        /*
         * IME の未確定文字列（変換中の文字）。**素の IME は候補一覧しか描かない**ので、
         * ここが描かないと「候補は出るのに何を打っているか見えない」ことになる
         * （2026-08-13 に気づいた）。
         *
         * 置く所は入力欄の桁（`ime_text_rect`。コアのカーソルから採ってある）。
         * 下敷きを敷くのは、**下の行の字と重なると読めない**ため。下線は「まだ確定して
         * いない」の印で、これは日本語入力の共通の作法である。
         */
        if (frame.text_input_active && !ime_edit_text.empty() && (ime_text_rect.w > 0)) {
            const int w = std::max(text.cell_w(), text.measure(ime_edit_text));
            const RectPx box{ ime_text_rect.x, ime_text_rect.y, w, ime_text_rect.h };
            paint.rect(box, PaintColor{ 0.09f, 0.09f, 0.12f, 0.94f });
            text.draw(box.x, box.y, ime_edit_text, TextColor{ 1.f, 1.f, 1.f, 1.f }, box.w);
            //! 下線。カーソルと同じ黄色（`draw_term_mirror` の caret と揃える）。
            paint.rect(RectPx{ box.x, (box.y + box.h) - 2, box.w, 2 },
                PaintColor{ 1.f, 0.9f, 0.2f, 1.f });
        }
        /*
         * 戦闘の見せ場。**地図の上・HUD の下**に描く。
         * 3D のポスト処理は通さない（にじませたくない）。
         * - 命中と飛道 … マスを画面へ落として点と線で描く
         * - 被弾 … 地図の矩形いっぱいに属性の色を敷く（**画面全部ではない**。
         *   サブパネルまで染めると数字が読めなくなる）
         */
        if (frame_shows_map(frame)) {
            draw_combat_fx(paint, combat_fx, camera, layout.scene, settings.damage_flash);
            //! 攻撃の視覚効果の光の波（視界内の全員にかかる魔法）。被弾のフラッシュと同じ段。
            draw_attack_fx_waves(paint, attack_fx_frame.screen_waves, layout.scene);
        }
        /*
         * バーチャルパッド（2026-08-12 に決めた）。ゲームの UI の上・メニューの下
         * （メニューの字がボタンで隠れない。ボタンでのメニュー操作は描画順と無関係に効く）。
         * **カットインの間はここでは描かない**——幕は本体の flush の後（最上位）に
         * 描かれるので、ここで描いたパッドは幕の下に隠れる。幕を進める決定はタッチでは
         * A ボタンだけで、見えないと押せない（2026-08-12 に気づいた）。幕の上に描く側は
         * `floor_cutin.draw` の直後にある。
         *
         * **見るのは `blocks_input()` ではなく `active()`**（2026-08-28）。
         * 行先の地図待ち（`Pending`）と幕が薄れる `Reveal` は入力を止めないが、
         * **幕は出ている**——`blocks_input()` で分けると、その 2 つの段でパッドが
         * 幕の下に沈む。`Pending` は `-more-` を越えるキーを待つ段なので、
         * そこでパッドが見えないと**黒い画面のまま先へ進めなくなる**。
         */
        /*
         * --- コマンドの一覧の下敷き（2026-08-29 に決めた） ---
         * 「コマンドメニュー背景にコアのマップが表示される。消して」。命令の名札は
         * 2 列に細かく並ぶので、後ろに地図の字と絵があると**どこまでが名札か**が
         * 読めない。窓いっぱいに下敷きを敷いて世界を隠す。
         *
         * **バーチャルパッドより先に敷く。**後に敷くとボタンまで隠れ、触りだけの機体で
         * 一覧を動かす手が無くなる（幕の上にボタンが残るのが正しい）。
         * **設定の節では敷かない**——あちらは世界を見ながら値をいじる画である。
         *
         * **新 UI では敷かない**（決めたこと 2026-09-26「すべてのメニューで背景にマップを
         * 残して」）。新 UI の一覧はパネルの地の上に名札を並べるので、後ろに地図があっても
         * 名札の境目は読める。オリジナル UI は名札を地図に直に並べるので従来どおり敷く。
         */
        if (feature_menu.showing_commands() && !settings.modern_ui) {
            paint.rect(RectPx{ 0, 0, screen_w, screen_h }, PaintColor{ 0.03f, 0.03f, 0.05f, 1.f });
        }
        if (!floor_cutin.active()) {
            if (dcss_ui) {
                //! DCSS（段 5）: 仮想パッドのボタンの名前も、パッドと同じ割り当て表（原作の割り当て）から出す。
                PadBinds dcss_vpad_binds;
                std::vector<PadCommand> dcss_vpad_commands;
                dcss_ui->pad().fill_vpad_view(dcss_vpad_binds, dcss_vpad_commands);
                vpad.draw(paint, text, settings.vpad, usable_area(), pad_macros, chord_mask, dcss_vpad_binds, dcss_vpad_commands);
            } else {
                vpad.draw(paint, text, settings.vpad, usable_area(), pad_macros, chord_mask, settings.pad, pad_commands);
            }
        }
        //! 機能メニューは**いちばん上**（開いている間はこれが操作の対象なので）。
        feature_menu.draw(paint, text, layout, settings);
        if (menu_handoff && !feature_menu.is_open()) {
            menu_handoff->draw(paint, text, layout, settings);
        } else if (modern_handoff_drawn) {
            modern_handoff->draw(paint, text, layout, frame, hud_state.modern_theme);
        }

        /*
         * ここから下は**調べるための重ね書き**（`--text`）。P0〜P7 で見ていた数字である。
         * 本物の UI が入った以上、常に出しておくと状態列と重なって読めない。
         */
        if (options.text_map) {
            draw_frame_as_text(text, frame, title_line, screen_w, screen_h, fps, frame_bytes, true);
        }
        if (options.text_map && !showing_term) {
            /*
             * **実体が正しいマスに立っているかを目で確かめるための印**（P4 の検証）。
             * プレイヤのマスの中心を投影した所に `+` を置く。ビルボードの足元と重なっていれば
             * 「絵と格子が一致している」と言える（必守制約 3）。
             */
            float mark_x = 0.f;
            float mark_y = 0.f;
            const Vec3 centre{ static_cast<float>(frame.player_gx) + 0.5f, static_cast<float>(frame.player_gy) + 0.5f, 0.f };
            if (camera.project(centre, mark_x, mark_y)) {
                // **`scene.x/y` を足す**（P8）。カメラが返すのは 3D の矩形の中の画素である。
                text.draw(layout.scene.x + static_cast<int>(mark_x) - (text.cell_w() / 2),
                    layout.scene.y + static_cast<int>(mark_y) - (text.cell_h() / 2),
                    "+", TextColor{ 1.f, 0.2f, 0.2f, 1.f });
            }
        }
        if (options.text_map) {
            char line[256]{};
            std::snprintf(line, sizeof(line),
                "cam pitch=%.0f fov_x=%.0f dist=%.1f cell=%.0fpx  view=%dx%d (N%.1f S%.1f W%.1f E%.1f)",
                camera.pitch * 57.2958f, camera.fov_x * 57.2958f, camera.distance, settings.camera_cell_px,
                ui_state.view_w, ui_state.view_h, view_window.north, view_window.south,
                view_window.west, view_window.east);
            text.draw_cell(8, 8, 0, 4, line, kDimColor);
            text.draw_cell(8, 8, 0, 5, floor_meaning_summary(meaning), kDimColor);
            std::snprintf(line, sizeof(line),
                "boxes=%zu slabs=%zu props=%d lib=%d  cells drawn=%d culled=%d (of %d)  "
                "entities P%d R%d K%d (slabs %zu, missing %d)",
                terrain.boxes.size(), terrain.slabs.size(), terrain.prop_count, terrain.lib_instances,
                terrain.drawn_cells, terrain.culled_cells, terrain.considered_cells,
                entity_view.players, entity_view.monsters, entity_view.objects,
                slab_library.loaded(), entity_view.missing_tiles);
            text.draw_cell(8, 8, 0, 6, line, kDimColor);
            // 光と影（P5）。**捨てた点光源の数を出す**（黙って消えると気づけない）。
            std::snprintf(line, sizeof(line),
                "light %02d:%02d %s%s  sun=(%.2f,%.2f,%.2f) ambient=%.2f shadow=%.2f%s  points=%zu%s",
                light_state.day_minute / 60, light_state.day_minute % 60,
                light_state.daytime ? "昼" : "夜", (force_time >= 0) ? "(強制)" : "",
                lighting.sun_dir.x, lighting.sun_dir.y, lighting.sun_dir.z,
                lighting.ambient_scale, lighting.shadow_strength, shadow.ready() ? "" : " (影なし)",
                lighting.points.size(),
                (dropped_lights > 0) ? (" 捨てた:" + std::to_string(dropped_lights)).c_str() : "");
            text.draw_cell(8, 8, 0, 7, line, (dropped_lights > 0) ? kWarnColor : kDimColor);
            std::snprintf(line, sizeof(line),
                "ms frame=%.2f (decode=%.2f meaning=%.2f build=%.2f shadow=%.2f submit=%.2f post=%.2f present=%.2f)  hover=%s  round-trip: %s%s",
                ms_frame, ms_decode, ms_meaning, ms_build, ms_shadow, ms_submit, ms_post, ms_present,
                hover_valid ? (std::to_string(hover_gx) + "," + std::to_string(hover_gy)).c_str() : "(地平線)",
                round_trip_ok ? "OK " : "NG ", round_trip_report.c_str());
            text.draw_cell(8, 8, 0, 8, line, round_trip_ok ? kDimColor : kWarnColor);
            // ポスト処理（P7）。**何が効いているか**が画面に出ていないと、絵の話ができない。
            std::snprintf(line, sizeof(line), "post %s  cutaway=%.0fpx  lut=%s  layout=%s%s",
                settings.post.to_line().c_str(), settings.cutaway_radius, post.lut_source().c_str(),
                layout_mode_name(layout.mode), hud_state.subs_open ? " subs+" : "");
            text.draw_cell(8, 8, 0, 9, line, kDimColor);
        } else if ((entity_view.missing_tiles > 0) || !round_trip_ok) {
            /*
             * `--text` を付けていなくても、**食い違っているものは画面に出す。**
             * 「黙って消える」を許すと、絵が違うことに誰も気づけないまま先へ進む
             * （この企画で何度も踏んでいる形。可視窓の打ち切りと同じ扱い）。
             *
             * **点光源を捨てたことはここに出さない**（2026-08-12 に決めた
             * 「光源の行だけ黙らせて」）。16 個（`kMaxPointLights`）の枠から溢れた遠い光を
             * 落としているだけで、**絵の間違いではない**——町や溶岩の階では普通に起きるので、
             * 出しっぱなしにすると本当の警告（タイル欠け・往復 NG）がその中に埋もれる。
             * 記録は残す道が 2 つある: `--text` の light 行（捨てた数を常に出す）と、
             * 下の「初回だけ標準エラーへ」。
             */
            char line[256]{};
            std::snprintf(line, sizeof(line), "警告: タイル欠け %d / 往復 %s",
                entity_view.missing_tiles, round_trip_ok ? "OK" : "NG");
            /*
             * **場面の枠の中へ置く**（2026-08-21）。以前は画面の左上角
             * （`cell_w, cell_h/2`）に描いていたが、そこは**状態列の一番上**——
             * 水色のプレイヤ名の行——と重なる。赤と青が混ざって**どちらも読めなくなり**、
             * 「この赤い字は何だ」と訊かれて初めて気づいた。
             * この知らせは 3D の絵についてのものなので、絵の枠の中が居場所である。
             *
             * 下敷きを敷くのは、絵の上に直に赤い字を置くと**明るい床では消える**ため。
             * 隠すのは 1 行ぶんだけで、隠していること自体も知らせのうちである。
             */
            const int warn_x = layout.scene.x + layout.cell_w;
            const int warn_y = layout.scene.y + (layout.cell_h / 2);
            const int warn_w = text.measure(line);
            paint.rect(
                RectPx{ warn_x - (layout.cell_w / 2), warn_y, warn_w + layout.cell_w, text.cell_h() },
                PaintColor{ 0.f, 0.f, 0.f, 0.65f });
            text.draw(warn_x, warn_y, line, kWarnColor);
        }
        /*
         * 点光源を捨てたことは**初めての 1 回だけ**標準エラーへ置く。画面から下ろしても
         * 「いつから捨てていたか分からない」にはしない（黙って捨てない、という方針は残す）。
         * 毎フレーム出すと記録が流れて他が読めなくなるので、1 回きりにしてある。
         */
        if ((dropped_lights > 0) && !dropped_lights_reported) {
            dropped_lights_reported = true;
            std::fprintf(stderr, "[hd2d] 点光源が %d 個あふれました（上限 %d。遠い順に捨てています。"
                                 "絵の間違いではないので画面には出しません）\n",
                dropped_lights, kMaxPointLights);
        }
        paint.flush(); //!< **文字より先**（下敷きなので）
        if(!feature_menu.is_open()&&!menu_handoff)hud_state.modern.draw_voxels(item_icon_renderer,slab_library,tile_catalog,layout,&text);
        trace_targeting_ui(hud_state.targeting,camera,layout,settings.main_panel==Hd2dSettings::MainPanel::Ascii,first_person.active);
        /*
         * 2D アスキー地図の字（専用のマスのとき）。**`paint` の後・UI の文字の前**に流す。
         * 前に流すと下敷きの黒で塗り潰され、後に流すと状態列やミニマップの上に乗る。
         */
        if (ascii_text_pending) {
            ascii_text.flush();
            ascii_text_pending = false;
        }
        text.flush();
        /*
         * ボタン配置の編集画面は**本体の flush の後に積んでもう一度流す**（黒背景の板を
         * 本体と同じ積みに混ぜると、下敷きが全部先に流れて HUD の字が黒幕の上に浮く。
         * `floor_cutin` と同じ理由。`flush()` は流したら空になるので二重には出ない）。
         */
        if (vpad.editor_open()) {
            vpad.draw_editor(paint, text, settings.vpad, usable_area(),settings.window_theme(),settings.modern_font_level);
            paint.flush();
            text.flush();
        }
        /*
         * カットイン演出は**いちばん上**（P10。`ui/floor_cutin.h`）。本体の積みを流した
         * 後に、自前の `UiPaint` / `TextOverlay` で描く——同じ積みに混ぜると、黒幕（四角）が
         * HUD の文字より先に流れて**幕の上に状態列が浮く**。
         * `images` は上で `begin()` 済み（板の絵に差し替えたときだけ使う）。
         */
        floor_cutin.draw(layout, screen_w, screen_h, images);
        /*
         * カットインの幕の間は、バーチャルパッドを**幕の上に**描き直す（気づいたこと
         * 2026-08-12「カットインの際にバーチャルパッドが消えてしまう」）。幕を進める
         * 決定（A）はパッドからしか押せないので、幕より上に見えていなければならない。
         * 本体の積みはもう流してあるので、ここで積んでもう一度流す（編集画面と同じ形。
         * `flush()` は流したら空になるので二重には出ない）。
         */
        if (floor_cutin.active()) {
            vpad.draw(paint, text, settings.vpad, usable_area(), pad_macros, chord_mask, settings.pad, pad_commands);
            paint.flush();
            text.flush();
        }
        /*
         * **窓へ戻す。**VR では文字 UI を板の FBO へ描いていたので、ここで外さないと
         * この後の `--shot=`（既定のフレームバッファを読む）も swap も板を見てしまう。
         */
        if (ui_target != 0u) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, screen_w, screen_h);
        }
        /*
         * VR のフレームの締め（§4）。**文字 UI を板へ描き終えてから**畳む——
         * `end_frame` が swapchain を返してしまうので、先に畳むと板が空のまま出る。
         * 鏡窓への写しは既に済ませてある（あちらはまとめる前でなければならない）。
         */
        if (vr_frame_ready && xr.valid()) {
            xr.end_frame();
        }
        /*
         * `--shot-after` は既定では**地形が出てから**数えるが、**負の値なら駆動ループの
         * 通し番号**で数える。タイトル画には地形が 1 フレームも無いので、
         * そこを撮るにはこちらが要る。
         */
        /*
         * **テスト入力の起動でだけ**、途中の画を撮る（`--test-input-file=` の隣の `.shot` に
         * 保存先のパスを 1 行書くと、次のフレームを BMP で保存してそのファイルを消す）。
         * `--shot` は撮ると終了するので、メニューを開いた画を続けて撮れない（2026-09-26）。
         */
        if (!options.test_input_file.empty()) {
            //! 入力ファイルのパスは UTF-8（Windows は起動の入口で UTF-8 へ直す。`TestKeyboard::configure` と同じ読み方）。
            const std::filesystem::path trigger(std::filesystem::u8path(options.test_input_file + ".shot"));
            std::error_code shot_ec;
            if (std::filesystem::exists(trigger, shot_ec)) {
                std::string shot_to;
                bool opened = false;
                {
                    std::ifstream in(trigger);
                    opened = in.is_open();
                    std::getline(in, shot_to);
                }
                /*
                 * **開けなかった・まだ空のときは、消さずに次のフレームでやり直す。**
                 * Windows では、置いた直後のファイルをほかのプロセス（ウイルス対策・インデックス作成など）が
                 * 一瞬つかんでいて `ifstream` が開けないことがある。以前はそのまま消していたため、撮影の要求が
                 * 黙って捨てられ、呼び手が時間切れまで待った（2026-10-23。DCSS の u1 が約 1 割落ちた原因。
                 * 開けなかった回は `opened=0 len=0` で、画面自体は応答していた）。
                 * 無限には待たない（書き手が空のファイルを置いたままのとき）。
                 */
                static int shot_unreadable_frames = 0;
                constexpr int kShotUnreadableFrameLimit = 300;
                if ((!opened || shot_to.empty()) && (shot_unreadable_frames < kShotUnreadableFrameLimit)) {
                    ++shot_unreadable_frames;
                    if (slow_frame_ms > 0.0) {
                        std::fprintf(stderr, "[hd2d] diag t=%llu ms: shot trigger unreadable frame=%d opened=%d len=%zu (retry %d)\n",
                            static_cast<unsigned long long>(clock::ticks64()), frame_index, opened ? 1 : 0, shot_to.size(),
                            shot_unreadable_frames);
                    }
                } else {
                    shot_unreadable_frames = 0;
                    std::error_code remove_ec;
                    const bool removed = std::filesystem::remove(trigger, remove_ec);
                    if (slow_frame_ms > 0.0) {
                        std::fprintf(stderr, "[hd2d] diag t=%llu ms: shot trigger frame=%d opened=%d len=%zu removed=%d remove_ec=%d\n",
                            static_cast<unsigned long long>(clock::ticks64()), frame_index, opened ? 1 : 0, shot_to.size(),
                            removed ? 1 : 0, remove_ec.value());
                    }
                    if (!shot_to.empty()) {
                        (void)save_framebuffer_bmp(shot_to, screen_w, screen_h);
                    }
                }
            }
        }
        /*
         * **画面側が描いた窓の種類を記録する**（検査用。`HD2D_WINDOW_LOG=<パス>`）。
         * 変わったフレームだけ 1 行ずつ書く。タブの切り替えの途中に別の窓（マップだけ・空の窓・
         * 前のタブ）が 1 フレームでも挟まったかを数える。
         */
        if (const char *window_log = std::getenv("HD2D_WINDOW_LOG"); (window_log != nullptr) && (*window_log != '\0')) {
            static std::string window_log_last;
            static std::ofstream window_log_out(window_log, std::ios::binary | std::ios::trunc);
            std::string drawn;
            const FeatureMenu *shown_menu = feature_menu.is_open() ? &feature_menu : menu_handoff.get();
            if (shown_menu != nullptr) {
                drawn = std::string(shown_menu == &feature_menu ? "feature:" : "handoff:")
                    + (shown_menu->showing_commands() ? "commands" : "page")
                    + " tab=" + std::to_string(shown_menu->command_tab_index());
                const auto labels = shown_menu->command_tab_labels();
                const int at = shown_menu->command_tab_index();
                if ((at >= 0) && (at < static_cast<int>(labels.size()))) drawn += "(" + labels[static_cast<std::size_t>(at)] + ")";
            } else if (modern_handoff_drawn) {
                drawn = "handoff-modern:" + modern_handoff->status();
            } else if (hud_state.modern.opening_placeholder()) {
                drawn = "map(placeholder)";
            } else if (hud_state.modern.active()) {
                drawn = "modern:" + hud_state.modern.status() + " cells=" + std::to_string(hud_state.modern.painted_content().size());
            } else if (!frame.menu_term_lines.empty() && !frame.menu_over_map) {
                drawn = "term";
            } else {
                drawn = "map";
            }
            std::string line = drawn + " core_menu=" + (frame.menu_open ? "1" : "0")
                + " modern_kind=" + frame.modern.kind + ":" + frame.modern.stage;
            //! TOband のエレメントスコープ（四角）と、カーソルのポップアップ（カーソルのマスごと）が出ている間は、行を変えて撮る。
            //! コアが送ったときだけ付く印なので、ほかのコアの行は今までと同じ。
            if (!frame.element_scope.empty()) {
                line += " scope=" + std::to_string(frame.element_scope.size());
            }
            if (!frame.targeting.info.empty()) {
                line += " aim=" + std::to_string(frame.targeting.selected.x) + "," + std::to_string(frame.targeting.selected.y);
            }
            if (line != window_log_last) {
                window_log_last = line;
                window_log_out << frame_index << ' ' << SDL_GetTicks() << ' ' << line << '\n';
                window_log_out.flush();
                //! 変わった画を撮る（`HD2D_WINDOW_SHOT_DIR=<フォルダ>`。途中の 1 フレームを目で見るため）。
                if (const char *shot_dir = std::getenv("HD2D_WINDOW_SHOT_DIR"); (shot_dir != nullptr) && (*shot_dir != 0)) {
                    (void)save_framebuffer_bmp((std::filesystem::path(shot_dir) / (std::to_string(frame_index) + ".bmp")).string(),
                        screen_w, screen_h);
                }
            }
        }
        if (ui_probe().enabled()) {
            /*
             * 検査用の問い合わせ（`{"query":"ui"}`。`app/ui_probe.h`）：このフレームの全体の情報を確定させる。
             * 文字・押せる部品・窓・玉・メッセージは、描いた関数の出口が記録済み。ここでは画面全体の状態を足す。
             */
            const char *probe_phase = ui_probe_phase(frame); //!< 規則は `app_support.cpp`（`game_over`・タイトル・誕生の見分け。自己検査が見る）
            const char *probe_wait = "";
            if (floor_cutin.blocks_input()) {
                probe_wait = "cutin";
            } else if (presentation::message_more_pending(frame)) {
                probe_wait = "more";
            } else if (frame.text_input_active) {
                probe_wait = "text";
            } else if (frame.numeric.active) {
                probe_wait = "number";
            } else if (!frame.prompt.text_utf8.empty() || !frame.prompt.choices.empty()) {
                const bool yes_no = std::any_of(frame.prompt.choices.begin(), frame.prompt.choices.end(),
                    [](const PromptChoice &c) { return (c.key == 'y') || (c.key == 'Y'); });
                probe_wait = yes_no ? "yesno" : "prompt";
            } else if (feature_menu.is_open() || hud_state.modern.active() || frame.menu_open || !frame.menu_term_lines.empty()) {
                probe_wait = "menu";
            } else if (frame.targeting.active) {
                probe_wait = "targeting";
            } else if (frame.awaiting_command) {
                probe_wait = "command";
            }
            if (!frame.pre_game_menu && (frame.hud.hp_max > 0) && (frame.hud.hp < 0)) {
                probe_wait = "death";
            } else if (frame.game_over && (probe_wait[0] == 0)) {
                //! ゲーム終了の画面で、ほかに待ちの種類が無いとき（コアが処理中の「none」と区別する）。
                probe_wait = "death";
            }
            ui_probe().frame_info(link.hello_ack().core_name, frame.frame_id, screen_w, screen_h, probe_phase, probe_wait);
            if (!frame.pre_game_menu && !frame.title_screen && !frame.cells.empty()) {
                ui_probe().map(frame.player_gx, frame.player_gy, entity_view.players, entity_view.monsters, entity_view.objects,
                    map_visible, first_person.active ? "fps" : ((settings.main_panel == Hd2dSettings::MainPanel::Ascii) ? "2d" : "3d"));
            }
            if (!frame.pre_game_menu && !frame.title_screen && !frame.cells.empty()) {
                //! 画面が描いた地形・実体（ビューポートの既知のマス）と、フロア全域の格子（ミニマップ）。入口の番号は階ごとに覚える。
                std::vector<ProbeCell> probe_cells;
                probe_cells.reserve(frame.cells.size());
                //! 光の届く範囲（画面の光の計算の結果。`map.lit_cells`）・描いている実体・使った絵の名前も、同じ走査で集める。
                ProbeLight probe_light;
                probe_light.kind = static_cast<int>(frame.lighting.light_kind);
                probe_light.radius = frame.lighting.light_radius;
                probe_light.declared = !frame.lighting.player_lit.empty();
                probe_light.memory_dim = (frame.lighting.memory_dim != 0);
                std::vector<ProbeEntity> probe_entities;
                std::map<int, std::string> probe_tile_names;
                const auto probe_alert_of = [&](int gx, int gy) -> const char * {
                    for (const MonsterAlertCell &alert : frame.monster_alerts) {
                        if ((alert.gx == gx) && (alert.gy == gy)) {
                            switch (static_cast<MonsterAlert>(alert.level)) {
                            case MonsterAlert::Asleep: return "sleep";
                            case MonsterAlert::Unwary: return "unaware";
                            case MonsterAlert::Alert: return "alert";
                            default: return "";
                            }
                        }
                    }
                    return "";
                };
                const auto probe_ring_at = [&](int gx, int gy) {
                    for (const GroundRingInstance &ring : entity_view.rings) {
                        if ((std::fabs(ring.x - (static_cast<float>(gx) + 0.5f)) < 0.6f)
                            && (std::fabs(ring.y - (static_cast<float>(gy) + 0.5f)) < 0.6f)) {
                            return true;
                        }
                    }
                    return false;
                };
                const bool probe_town = town_plan.valid() && (frame.floor.kind == static_cast<int>(FloorKind::Surface));
                const std::vector<std::uint8_t> &probe_lit_bits = frame.lighting.player_lit;
                for (std::size_t cell_index = 0; cell_index < frame.cells.size(); ++cell_index) {
                    const MapCellView &cell = frame.cells[cell_index];
                    const std::uint16_t ff = cell.feature_flags;
                    const bool probe_lit = ((cell_index >> 3) < probe_lit_bits.size())
                        && (((probe_lit_bits[cell_index >> 3] >> (cell_index & 7u)) & 1u) != 0u);
                    if (probe_lit) {
                        ++probe_light.lit_cells;
                        const int dx = cell.gx - frame.player_gx;
                        const int dy = cell.gy - frame.player_gy;
                        probe_light.max_distance = std::max(probe_light.max_distance,
                            static_cast<int>(std::ceil(std::sqrt(static_cast<double>((dx * dx) + (dy * dy))))));
                        if (lit_range_field.active() && (lit_range_field.value_at(cell.gx, cell.gy) >= 0.5f)) {
                            ++probe_light.shown_cells;
                        }
                    } else if (probe_light.memory_dim && (cell.light_level == 1)) {
                        ++probe_light.memory_cells;
                    }
                    if ((ff & CELL_FEAT_KNOWN) == 0u) {
                        continue;
                    }
                    if ((ff & CELL_FEAT_PLAYER) != 0u) {
                        probe_entities.push_back(ProbeEntity{ cell.gx, cell.gy, "player", 0, 0, cell.tile_index, "", false });
                    } else if (cell.monster_id > 0) {
                        probe_entities.push_back(ProbeEntity{ cell.gx, cell.gy, "monster", cell.monster_id, cell.monster_slot,
                            cell.tile_index, probe_alert_of(cell.gx, cell.gy), probe_ring_at(cell.gx, cell.gy) });
                    }
                    if ((cell.object_id > 0) && ((ff & CELL_FEAT_PLAYER) == 0u)) {
                        probe_entities.push_back(ProbeEntity{ cell.gx, cell.gy, "object", cell.object_id, 0,
                            (cell.monster_id > 0) ? 0 : cell.tile_index, "", false });
                    }
                    if ((cell.tile_index != 0) && (probe_tile_names.find(cell.tile_index) == probe_tile_names.end())) {
                        std::string name;
                        if (const TileEntry *entry = tile_catalog.find(cell.tile_index); entry != nullptr) {
                            //! 絵のパスは UTF-8（日本語のフォルダを含みうる）。`std::filesystem::path` へ狭い文字列で渡すと ANSI として読まれて投げるので、文字列のまま切る。
                            const std::string &file = entry->path;
                            const std::size_t slash = file.find_last_of("/\\");
                            const std::size_t begin = (slash == std::string::npos) ? 0 : (slash + 1);
                            const std::size_t dot = file.find_last_of('.');
                            const std::size_t end = ((dot == std::string::npos) || (dot < begin)) ? file.size() : dot;
                            name = std::string(1, entry->kind) + ":" + std::to_string(entry->id) + ":" + file.substr(begin, end - begin);
                        }
                        probe_tile_names.emplace(static_cast<int>(cell.tile_index), std::move(name));
                    }
                    ProbeCell pc;
                    pc.gx = cell.gx;
                    pc.gy = cell.gy;
                    pc.terrain_id = cell.terrain_id;
                    pc.tile_index = cell.tile_index;
                    pc.fg_color = cell.fg_color;
                    {
                        const RgbColor rgb = term_color_to_rgb(cell.fg_color);
                        pc.fg_rgb[0] = rgb.r;
                        pc.fg_rgb[1] = rgb.g;
                        pc.fg_rgb[2] = rgb.b;
                    }
                    pc.light_level = cell.light_level;
                    pc.lit = probe_lit;
                    if (probe_town) {
                        static const char *const kRoleNames[] = { "", "border", "rampart", "tower", "gate", "yard", "house",
                            "fountain", "crag", "path", "turf", "manor" };
                        const auto role = static_cast<std::size_t>(town_plan.role_at(cell.gx, cell.gy));
                        pc.role = (role < (sizeof(kRoleNames) / sizeof(kRoleNames[0]))) ? kRoleNames[role] : "other";
                        pc.site = town_plan.site_at(cell.gx, cell.gy);
                    }
                    if ((ff & CELL_FEAT_WALL) != 0u) {
                        pc.kind = ((ff & CELL_FEAT_PERMANENT) != 0u) ? "permanent-wall" : "wall";
                    } else if ((ff & CELL_FEAT_DOOR) != 0u) {
                        pc.kind = ((ff & CELL_FEAT_DOOR_OPEN) != 0u) ? "door-open" : "door";
                    } else if ((ff & CELL_FEAT_STAIRS) != 0u) {
                        pc.kind = "stairs";
                    } else if ((ff & CELL_FEAT_TREE) != 0u) {
                        pc.kind = "tree";
                    } else if ((ff & CELL_FEAT_WATER) != 0u) {
                        pc.kind = "water";
                    } else if ((ff & CELL_FEAT_LAVA) != 0u) {
                        pc.kind = "lava";
                    } else if ((ff & CELL_FEAT_RUBBLE) != 0u) {
                        pc.kind = "rubble";
                    } else if ((ff & CELL_FEAT_PASSABLE) != 0u) {
                        pc.kind = "floor";
                    }
                    pc.glyph = (cell.ascii_fallback != '\0') ? cell.ascii_fallback : ' ';
                    if (((ff & CELL_FEAT_DOOR) != 0u) && (std::isalnum(static_cast<unsigned char>(cell.ascii_fallback)) != 0)) {
                        pc.entrance = cell.ascii_fallback;
                    }
                    if ((ff & CELL_FEAT_PLAYER) == 0u) {
                        pc.monster = cell.monster_id;
                        pc.object = cell.object_id;
                    }
                    probe_cells.push_back(pc);
                }
                std::vector<std::string> floor_rows;
                if (frame.minimap.valid()) {
                    static const char kKindChars[] = " .#+>!m@^~T\"Ls";
                    floor_rows.reserve(static_cast<std::size_t>(frame.minimap.height));
                    for (int y = 0; y < frame.minimap.height; ++y) {
                        std::string row(static_cast<std::size_t>(frame.minimap.width), ' ');
                        for (int x = 0; x < frame.minimap.width; ++x) {
                            const auto kind = static_cast<std::size_t>(frame.minimap.at(x, y));
                            row[static_cast<std::size_t>(x)] = (kind < sizeof(kKindChars) - 1) ? kKindChars[kind] : '?';
                        }
                        floor_rows.push_back(std::move(row));
                    }
                }
                ui_probe().map_cells(std::move(probe_cells),
                    std::to_string(frame.floor.dungeon_id) + ":" + std::to_string(frame.floor.dun_level) + ":"
                        + std::to_string(frame.floor.generated_turn) + ":" + std::to_string(frame.floor.town_id) + ":"
                        + (frame.floor.wild_mode ? "w" : "-"),
                    std::move(floor_rows));
                ui_probe().light(probe_light);
                ui_probe().entities(std::move(probe_entities), static_cast<int>(entity_view.rings.size()));
                ui_probe().map_tile_names(std::move(probe_tile_names));
                /*
                 * マス → 窓の画素。**クリックの拾い方の逆**（`write_click_snapshot` と同じ式）。カメラ・矩形・字の格子の**コピー**を持たせる
                 * （次の問い合わせまで、そのフレームの写し方を保つ）。
                 */
                ui_probe().set_cell_projector([cam = camera, scene = layout.scene,
                    ascii = (settings.main_panel == Hd2dSettings::MainPanel::Ascii),
                    grid = hud_state.targeting.map_projection](int gx, int gy, int &sx, int &sy, bool &inside) {
                    if (ascii) {
                        if ((grid.cell_w <= 0) || (grid.cell_h <= 0)) {
                            return false;
                        }
                        const int col = gx - grid.cam_x + (grid.cols / 2);
                        const int row = gy - grid.cam_y + (grid.rows / 2);
                        sx = grid.x + (col * grid.cell_w) + (grid.cell_w / 2);
                        sy = grid.y + (row * grid.cell_h) + (grid.cell_h / 2);
                        inside = (col >= 0) && (row >= 0) && (col < grid.cols) && (row < grid.rows);
                        return true;
                    }
                    float px = 0.f;
                    float py = 0.f;
                    if (!cam.project(Vec3{ static_cast<float>(gx) + 0.5f, static_cast<float>(gy) + 0.5f, 0.f }, px, py)) {
                        return false;
                    }
                    sx = static_cast<int>(std::lround(px - 0.5f)) + scene.x;
                    sy = static_cast<int>(std::lround(py - 0.5f)) + scene.y;
                    inside = scene.contains(sx, sy);
                    return true;
                });
            }
            std::vector<std::string> probe_fx;
            for (std::size_t i = 0; i < attack_fx.size(); ++i) {
                const std::string &attr = attack_fx.spec_at(i).attr;
                probe_fx.push_back(attr.empty() ? std::string("missile") : attr);
            }
            ui_probe().fx_active(std::move(probe_fx));
            {
                /*
                 * 狙い・見る：`look` ／ `target`（点・生き物・固定）／ `direction`（8 方向）。
                 * 見る `l` は Angband だけが窓（`TargetingUi`）になっている。ほかのコア（変愚・短愚・幻想蛮怒・Frox・Sil-Q）の見るは、
                 * コアの問いを画面が帯（`PromptBar`）にしたもので、窓は立たない。帯の頭は「見る: 」（英語は「Look: 」）。
                 * 帯が出ている間も `active` を真にし、`mode` を `look` にする（帯か窓かは `via` で分ける）。
                 */
                const TargetingState &aim = hud_state.targeting.state();
                const bool aim_ui = hud_state.targeting.active();
                const std::string &aim_mode = frame.targeting.mode;
                bool look_band = false;
                if (!aim_ui && !frame.prompt.choices.empty()) {
                    const std::string &head = frame.prompt.text_utf8;
                    look_band = (head.rfind("見る: ", 0) == 0) || (head.rfind("Look: ", 0) == 0);
                }
                ProbeTargetingDetail detail;
                detail.band = look_band;
                if (look_band) {
                    detail.band_text = frame.prompt.text_utf8;
                }
                detail.ui_active = aim_ui;
                if (aim_ui) {
                    detail.ui_mode = aim.mode;
                    detail.ui_kind = aim.kind;
                    detail.can_confirm = aim.can_confirm;
                    detail.extras_open = hud_state.targeting.extras_open();
                    detail.candidates = static_cast<int>(aim.candidates.size());
                    detail.markers = static_cast<int>(aim.markers.size());
                    detail.distance = aim.distance;
                    detail.source_x = aim.source.x;
                    detail.source_y = aim.source.y;
                    if (aim.mode != "direction8") {
                        detail.has_selected = true;
                        detail.selected_x = aim.selected.x;
                        detail.selected_y = aim.selected.y;
                        detail.selected_id = aim.selected_id;
                        //! いま指しているもの：窓の候補にあればその種類、無ければ id（正＝敵・−1＝自分）、どれでもなければ自由な位置。
                        std::string kind;
                        for (const TargetingCandidate &candidate : aim.candidates) {
                            if (candidate.point == aim.selected) {
                                if (!candidate.kind.empty()) {
                                    kind = candidate.kind;
                                } else if (candidate.id > 0) {
                                    kind = "monster";
                                } else if (candidate.id == -1) {
                                    kind = "player";
                                }
                                break;
                            }
                        }
                        if (kind.empty()) {
                            if (aim.selected_id > 0) {
                                kind = "monster";
                            } else if ((aim.selected_id == -1) || (aim.selected == aim.source)) {
                                kind = "player";
                            } else {
                                kind = "free";
                            }
                        }
                        detail.selected_kind = std::move(kind);
                    }
                    for (const TargetingAreaCell &cell : aim.area) {
                        detail.area.push_back({ cell.point.x, cell.point.y, cell.possible });
                    }
                    for (const TargetingPath &path : aim.paths) {
                        for (const TargetingPoint &point : path.points) {
                            detail.path.push_back({ point.x, point.y, path.possible });
                        }
                    }
                }
                ui_probe().targeting(aim_ui || look_band,
                    look_band ? "look"
                              : ((aim_mode == "look") ? "look" : ((aim_mode == "direction8") ? "direction" : "target")),
                    frame.targeting.target_name.empty() ? frame.targeting.label : frame.targeting.target_name);
                ui_probe().targeting_detail(std::move(detail));
            }
            {
                //! 一時的な重ね書きの字（コアが送ったものと、画面が描いた数）。
                std::vector<ProbeOverlay> probe_overlays;
                probe_overlays.reserve(frame.map_overlay.size());
                for (const MapOverlayCell &overlay : frame.map_overlay) {
                    probe_overlays.push_back({ overlay.gx, overlay.gy, std::string(1, overlay.ascii), overlay.color });
                }
                ui_probe().overlays(std::move(probe_overlays), static_cast<int>(overlay_glyphs.size()));
            }
            if (!frame.element_scope.empty()) {
                //! エレメントスコープの四角（TOband）。描画器が使える間は、送られた数だけ描いている（描画の側は `frame.element_scope` を 1 マス 1 枚にする）。
                std::vector<ProbeScopeCell> probe_scope;
                probe_scope.reserve(frame.element_scope.size());
                for (const ElementScopeCell &cell : frame.element_scope) {
                    const RgbColor rgb = term_color_to_rgb(cell.color);
                    probe_scope.push_back({ cell.gx, cell.gy, cell.color, { rgb.r, rgb.g, rgb.b } });
                }
                ui_probe().element_scope(std::move(probe_scope),
                    optional_render.targeting.ready() ? static_cast<int>(frame.element_scope.size()) : 0);
            }
            {
                //! 窓の焦点・大きさと、昼夜（画面が光の計算に使った時刻）。
                const Uint32 window_flags = SDL_GetWindowFlags(window.window);
                ProbeWindowState state;
                state.input_focus = (window_flags & SDL_WINDOW_INPUT_FOCUS) != 0;
                state.mouse_focus = (window_flags & SDL_WINDOW_MOUSE_FOCUS) != 0;
                state.shown = (window_flags & SDL_WINDOW_SHOWN) != 0;
                state.minimized = (window_flags & SDL_WINDOW_MINIMIZED) != 0;
                state.test_hidden = !options.test_input_file.empty() && (std::getenv("HD2D_TEST_HIDE_WINDOW") != nullptr);
                state.pad_focus = state.input_focus || state.test_hidden;
                state.w = screen_w;
                state.h = screen_h;
                ui_probe().window_state(state);
                ProbeTimeState time;
                time.day_minute = light_state.day_minute;
                time.daytime = light_state.daytime;
                time.night_factor = lamp_night_factor(light_state);
                ui_probe().time_state(time);
                if (weather_view.active() && frame.weather.declared) {
                    ProbeWeather pw;
                    pw.rain = frame.weather.rain;
                    pw.wind = frame.weather.wind;
                    pw.temp = frame.weather.temp;
                    pw.rain_amount = weather_view.rain_amount;
                    pw.snow_amount = weather_view.snow_amount;
                    pw.wind_x = weather_view.wind_x;
                    pw.wind_y = weather_view.wind_y;
                    pw.right_x = weather_view.right_x;
                    pw.right_y = weather_view.right_y;
                    pw.icon = weather_icon_index(frame.weather);
                    pw.dust_amount = settings.dust * (1.f - std::min(1.f, weather_view.precipitation()));
                    pw.daylight = weather_daylight_factor(light_state.day_minute);
                    ui_probe().weather(pw);
                }
            }
            //! コアの生死と、コアのフレームが最後に変わってからの時間（`frame_age_ms`。フレームの番号が変わった時刻を持つ）。
            ui_probe().liveness(!link.core_gone() && !link.child_exited(), link.exit_received() ? link.exit_code() : -1);
        }
        recorder.capture(screen_w, screen_h); //!< swap の直前（`--shot=` と同じ位置）
        const int shot_gate = (options.shot_after_frames >= 0) ? frames_with_terrain : frame_index;
        const int shot_want = std::abs(options.shot_after_frames);
        if (!options.shot_path.empty() && (shot_gate == shot_want)) {
            (void)save_framebuffer_bmp(options.shot_path, screen_w, screen_h);
            result = 0;
            break;
        }
        if (!skip_window_present) {
            const Uint64 present_start = clock::perf();
            SDL_GL_SwapWindow(window.window);
            ms_present = static_cast<double>(clock::perf() - present_start) * 1000.0
                / static_cast<double>(perf_freq);
        }
        // **仕事に使った時間**（末尾の待ちは含まない）。`loop_started` は ms なので混ぜない。
        ms_frame = static_cast<double>(clock::perf() - loop_started_perf) * 1000.0
            / static_cast<double>(perf_freq);

        /*
         * (d') 往復検査（§4.5・§14-6）。**毎フレームは重いので間引く**が、
         * カメラは動き続けるので 1 回だけでは意味が無い。半秒に 1 度、いまのカメラで回す。
         */
        if (((frame_index % 30) == 0) && !first_person.active) {
            /*
             * **一人称では回さない。**この検査は「画面に写っているマスを指し直せるか」で、
             * クリック移動の担保である。見下ろし 5° では地平線の手前で 1 画素に何マスも
             * 載るので、指し直すと隣のマスへ落ちるのが**当たり前**になる（壊れてはいない）。
             * 回せば必ず失敗が出て、本当の退行を隠す雑音になる。
             */
            round_trip_ok = camera_round_trip_check(camera, view_window, round_trip_report);
            if (!round_trip_ok) {
                std::fprintf(stderr, "[hd2d] round-trip FAILED: %s\n", round_trip_report.c_str());
            }
        }

        ++fps_frames;
        const Uint64 now = clock::ticks64();
        if ((now - fps_mark) >= 500) {
            fps = (static_cast<double>(fps_frames) * 1000.0) / static_cast<double>(now - fps_mark);
            fps_frames = 0;
            fps_mark = now;
        }

        /*
         * (d'') 設定の書き戻し（P8）。**メニューを閉じている間にだけ**書く。
         * 開いている最中に書くと、値を 1 つ動かすたびにディスクを叩く
         * （既存 UI の「掴みを離すときに初めて cfg へ書く」と同じ考え方）。
         */
        if (!feature_menu.is_open() && (hud_state.grabbed_grip < 0) && settings.differs_from(saved_settings)) {
            /*
             * **2 か所へ書く**（2026-08-26 に決めた「完全にコア毎の記述になるように」）。
             *   - `hd2d-<コア>.cfg` … このコアの設定。**次からはこちらが読まれる**
             *   - `hd2d.cfg` … コアの一覧と前回選んだコア（**コアを選ぶ前に要る**）。
             *     まだ自分のファイルを持たないコアの引き継ぎ元でもある
             */
            const bool wrote_core = save_settings(core_cfg_path, settings);
            const bool wrote_shared = save_settings(default_settings_path(), settings);
            if (wrote_core && wrote_shared) {
                saved_settings = settings;
            } else {
                //! 書けなかったことは黙らせない。次の起動で消えるのを黙って受け入れさせない。
                std::fprintf(stderr, "[hd2d] %s へ書けませんでした（設定は次の起動で消えます）\n",
                    wrote_core ? default_settings_path() : core_cfg_path.c_str());
                saved_settings = settings; // 毎フレーム叩き続けないように諦める
            }
        }
        /*
         * (d2) 言語が替わった → **コアを起こし直す**（追補 A2。2026-08-23 に決めた
         * 「画面がコアを立て直す。その際自動セーブしてコアを終了させ、
         * コアの再起動時に自動ロードさせる」）。
         *
         * 替わったことに気づく場所をここにしたのは、**言語を替える口が 1 つとは限らない**
         * からである（機能メニュー・`HD2D_LANG`・cfg の読み直し）。つまみの側に
         * 手を入れるより、降ろした値と今の値を突き合わせるほうが取りこぼしが無い。
         *
         * 動くのは**コアが `lang-restart` を名乗ったとき**だけ。名乗らないコア
         * （変愚・短愚蛮怒・幻想蛮怒）は 1 バイトも変わらない——あちらは `ui_state.lang`
         * が次のフレームから効く。
         *
         * `want_quit` を立てて (e) に任せる。コアは `quit_request` を**保存の経路**で
         * 受けるので（`sq_shutdown_and_quit()`）、ここで別に保存を頼む必要は無い。
         */
        if (core_wants_lang_restart && !relaunch_for_lang && (core_lang != i18n::current())) {
            std::fprintf(stderr, "[hd2d] 言語が %s → %.*s に替わりました。コアを保存して起こし直します\n",
                core_lang.c_str(), static_cast<int>(i18n::current().size()), i18n::current().data());
            relaunch_for_lang = true;
            want_quit = true;
        }

        /*
         * 機能メニューの「終了」は削除した（2026-08-11 に決めた）。`want_quit` に残るのは
         * 窓の × とコアからの終わりだけで、遊びの終了はコアの通常の終了（^X ほか）が受け持つ。
         */

        // (e) 終了要求 → `quit_request` を**一度だけ**（v1 §9.1）。窓は畳まない。
        if (want_quit && !quit_sent) {
            quit_sent = true;
            quit_sent_at = clock::ticks64();
            (void)link.send_quit_request();
        }

        // (f) 終わりの検知。
        if (link.fatal_received()) {
            show_core_failure(link, "ゲームコアが続行不能を報告しました:\n" + link.fatal_reason());
            result = 1;
            break;
        }
        if (link.exit_received()) {
            result = link.exit_code();
            if (dcss_ui) { // DCSS のときだけ（終了コード 0 で終わる道を後から突き止める記録。ほかのコアは変えない）
                std::fprintf(stderr, "[hd2d] コアが終わりました（exit code=%d・終了要求を送った=%d）\n", result, quit_sent ? 1 : 0);
            }
            /*
             * **言語を替えたので畳んだ**（(d2)）。同じコアを起こし直す。
             * `want_quit` は見ない——あれはフレームごとの局所変数で、`exit` が
             * 次のフレームで届いた時点ではもう倒れている。旗は `relaunch_for_lang` のほう。
             */
            if (relaunch_for_lang && (result == 0)) {
                /*
                 * **cfg へ書いてから畳む**（2026-08-25。こう気づいた——「英語に切り替えると
                 * ゲームが終了し立ち上がりなおすが、**日本語に戻って立ち上がる**」）。
                 *
                 * 起こし直しは `run()` を出て入り直す道なので、**次の `run()` は cfg を
                 * 読み直す**（`load_settings()` → `settings.lang` → `i18n::set_language()`）。
                 * ところが (d'') の書き戻しは**機能メニューを閉じている間しか走らない**。
                 * 言語を替える口はその機能メニューの中にあり、替えた**その場で** (d2) が
                 * 起こし直しを決めるので、**開いたまま畳まれて 1 度も書かれない**。
                 * 結果、次の `run()` は古い cfg の `ja` を読み、**替えたはずの言語が戻る**。
                 *
                 * コアの言語も同じ道である——コアは `--lang=` が無ければ**最初の
                 * `ui_state.lang`** で決める（`sq_main.cpp` の `g_first_ui_state_lang`）ので、
                 * 画面が `ja` に戻れば**コアも `ja` で立つ**。直す場所は 1 つでよい。
                 *
                 * **言語だけを書かない。** メニューを開けている間に触った値は他にもありうる
                 * （拡大率・パネル…）。ここで畳むと**それも道連れ**になる。
                 */
                if (settings.differs_from(saved_settings)) {
                    const bool wrote_core = save_settings(core_cfg_path, settings);
                    const bool wrote_shared = save_settings(default_settings_path(), settings);
                    if (wrote_core && wrote_shared) {
                        saved_settings = settings;
                    } else {
                        std::fprintf(stderr,
                            "[hd2d] %s へ書けませんでした（起こし直すと言語が戻ります）\n",
                            wrote_core ? default_settings_path() : core_cfg_path.c_str());
                    }
                }
                CoreRelaunch req;
                req.core_path = core_choice;
                req.resume = true;
                req.core_select_shown = core_select_shown;
                set_core_relaunch(req);
                result = kRunRelaunchCore;
                break;
            }
            // A finished core ends the application (user decision, 2026-09-10).
            // Language changes are handled above by kRunRelaunchCore.
            break;
        }
        if (link.core_gone() || link.child_exited()) {
            // Process exit and a failed write can precede the final pipe read.
            // Read completion first: every exit/fatal message is published before it.
            const bool drained = link.receiver_finished();
            if (link.exit_received() || link.fatal_received()) {
                continue; // Handle the terminal message at the top of this check.
            }
            const Uint64 now = clock::ticks64();
            if (core_disconnected_at == 0) core_disconnected_at = now + 1;
            if (drained || (now + 1 - core_disconnected_at >= 2000)) {
                core_died_unexpectedly = true;
                result = 1;
                break;
            }
        }

        // (g) `quit_request` 後の待ち（v1 §9.2）。勝手に kill しない。
        //     コアは終了要求を **ESC 1 個**として受け取るので、地図の上ではすぐ終わらない。
        if (quit_sent && ((clock::ticks64() - quit_sent_at) >= kQuitGraceMs)) {
            if (ask_force_kill()) {
                link.force_quit();
                result = 1;
                break;
            }
            quit_sent_at = clock::ticks64(); // 「待つ」を選んだのでもう 30 秒
        }

        /*
         * (h) ペース調整。
         *
         * **`SDL_Delay` は Windows では刻みが粗い**（既定のタイマ分解能では 15.6ms 単位に
         * 丸められる）。P3 の実測で「仕事は 2ms なのに 37fps」だったのはここが原因で、
         * 描画が重いわけではなかった。`SDL_GL_SetSwapInterval(1)` が効いている限り
         * `SDL_GL_SwapWindow` が待つので、**残りが 2ms を切っていたら寝ない**。
         */
        if (!xr.valid()) {
            const Uint64 spent = clock::ticks64() - loop_started;
            if ((spent + 2) < kFrameIntervalMs) {
                clock::delay(static_cast<Uint32>(kFrameIntervalMs - spent - 2));
            }
        }
    }

    /*
     * **マウスを放してから畳む。**掴んだまま（`SDL_SetRelativeMouseMode`）終わると、
     * コアの停止を知らせる窓（`show_core_failure`）でカーソルが出ず、閉じられなくなる。
     */
    if (mouse_is_relative) {
        (void)SDL_SetRelativeMouseMode(SDL_FALSE);
        mouse_is_relative = false;
    }
    if (core_died_unexpectedly) {
        show_core_failure(link, "ゲームコアが予期せず停止しました。");
    }

    /*
     * **GL コンテキストを消す前に畳む。**VR は FBO とレンダバッファを持っているので、
     * デストラクタ任せ（`SDL_GL_DeleteContext` の後）にすると死んだ文脈へ返しに行く。
     */
    xr.shutdown();
    vr_room_renderer.shutdown();
    vr_fake_ui.shutdown();
    //! TRON のマステクスチャ。**文脈を消す前に。**
    if (look_cells_tex != 0) {
        glDeleteTextures(1, &look_cells_tex);
        look_cells_tex = 0;
    }
    /*
     * **身代わりのテクスチャ 3 枚**（影 1・字 2）。**文脈を消す前に。**
     *
     * どちらも描画の途中で作られ、**関数の中の静的な変数**で持っている。作られた当時は
     * 「文脈は 1 プロセスに 1 つ」だったので、捨てる道を持っていなかった。
     * **追補 A2（言語の切り替え）で `run()` を出入りするようになって前提が壊れた**
     * ——死んだ文脈の名前が次の回へ残り、結ぶたびに
     * `GL_INVALID_OPERATION: Texture name does not refer to a texture object generated by OpenGL`
     * を出す。実機のログで **2 回目の `run()` から 1 回あたり 1 万件超**（SH-36）。
     *
     * 捨てれば、次に要ったときにその文脈で作り直される。
     */
    release_shadow_placeholder();
    release_look_placeholders();
    //! 光の届く範囲の場（テクスチャ）と身代わり。これも文脈を消す前に（同じ理由）。
    lit_range_field.release();
    release_lit_placeholder();
    optional_render.shutdown();
    dust.shutdown();
    weather_particles.shutdown();
    post.shutdown();
    shadow.shutdown();
    sky.shutdown();
    clouds.shutdown();
    /*
     * **この回の板の成績を 1 行残す**（2026-08-25。SH-34）。`run()` はコアを起こし直すたびに
     * 出入りするので、**同じログに何回ぶんも並ぶ**——推定「コアを起こし直すと
     * 字の板に落ちる」が当たっているなら、**外れの数が回を追うごとに増える**はずである。
     * 当たっていなければ 0 のまま並ぶ。**数えずに議論しないための行**である。
     */
    {
        const std::string why
            = slab_library.last_error().empty() ? std::string() : ("（直近の理由: " + slab_library.last_error() + "）");
        std::fprintf(stderr, "[hd2d] この回の実体の板: 載せた %zu 枚 / 外れ %zu 件%s\n", slab_library.loaded(),
            slab_library.missing(), why.c_str());
    }
    slab_library.shutdown(&renderer);
    library.release_gpu(renderer);
    renderer.release(wall_gpu);
    renderer.release(floor_gpu);
    renderer.shutdown();
    item_icon_renderer.shutdown();
    title_image.unload();
    panel_bottom_image.unload();
    panel_right_image.unload();
    orb_back_image.unload();
    orb_glass_image.unload();
    orb_liquid_hp_image.unload();
    orb_liquid_mp_image.unload();
    orb_glass_top_image.unload();
    orb_front_image.unload();
    status_icons_image.unload();
    weather_icons_image.unload();
    floor_cutin.shutdown();
    hud_state.modern.shutdown();
    if (dcss_ui) {
        dcss_ui->shutdown();
    }
    images.shutdown();
    paint.shutdown();
    text.shutdown();
    pad.close();
    SDL_GL_DeleteContext(window.context);
    SDL_DestroyWindow(window.window);
    /*
     * 窓を畳んでからコアの後始末。**順序が意味を持つ**:
     * stdin を閉じる（＝ui が消えたのと同じ合図）→ コアが緊急セーブして畳むのを待つ →
     * こちらのハンドルを閉じる。保存の猶予後も終了しなければ shutdown が強制終了する。
     */
    link.close_core_stdin();
    (void)link.wait_for_core_exit(static_cast<unsigned>(kQuitGraceMs));
    link.shutdown();
#if defined(_WIN32)
    if (result == kRunRelaunchCore) {
        // Keep the Windows input subsystem alive across UI runs. Destroying and
        // reinitializing it here can leave the new window without hardware input.
        // Events from the destroyed window must not activate the next selector.
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        std::fprintf(stderr, "[hd2d] keeping SDL input initialized for UI restart\n");
    } else {
        SDL_Quit();
    }
#else
    SDL_Quit();
#endif
    return result;
}

} // namespace hd2d
