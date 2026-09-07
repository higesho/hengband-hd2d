/*!
 * @file hd2d_main.cpp
 * @brief `HengbandHd2d.exe` の入口（ボクセル HD2D。第 3 の実行体）。
 *
 * 基準はボクセル HD2D の設計（契約）と、その段取り。
 *
 * ## この exe の立ち位置
 * `HengbandCore.exe` にプロトコル v1 で繋がる**フロントエンド**である。
 * 2D の `HengbandUi.exe` と並走する作りだったが、あちらは 2026-08-12 に削除した
 * （設計書 §2・必守制約 5）。`src/` には触らない。
 *
 * ## 自分が解釈する起動引数
 * `--protocol-log=` / `--core-protocol-log=` / `--gl-probe` / `--windowed=WxH` の 4 つだけ。
 * **それ以外は全部そのままコアへ渡す**（`--bot-json-output=` などを従来どおり通すため）。
 *
 * ## サブシステム
 * WINDOWS（`WinMain`）。コンソールから起動されたときだけ親のコンソールへ stderr を繋ぐ。
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX //!< windows.h の min/max マクロが `std::max` を壊すため
#endif
#include <windows.h>

#include <algorithm>

#include "app/hd2d_app.h"
#include "render/camera.h" //!< `kViewTurnCount`（視点回転の段数。1 段 45°）

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kProtocolLogArg = "--protocol-log=";
constexpr std::string_view kCoreProtocolLogArg = "--core-protocol-log=";
constexpr std::string_view kWindowedArg = "--windowed=";
/*!
 * @brief 起こすコアの exe。**選択画面を飛ばす。**
 * @note **転送の else より前に置くこと。**後ろだとコアへ素通りして無反応になる（罠 11）。
 */
constexpr std::string_view kCorePathArg = "--core-path=";
//! コア選択画面だけを出して確かめる（同 §9-3）。**コアを起こさない。**
constexpr std::string_view kCoreSelectCheckArg = "--core-select-check";
constexpr std::string_view kGlProbeArg = "--gl-probe";
//! P1 の口。**コアを起こさない。**
constexpr std::string_view kPrefabArg = "--prefab=";
constexpr std::string_view kPrefabCheckArg = "--prefab-check=";
//! 実体の板の粒を 8〜512 の 7 段すべてで通す（`hd2d/app/hd2d_app.h` の `slab_ladder_check`）。
constexpr std::string_view kSlabLadderCheckArg = "--slab-ladder-check";
constexpr std::string_view kVoxelDirArg = "--voxel-dir=";
constexpr std::string_view kShotArg = "--shot=";
constexpr std::string_view kShotAfterArg = "--shot-after=";
constexpr std::string_view kTextArg = "--text";
constexpr std::string_view kTerrainCheckArg = "--terrain-check";
constexpr std::string_view kCombatFxCheckArg = "--combat-fx-check";
constexpr std::string_view kShrinkViewArg = "--shrink-view=";
//! P5 の検査の検査。影の直方体をわざと狭める。
constexpr std::string_view kShrinkShadowArg = "--shrink-shadow=";
constexpr std::string_view kCameraArg = "--camera=";
constexpr std::string_view kWorldCheckArg = "--world-check";
constexpr std::string_view kTownCheckArg = "--town-check";
constexpr std::string_view kTownDataArg = "--town-data=";
//! **実データの町をその場で描く**（P10 第 2 期レビュー 3。町ごとの意匠を見てもらう口）。
constexpr std::string_view kTownViewArg = "--town-view";
constexpr std::string_view kTownAtArg = "--town-at=";
//! **合成フロアに N 番のダンジョンの顔をさせる**（P10 第 4 期。町の `--town-view` と同じ役目）。
constexpr std::string_view kDungeonArg = "--dungeon=";
//! **合成フロアの階**（`--dun-level=N`）。意匠の**階の帯**を検分する手段（D5 の浅間浄穢山）。
constexpr std::string_view kDunLevelArg = "--dun-level=";
//! P6 の口。動きの検査と、撮影のための時刻固定。
constexpr std::string_view kMotionCheckArg = "--motion-check";
//! 同時押しとマクロのトリガーの検査（`hd2d/app/hd2d_app.h` の `pad_check`）。
constexpr std::string_view kPadCheckArg = "--pad-check";
constexpr std::string_view kMotionTimeArg = "--motion-time=";
constexpr std::string_view kMotionFramesArg = "--motion-frames=";
constexpr std::string_view kMotionSpanArg = "--motion-span=";
constexpr std::string_view kFieldArg = "--field=";
//! P7 の口。ポスト処理とカットアウェイ。
constexpr std::string_view kPostArg = "--post=";
constexpr std::string_view kLutArg = "--lut=";
constexpr std::string_view kLutExportArg = "--lut-export=";
constexpr std::string_view kCutawayArg = "--cutaway=";
//! P10 レビュー 5。ぼけの強さ（並べて比べる手段）。
constexpr std::string_view kDofArg = "--dof=";
constexpr std::string_view kPostCheckArg = "--post-check";
constexpr std::string_view kCutawayCheckArg = "--cutaway-check";
constexpr std::string_view kShrinkCutawayArg = "--shrink-cutaway=";
//! P8 の口。画面の作りと、その検査。
constexpr std::string_view kLayoutArg = "--layout=";
constexpr std::string_view kUiCheckArg = "--ui-check";
//! 一人称（FPS モード）で始める。実行中の切り替えは機能メニューで割り当てた F キー。
constexpr std::string_view kFpsArg = "--fps";
//! 同 初期方位つき（`--fps=90` で東を向いて始める）。0 = 北・時計回り。
constexpr std::string_view kFpsYawArg = "--fps=";
/*!
 * @brief 見下ろしの視点回転。
 * @details 実行中は F5／F6 だが、**そのキーはこの exe が食う**のでスクリプトからは押せない。
 * 4 方位を `--shot=` で撮り分けるための手段。
 */
constexpr std::string_view kTurnArg = "--turn=";
/*!
 * @brief VR ジオラマの盤の回転。
 * @details 実行中は右スティック横のフリックだが、**スティックはスクリプトから注入できない**。
 * @note **転送の else より前に置くこと**（罠 11）。
 */
constexpr std::string_view kBoardTurnArg = "--board-turn=";
//! 実体の表現。
constexpr std::string_view kEntityArg = "--entity=";
//! メインパネルを 2D のアスキー地図で始める（`--original`。同 §6）。
constexpr std::string_view kOriginalArg = "--original";
/*!
 * @name 画調
 * @details 実行中は機能メニューで回せるが、**メニューはスクリプトから開けない**ので
 * `--shot=` で撮り分けるための手段が要る（`--entity=` と同じ理由）。
 * @note **転送の else より前に置くこと**（罠 11）。`--tron-sky=` を `--tron-sky` より
 * **先に**見ること——後ろに置くと `--tron-sky=0` が素の旗として食われて逆の意味になる。
 * @{
 */
constexpr std::string_view kLookArg = "--look=";
constexpr std::string_view kBgmArg = "--bgm=";
constexpr std::string_view kTronSkyValueArg = "--tron-sky=";
constexpr std::string_view kTronSkyArg = "--tron-sky";
//! 面の汚し（`hd2d/render/surface_wear.h`）。`off` / `on` / 0.0〜2.0。
constexpr std::string_view kWearArg = "--wear=";
//! 木の葉（`hd2d/render/leaf_detail.h`）。`off` / `on` / 0.0〜2.0。
constexpr std::string_view kLeafArg = "--leaf=";
//! 材質ごとの汚しの入切。`all` / `none` / `stone,wood,…`。**`--wear=` より先に見ること。**
constexpr std::string_view kWearMaterialsArg = "--wear-materials=";
/*! @name 画面全体の色処理（2026-08-23 に決めた）。撮り比べるための口。@{ */
constexpr std::string_view kVignetteArg = "--vignette=";
constexpr std::string_view kSepiaArg = "--sepia=";
constexpr std::string_view kHdrArg = "--hdr=";
constexpr std::string_view kExposureArg = "--exposure=";
constexpr std::string_view kDustArg = "--dust=";
/*! @} */
//! ライブラリぜんぶの材質の推定を数える（GL 不要）。
constexpr std::string_view kMaterialCheckArg = "--material-check";
/*! @} */
constexpr std::string_view kGrowSceneArg = "--grow-scene=";
/*!
 * @name VR
 * @details **転送の else より前に置くこと。**後ろに置くとコアへ素通りして無反応になる
 * （設計書 罠 11）。この表の順は解釈ループの順とは別なので、ここに足すだけでは足りない。
 * @{
 */
constexpr std::string_view kVrArg = "--vr";
constexpr std::string_view kVrFakeArg = "--vr-fake";
constexpr std::string_view kVrCheckArg = "--vr-check";
constexpr std::string_view kVrMathCheckArg = "--vr-math-check";
constexpr std::string_view kVrFramesArg = "--vr-frames=";
/*! @} */
constexpr std::string_view kFullscreenArg = "--fullscreen";
//! P9 の口。ボクセルエディタ。**コアを起こさない。**
constexpr std::string_view kEditArg = "--edit=";
constexpr std::string_view kEditNewArg = "--edit-new=";
constexpr std::string_view kEditCheckArg = "--edit-check";

/*!
 * @brief 画面側の stderr を `%TEMP%\hengband-hd2d-stderr.log` へ向ける。
 * @details コアの側は `core_link.cpp` が同じことをしている
 * （`%TEMP%\hengband-hd2d-core-stderr.log`）。**画面側だけ行き先が無かった**——
 * 二重起動で読み合わないよう、名前はコアのものと分けてある。
 * @note `"w"` なので**起動のたびに消える**（溜め込まない）。コアを起こし直しても
 * 画面のプロセスは生き続けるので、**1 回の遊びが 1 本のログ**になる。
 */
void log_stderr_to_temp_file()
{
    char dir[MAX_PATH]{};
    const DWORD len = ::GetTempPathA(MAX_PATH, dir);
    std::string path = ((len > 0) && (len < MAX_PATH)) ? std::string(dir, len) : std::string();
    path += "hengband-hd2d-stderr.log";
    /*
     * **`freopen_s` で開く。共有して開こうとするのは、もうやめる。**
     *
     * `freopen` は独占で開くので、**窓を閉じるまでログを読めない**。それが不便なのは
     * 確かで、2 通り試したが**どちらも 0 バイトのログを作って失敗した**:
     *
     * | 試したもの | 何が起きたか |
     * |---|---|
     * | `_fsopen()` ＋ `_dup2()`（2026-08-25） | 窓の実行体には**標準エラーの番号が無い**（`_fileno(stderr)` が負）ので、複製の行き先が無い |
     * | `CreateFile` ＋ `_open_osfhandle` ＋ `*stderr = *fp`（2026-08-26） | いまの CRT の `FILE` は**中身を持たない器**なので、丸ごと写しても何も繋がらない |
     *
     * **2 度とも「書けなくなった」ことに気づけたのは気づいたことからである。**
     * 読みやすさのために書けることを賭けるのは割に合わない。
     * **読むときは窓を閉じてもらう**——それが確実で、失うのは手間だけである。
     */
    FILE *reopened = nullptr;
    if (::freopen_s(&reopened, path.c_str(), "w", stderr) != 0) {
        return;
    }
    //! **緩衝しない。**落ちた回のログこそ読みたいのに、最後の数百行が消えては意味が無い。
    (void)std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::fprintf(stderr, "[hd2d] stderr -> %s\n", path.c_str());
}

/*!
 * @brief 自分の stderr を使える状態にする。
 * @details 既に有効な標準エラー（リダイレクト先）があればそれを使う。無ければ親の
 * コンソールへ相乗りする。**どちらも無ければ一時ファイルへ落とす**
 * （2026-08-25。それまでは行き先を持たず、画面側の警告が**まるごと消えていた**——
 * SH-34「板が字に化ける」で、せっかく出した理由が誰にも届かなかった。
 * 窓を二重に叩いて起動する利用者に「コンソールから起動してください」と言うのは、
 * 起きた後では手遅れである）。
 */
void prepare_stderr()
{
    const HANDLE handle = ::GetStdHandle(STD_ERROR_HANDLE);
    if ((handle != nullptr) && (handle != INVALID_HANDLE_VALUE)) {
        return;
    }
    if (::AttachConsole(ATTACH_PARENT_PROCESS) == FALSE) {
        log_stderr_to_temp_file();
        return;
    }
    const HANDLE console = ::CreateFileA("CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (console == INVALID_HANDLE_VALUE) {
        log_stderr_to_temp_file();
        return;
    }
    ::SetStdHandle(STD_ERROR_HANDLE, console);
    FILE *reopened = nullptr;
    (void)::freopen_s(&reopened, "CONOUT$", "w", stderr);
}

//! `--windowed=1600x900` を読む。読めなければ何もしない（既定のまま）。
/*!
 * @brief `--windowed=WxH` を読む。
 * @note **受け取らなかったときは必ず言う。**以前は黙って既定へ落としていたので、
 * `--windowed=600x500`（下限 640×400 未満）が無視されたことに気づけず、
 * 1600×900 で撮れた画像を見て「なぜ大きいのか」を考える羽目になった。
 * 捨てたことを黙らせないのはこの企画の方針である（可視窓の打ち切りと同じ）。
 */
void parse_window_size(std::string_view value, hd2d::AppOptions &options)
{
    const std::size_t cross = value.find('x');
    const int w = (cross == std::string_view::npos) ? 0 : std::atoi(std::string(value.substr(0, cross)).c_str());
    const int h = (cross == std::string_view::npos) ? 0 : std::atoi(std::string(value.substr(cross + 1)).c_str());
    if ((w >= 640) && (h >= 400)) {
        options.window_w = w;
        options.window_h = h;
        return;
    }
    std::fprintf(stderr, "[hd2d] --windowed=%.*s は受け取れません（WxH で 640x400 以上）。"
                         "%dx%d のままにします\n",
        static_cast<int>(value.size()), value.data(), options.window_w, options.window_h);
}

} // namespace

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
    prepare_stderr();

    hd2d::AppOptions options;
    for (int i = 1; i < __argc; ++i) {
        const std::string_view opt = __argv[i];
        if (opt.starts_with(kProtocolLogArg)) {
            options.protocol_log_path = std::string(opt.substr(kProtocolLogArg.size()));
        } else if (opt.starts_with(kCoreProtocolLogArg)) {
            options.core_protocol_log_path = std::string(opt.substr(kCoreProtocolLogArg.size()));
        } else if (opt == kCoreSelectCheckArg) {
            options.core_select_check = true;
        } else if (opt.starts_with(kCorePathArg)) {
            options.core_path = std::string(opt.substr(kCorePathArg.size()));
        } else if (opt.starts_with(kWindowedArg)) {
            parse_window_size(opt.substr(kWindowedArg.size()), options);
        } else if (opt == kGlProbeArg) {
            options.gl_probe_only = true;
        } else if (opt.starts_with(kPrefabArg)) {
            options.prefab_name = std::string(opt.substr(kPrefabArg.size()));
        } else if (opt.starts_with(kPrefabCheckArg)) {
            options.prefab_name = std::string(opt.substr(kPrefabCheckArg.size()));
            options.prefab_check_only = true;
        } else if (opt == kSlabLadderCheckArg) {
            options.slab_ladder_check = true;
        } else if (opt.starts_with(kVoxelDirArg)) {
            options.voxel_dir = std::string(opt.substr(kVoxelDirArg.size()));
        } else if (opt.starts_with(kShotArg)) {
            options.shot_path = std::string(opt.substr(kShotArg.size()));
        } else if (opt.starts_with(kShotAfterArg)) {
            // 負の値は「駆動ループの通し番号で数える」（タイトル画を撮るときに使う）。
            const int after = std::atoi(std::string(opt.substr(kShotAfterArg.size())).c_str());
            options.shot_after_frames = (after < 0) ? std::min(-1, after) : std::max(1, after);
        } else if (opt == kTextArg) {
            options.text_map = true;
        } else if (opt == kTerrainCheckArg) {
            options.terrain_check = true;
        } else if (opt == kCombatFxCheckArg) {
            //! 地形の上に重ねて撮るので、場面は `--terrain-check` のものを使う。
            options.terrain_check = true;
            options.combat_fx_check = true;
        } else if (opt == kMotionCheckArg) {
            options.motion_check = true;
        } else if (opt == kPadCheckArg) {
            options.pad_check = true;
        } else if (opt.starts_with(kMotionTimeArg)) {
            options.motion_time = static_cast<float>(std::atof(std::string(opt.substr(kMotionTimeArg.size())).c_str()));
        } else if (opt.starts_with(kMotionFramesArg)) {
            options.motion_frames = std::max(1, std::atoi(std::string(opt.substr(kMotionFramesArg.size())).c_str()));
        } else if (opt.starts_with(kMotionSpanArg)) {
            options.motion_span = static_cast<float>(std::atof(std::string(opt.substr(kMotionSpanArg.size())).c_str()));
        } else if (opt.starts_with(kFieldArg)) {
            options.field = std::clamp(std::atoi(std::string(opt.substr(kFieldArg.size())).c_str()), 1, 40);
        } else if (opt.starts_with(kShrinkShadowArg)) {
            options.shrink_shadow = std::atoi(std::string(opt.substr(kShrinkShadowArg.size())).c_str());
        } else if (opt.starts_with(kShrinkViewArg)) {
            options.shrink_view = std::atoi(std::string(opt.substr(kShrinkViewArg.size())).c_str());
        } else if (opt.starts_with(kCameraArg)) {
            // `--camera=見下ろし度,水平画角度,1マスのpx`。書かなかったところは既定のまま。
            float pitch = options.camera_pitch_deg;
            float fov = options.camera_fov_deg;
            float cell = options.camera_cell_px;
            const int got = std::sscanf(std::string(opt.substr(kCameraArg.size())).c_str(), "%f,%f,%f", &pitch, &fov, &cell);
            if (got >= 1) {
                options.camera_pitch_deg = pitch;
            }
            if (got >= 2) {
                options.camera_fov_deg = fov;
            }
            if (got >= 3) {
                options.camera_cell_px = cell;
            }
            options.camera_from_args = (got >= 1);
        } else if (opt == kWorldCheckArg) {
            options.world_check = true;
        } else if (opt == kTownCheckArg) {
            options.town_check = true;
        } else if (opt.starts_with(kTownDataArg)) {
            options.town_data_path = std::string(opt.substr(kTownDataArg.size()));
        } else if (opt == kTownViewArg) {
            options.town_view = true;
        } else if (opt.starts_with(kTownAtArg)) {
            int tx = -1;
            int ty = -1;
            if (std::sscanf(std::string(opt.substr(kTownAtArg.size())).c_str(), "%d,%d", &tx, &ty) == 2) {
                options.town_at_x = tx;
                options.town_at_y = ty;
            }
        } else if (opt.starts_with(kDunLevelArg)) {
            //! **`--dungeon=` より先に見る。**`--dungeon` は `--dun-level` の接頭辞ではないが、
            //! 逆に読むと将来 `--dun` 系を足したときに取り違える。長いほうを先に置く。
            options.synthetic_dun_level = std::atoi(std::string(opt.substr(kDunLevelArg.size())).c_str());
        } else if (opt.starts_with(kDungeonArg)) {
            options.synthetic_dungeon_id = std::atoi(std::string(opt.substr(kDungeonArg.size())).c_str());
        } else if (opt.starts_with(kPostArg)) {
            options.post_spec = std::string(opt.substr(kPostArg.size()));
            options.post_from_args = true;
        } else if (opt.starts_with(kLutExportArg)) {
            options.lut_export_path = std::string(opt.substr(kLutExportArg.size()));
        } else if (opt.starts_with(kLutArg)) {
            options.lut_path = std::string(opt.substr(kLutArg.size()));
        } else if (opt.starts_with(kCutawayArg)) {
            options.cutaway_radius = std::max(0.f,
                static_cast<float>(std::atof(std::string(opt.substr(kCutawayArg.size())).c_str())));
            options.cutaway_from_args = true;
        } else if (opt.starts_with(kDofArg)) {
            const float asked = static_cast<float>(std::atof(std::string(opt.substr(kDofArg.size())).c_str()));
            options.dof_strength = (asked < 0.f) ? 0.f : ((asked > 2.4f) ? 2.4f : asked);
            options.dof_from_args = true;
        } else if (opt == kPostCheckArg) {
            options.post_check = true;
        } else if (opt == kCutawayCheckArg) {
            options.cutaway_check = true;
        } else if (opt.starts_with(kShrinkCutawayArg)) {
            options.shrink_cutaway = std::atoi(std::string(opt.substr(kShrinkCutawayArg.size())).c_str());
        } else if (opt.starts_with(kLayoutArg)) {
            const std::string spec(opt.substr(kLayoutArg.size()));
            options.layout_from_args = hd2d::parse_layout_mode(spec, options.layout);
            if (!options.layout_from_args) {
                // **黙って既定に落とさない。**綴りを間違えたまま「変わらない」と読むのがいちばん高い。
                std::fprintf(stderr, "[hd2d] --layout=%s は読めません（full / hybrid / split）。"
                                     "%s のままにします\n",
                    spec.c_str(), hd2d::layout_mode_name(options.layout));
            }
        } else if (opt == kUiCheckArg) {
            options.ui_check = true;
        } else if (opt == kFullscreenArg) {
            options.ui_check_fullscreen = true;
        } else if (opt.starts_with(kGrowSceneArg)) {
            options.grow_scene = std::atoi(std::string(opt.substr(kGrowSceneArg.size())).c_str());
        } else if (opt.starts_with(kEditNewArg)) {
            options.edit_new_name = std::string(opt.substr(kEditNewArg.size()));
        } else if (opt.starts_with(kEditArg)) {
            options.edit_name = std::string(opt.substr(kEditArg.size()));
        } else if (opt == kEditCheckArg) {
            options.edit_check = true;
        } else if (opt == kVrArg) {
            options.vr = true;
        } else if (opt == kVrFakeArg) {
            options.vr_fake = true;
        } else if (opt == kVrMathCheckArg) {
            options.vr_math_check = true;
        } else if (opt == kVrCheckArg) {
            options.vr_check = true;
        } else if (opt.starts_with(kVrFramesArg)) {
            options.vr_check_frames = std::max(1, std::atoi(std::string(opt.substr(kVrFramesArg.size())).c_str()));
        } else if (opt == kFpsArg) {
            options.first_person = true;
        } else if (opt.starts_with(kFpsYawArg)) {
            options.first_person = true;
            options.first_person_yaw_deg
                = static_cast<float>(std::atof(std::string(opt.substr(kFpsYawArg.size())).c_str()));
        } else if (opt.starts_with(kTurnArg)) {
            const int turn = std::atoi(std::string(opt.substr(kTurnArg.size())).c_str());
            options.camera_turn = ((turn % hd2d::kViewTurnCount) + hd2d::kViewTurnCount) % hd2d::kViewTurnCount; //!< 0..7（1 段 45°）。範囲外は黙って丸める
        } else if (opt == kOriginalArg) {
            options.original_panel = true;
        } else if (opt.starts_with(kEntityArg)) {
            options.entity_style = std::string(opt.substr(kEntityArg.size()));
        } else if (opt.starts_with(kBgmArg)) {
            //! 綴りの検査は `hd2d::run` の側（`--look=` と同じ分担）。
            options.bgm_mode = std::string(opt.substr(kBgmArg.size()));
        } else if (opt.starts_with(kLookArg)) {
            //! 綴りの検査は `hd2d::run` の側（そこが cfg と突き合わせる唯一の場所）。
            options.scene_look = std::string(opt.substr(kLookArg.size()));
        } else if (opt == kMaterialCheckArg) {
            options.material_check = true;
        } else if (opt.starts_with(kLeafArg)) {
            options.leaf = std::string(opt.substr(kLeafArg.size()));
        } else if (opt.starts_with(kVignetteArg)) {
            options.vignette_strength = static_cast<float>(std::atof(std::string(opt.substr(kVignetteArg.size())).c_str()));
        } else if (opt.starts_with(kSepiaArg)) {
            options.sepia = static_cast<float>(std::atof(std::string(opt.substr(kSepiaArg.size())).c_str()));
        } else if (opt.starts_with(kHdrArg)) {
            options.hdr = static_cast<float>(std::atof(std::string(opt.substr(kHdrArg.size())).c_str()));
        } else if (opt.starts_with(kDustArg)) {
            options.dust = static_cast<float>(std::atof(std::string(opt.substr(kDustArg.size())).c_str()));
        } else if (opt.starts_with(kExposureArg)) {
            options.exposure = static_cast<float>(std::atof(std::string(opt.substr(kExposureArg.size())).c_str()));
        } else if (opt.starts_with(kWearMaterialsArg)) {
            options.wear_materials = std::string(opt.substr(kWearMaterialsArg.size()));
        } else if (opt.starts_with(kWearArg)) {
            //! 綴りの検査は `hd2d::run` の側（`--look=` と同じ分担）。
            options.wear = std::string(opt.substr(kWearArg.size()));
        } else if (opt.starts_with(kTronSkyValueArg)) {
            options.tron_sky = (std::atoi(std::string(opt.substr(kTronSkyValueArg.size())).c_str()) != 0) ? 1 : 0;
        } else if (opt == kTronSkyArg) {
            options.tron_sky = 1;
        } else if (opt.starts_with(kBoardTurnArg)) {
            const int turn = std::atoi(std::string(opt.substr(kBoardTurnArg.size())).c_str());
            options.vr_board_turn = ((turn % 4) + 4) % 4;
        } else {
            options.forwarded_args.emplace_back(opt);
        }
    }
    /*
     * **遊び終えたらコア選択へ戻す**（2026-08-23 に決めた。`hd2d::kRunRestart`）。
     *
     * 戻し方は「`run()` ごとやり直す」である。中で環を回すより安い——**コアごとの覚え**
     * （タイル目録・プレハブライブラリ・`pad_commands`・サブパネルの種類・町とダンジョンの意匠…）
     * が全部作り直しになるので、**写し忘れが起きようがない**。窓とライブラリを組み直すぶん
     * 1 秒半ほど掛かるが、遊びを終えた直後の 1 回だけである。
     */
    for (;;) {
        const int rc = hd2d::run(options);
        /*
         * **言語を替えたので同じコアを起こし直す**（`hd2d::kRunRelaunchCore`。追補 A2）。
         * 行き先がコア選択ではないぶんだけ `kRunRestart` と違う。申し送り
         * （どのコアか・`--resume` を渡すか）は `take_core_relaunch()` が持っている。
         */
        if (rc == hd2d::kRunRelaunchCore) {
            options.relaunch = hd2d::take_core_relaunch();
            continue;
        }
        options.relaunch = hd2d::CoreRelaunch{}; //!< ふつうのやり直しでは持ち越さない
        if (rc != hd2d::kRunRestart) {
            return rc;
        }
    }
}
