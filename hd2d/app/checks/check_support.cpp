/*!
 * @file check_support.cpp
 * @brief **検査どうし**で使い回す作り物（作り物のフレーム・町の読み込み・意匠の表）。
 *
 * @details `hd2d/app/hd2d_app.cpp` から切り出したもの（2026-09-06）。
 * 振る舞いが変わっていないことは `python tools/hd2d_verify/golden.py --check` で見る。
 */
#include "app/checks/check_support.h"

#include "app/app_support.h"
#include "render/gl_core.h"
#include "ui/combat_fx_view.h"
#include "ui/ui_paint.h"
#include "ui/ui_layout.h"
#include "ui/game_hud.h"
#include "render/camera.h"
#include "render/math3d.h"
#include "render/post_process.h"
#include "render/voxel_renderer.h"
#include "render/text_overlay.h"
#include "voxel/prefab.h"
#include "voxel/greedy_mesher.h"
#include "world/terrain_view.h"
#include "world/entity_view.h"
#include "world/overlay_view.h"
#include "frame/minimap_snapshot.h"
#include "i18n/lang.h"
#include "world/dungeon_style.h"
#include "world/floor_meaning.h"
#include "world/prefab_library.h"
#include "world/town_plan.h"
#include "frame/cell_feature_bits.h"
#include "frame/game_frame.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace hd2d {

using namespace hd2d::gl; //!< GL の型と関数は `hd2d::gl` に居る（gl_core.h）

/*!
 * @brief **検査モードにコア名を注入する入口**。
 *
 * @details 合成フレームと実データの検査（`--town-check` / `--town-view` /
 * `--terrain-check --dungeon=N`）は**コアを起こさない**ので `hello_ack.core_name` が無い。
 * コア名が無いと意匠の表（`town_styles.jsonc` / `dungeon_styles.jsonc`）を読む道が無く、
 * 別コアの意匠を 1 つも立てないまま「変わっていない」絵を検分することになる。
 *
 * 名前は **`HD2D_STYLE_CORE` へ汎用化**した——町とダンジョンで別々の旗を作ると、
 * 「町は幻想蛮怒でダンジョンは変愚」という在り得ない組み合わせが作れてしまう。
 * 先に在った **`HD2D_TOWN_CORE` も引き続き読む**（町スレッドのスクリプトと docs が使っている）。
 *
 * @return コア名。**未設定なら空**（＝従来どおりべた書きの表だけで描く）。
 */
std::string style_core_env()
{
    for (const char *const key : { "HD2D_STYLE_CORE", "HD2D_TOWN_CORE" }) {
        const char *const raw = std::getenv(key);
        if ((raw != nullptr) && (raw[0] != '\0')) {
            return std::string(raw);
        }
    }
    return std::string();
}

/*!
 * @brief 検査モードで意匠の表を 2 つとも読む（町 ＋ ダンジョン）。
 * @details 実機は握手の直後に同じ 2 つを呼ぶ。**同じ順で同じものを読む**ようにしてある
 * ——検査だけが片方を読み落とすと、検分した絵が実機と違うものになる。
 */
void load_styles_for_check(const AppOptions &options)
{
    const std::string core = style_core_env();
    if (core.empty()) {
        return;
    }
    std::string town_log;
    const bool town_took = load_town_styles(options.voxel_dir, core, &town_log);
    std::fprintf(stderr, "[hd2d] HD2D_STYLE_CORE=%s: town_styles.jsonc %s\n", core.c_str(),
        town_took ? "の節を採りました" : (town_log.empty() ? "に節がありません" : town_log.c_str()));
    std::string dun_log;
    const bool dun_took = load_dungeon_styles(options.voxel_dir, core, &dun_log);
    std::fprintf(stderr, "[hd2d] HD2D_STYLE_CORE=%s: dungeon_styles.jsonc %s\n", core.c_str(),
        dun_took ? "の節を採りました" : (dun_log.empty() ? "に節がありません" : dun_log.c_str()));
}

/*!
 * @brief `lib/edit/towns/*.txt` の実データからフレームを 1 枚組む（P10 第 2 期）。
 *
 * @details 引き継ぎ §4.2.3 の申し送り:「**実物の町を検査へ通す絵はまだ無い。罠 64 と
 * 同じ形なので、第 2 期では実データの町を読んで組み立てる検査を先に用意すること**」。
 *
 * 読むのは 2 種類の行だけ。
 * | 行 | 何 |
 * |---|---|
 * | `D:` | 地図。1 文字 1 マス |
 * | `F:<記号>:<地形キー>` | 記号の意味。**最初に出たものを既定として採る**（後続は条件つき） |
 *
 * ミニマップは「壁かどうか」しか運ばないので、`PERMANENT`／`MOUNTAIN` 系のキーに
 * 割り当てられた記号と `#` を壁にし、残りは床にする。**`T`（木）は壁ではない**
 * （8,409 マスあるので、ここを間違えると町じゅうが敷地になる）。
 *
 * @param path 町のデータ。
 * @param[out] err 読めなかった理由。
 * @return 組めたか。
 */
bool load_town_frame(const std::string &path, const PrefabLibrary &library, GameFrame &frame, std::string &err)
{
    std::vector<std::string> rows;
    std::map<char, std::string> glyph_key;
    /*!
     * @brief その記号が `F:` 行で持ちうる**全部の意味**（クエストの進行で入れ替わる）。
     *
     * @details 町のデータは `?:[EQU $QUEST29 1]` のような条件で `F:` 行を切り替える。
     * モリバントの `s` は**未受注なら `PERMANENT`・受注中なら `QUEST_ENTER`** で、
     * `glyph_key`（最初の 1 つが勝つ）では前者しか見えない。**検査が見なければならないのは
     * 後者**である——2026-08-12 に気づいた「モリバントの雑貨屋が岩山になっている」は
     * クエスト受注中にだけ出る姿で、この表が無いと検査を 1 度も通らない（罠 112 の 4 度目）。
     */
    std::map<char, std::vector<std::string>> glyph_all;
    /*
     * 記号の意味は 2 か所にある。**既定は `TownPreferences.txt`**（`#` = 永久壁、
     * `1` = 雑貨屋…）で、町ごとの上書きが `towns/*.txt` の `F:` 行。
     * 先に既定を読み、町の側は `emplace` で**上書きしない**（最初の 1 つが勝つ）ので、
     * 町の `F:` 行のほうを先に読む。
     */
    for (const std::string &file : { path, std::string("lib/edit/TownPreferences.txt") }) {
        std::FILE *fp = std::fopen(file.c_str(), "rb");
        if (fp == nullptr) {
            if (file == path) {
                err = "町のデータを開けません: " + path;
                return false;
            }
            continue; // 既定の表が無くても `#` だけで敷地は読める
        }
        char line[1024];
        while (std::fgets(line, sizeof(line), fp) != nullptr) {
            std::string text(line);
            while (!text.empty() && ((text.back() == '\n') || (text.back() == '\r'))) {
                text.pop_back();
            }
            if ((file == path) && (text.rfind("D:", 0) == 0)) {
                rows.push_back(text.substr(2));
            } else if ((text.rfind("F:", 0) == 0) && (text.size() >= 5) && (text[3] == ':')) {
                const char glyph = text[2];
                const std::size_t end = text.find(':', 4);
                const std::string key = text.substr(4, (end == std::string::npos) ? std::string::npos : (end - 4));
                glyph_key.emplace(glyph, key); //!< emplace なので**最初の 1 つだけ**が残る
                glyph_all[glyph].push_back(key);
            }
        }
        std::fclose(fp);
    }
    if (rows.empty()) {
        err = "D: の行が 1 つもありません: " + path;
        return false;
    }
    std::size_t width = 0;
    for (const std::string &row : rows) {
        width = std::max(width, row.size());
    }
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(rows.size());

    const auto is_wall_glyph = [&glyph_key](char glyph) {
        if (glyph == '#') {
            return true; // TownPreferences.txt の既定（PERMANENT）
        }
        const auto found = glyph_key.find(glyph);
        if (found == glyph_key.end()) {
            return false;
        }
        const std::string &key = found->second;
        return (key.rfind("PERMANENT", 0) == 0) || (key.rfind("MOUNTAIN", 0) == 0)
            || (key.rfind("GRANITE", 0) == 0);
    };
    //! 山の記号（`^`）。実機ではコアが `MOUNTAIN` から `MinimapKind::Mountain` を立てる。
    const auto is_mountain_glyph = [&glyph_key](char glyph) {
        const auto found = glyph_key.find(glyph);
        return (found != glyph_key.end()) && (found->second.rfind("MOUNTAIN", 0) == 0);
    };

    /*
     * **階段の記号**（2026-08-11）。町の `>` は `TownPreferences.txt` で `ENTRANCE`
     * ——つまり**ダンジョンの入口**である（辺境の地の `>` はイークの洞窟の口）。
     *
     * ここを見ていなかったので、`--town-check` の中では階段が 1 マスも無い町が出来ていた。
     * 実機ではコアが `STAIRS` の印から `MinimapKind::Stairs` を立てるので、
     * **検査だけが違う前提で走っていた**（罠 112）。
     *
     * **クエストの入口も階段である**（2026-08-12）。`QUEST_ENTER` は `STAIRS` を持つので、
     * 実機ではクエストを受けている間だけ町に階段が増える。ここは `glyph_all` を見て
     * 「その記号が階段になりうるなら階段」として扱う——**いちばん厳しい状態**を検査に
     * 通すためで、モリバントの `s`（未受注 `PERMANENT` / 受注中 `QUEST_ENTER`）は
     * 受注中の姿で読む。
     */
    const auto is_stairs_glyph = [&glyph_all](char glyph) {
        const auto found = glyph_all.find(glyph);
        if (found == glyph_all.end()) {
            return false;
        }
        for (const std::string &key : found->second) {
            if ((key.rfind("ENTRANCE", 0) == 0) || (key.rfind("UP_STAIR", 0) == 0)
                || (key.rfind("DOWN_STAIR", 0) == 0) || (key.rfind("SHAFT_", 0) == 0)
                || (key.rfind("QUEST_", 0) == 0) || (key.rfind("TOWN_EXIT", 0) == 0)) {
                return true;
            }
        }
        return false;
    };

    frame = GameFrame{};
    frame.view_w = 66;
    frame.view_h = 22;
    frame.floor.kind = static_cast<int>(FloorKind::Surface);
    /*
     * 何番の町かは**ファイル名の頭の数字**から採る（`03_Morivant.txt` → 3）。
     * `town_id` は町ごとの意匠（`TownStyle`）を引く鍵でもあるので、ここを 1 に決め打ちすると
     * **どの町を渡してもテルモラとモリバントの素材を 1 度も通らない**（罠 64 と同じ形＝
     * 検査の絵に無い条件は検出できない）。**0 だと町の読み取りが走らない**（荒野の扱い）。
     */
    frame.floor.town_id = [&path]() {
        const std::size_t slash = path.find_last_of("/\\");
        const std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
        int id = 0;
        for (std::size_t i = 0; (i < base.size()) && (base[i] >= '0') && (base[i] <= '9'); ++i) {
            id = (id * 10) + (base[i] - '0');
        }
        return (id > 0) ? id : 1;
    }();
    frame.floor.generated_turn = 12345;
    frame.lighting.day_minute = 12 * 60;
    frame.lighting.daytime = true;
    frame.minimap.width = w;
    frame.minimap.height = h;
    frame.minimap.kinds.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h),
        static_cast<std::uint8_t>(MinimapKind::Floor));
    for (int gy = 0; gy < h; ++gy) {
        for (int gx = 0; gx < w; ++gx) {
            const char glyph = (static_cast<std::size_t>(gx) < rows[static_cast<std::size_t>(gy)].size())
                ? rows[static_cast<std::size_t>(gy)][static_cast<std::size_t>(gx)]
                : ' ';
            /*
             * **階段を壁より先に見る。**モリバントの `s` は「未受注なら永久壁・受注中なら
             * クエスト入口」の両方の顔を持つので、壁を先に見ると受注中の姿が作れない。
             */
            const auto kind = is_stairs_glyph(glyph)
                ? MinimapKind::Stairs
                : (is_mountain_glyph(glyph) ? MinimapKind::Mountain
                                            : (is_wall_glyph(glyph) ? MinimapKind::Wall : MinimapKind::Floor));
            if (kind != MinimapKind::Floor) {
                frame.minimap.kinds[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(w))
                    + static_cast<std::size_t>(gx)]
                    = static_cast<std::uint8_t>(kind);
            }
        }
    }
    /*
     * 可視窓（`cells`）にも**町の全域**を入れる。`terrain_id` は記号の意味（`F:` の key）を
     * 表で引いて求める——こうすると「店の種別ごとに違う看板が出るか」まで実データで見られる。
     * 実機では可視窓ぶんしか届かないが、検査は「対応が通っているか」を見るのが目的である。
     */
    frame.view_w = w;
    frame.view_h = h;
    frame.cells.reserve(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int gy = 0; gy < h; ++gy) {
        for (int gx = 0; gx < w; ++gx) {
            const char glyph = (static_cast<std::size_t>(gx) < rows[static_cast<std::size_t>(gy)].size())
                ? rows[static_cast<std::size_t>(gy)][static_cast<std::size_t>(gx)]
                : ' ';
            MapCellView cell{};
            cell.gx = static_cast<std::int16_t>(gx);
            cell.gy = static_cast<std::int16_t>(gy);
            cell.feature_flags = CELL_FEAT_KNOWN;
            const auto found = glyph_key.find(glyph);
            if (found != glyph_key.end()) {
                cell.terrain_id = static_cast<std::uint16_t>(library.terrain_id_for_key(found->second));
            }
            frame.cells.push_back(cell);
        }
    }

    //! プレイヤは町の真ん中の街路に立たせる（視錐台の検査で使う）。
    frame.player_gx = w / 2;
    frame.player_gy = h / 2;
    frame.cam_x = frame.player_gx;
    frame.cam_y = frame.player_gy;
    frame.minimap.player_gx = frame.player_gx;
    frame.minimap.player_gy = frame.player_gy;
    return true;
}

/*!
 * @brief 検査用の合成フレームを作る（床と壁が市松に混ざったフロア）。
 * @details 実プレイに頼ると「セーブの中身しだいで通ったり落ちたりする検査」になる。
 * 合成にすれば毎回同じものを見られる。
 */
GameFrame make_synthetic_frame(int view_w, int view_h, int player_gx, int player_gy)
{
    GameFrame frame{};
    frame.view_w = view_w;
    frame.view_h = view_h;
    frame.player_gx = player_gx;
    frame.player_gy = player_gy;
    frame.cam_x = player_gx;
    frame.cam_y = player_gy;
    frame.floor.dungeon_id = 1;
    frame.floor.dun_level = 5;
    frame.floor.generated_turn = 4242;
    frame.floor.kind = static_cast<int>(FloorKind::Dungeon);
    /*
     * 光（P5）。**地下なので昼夜は効かない**が、松明は効く。
     * 半径 3 は素の松明。ここを 0 にすると点光源が 1 つも無い絵になるので、
     * 「点光源の経路をこの検査が通っているか」が分からなくなる。
     */
    frame.lighting.day_minute = 720;
    frame.lighting.daytime = true;
    frame.lighting.light_radius = 3;

    /*
     * 意味づけ層はフロア全域（ミニマップ）を読むので、こちらも全域を作る。
     * **部屋と通路の両方が出る形**にしないと「部屋と通路が別に見えるか」を確かめられない:
     * 5 マス間隔の格子の交点を部屋、線のところを幅 1 の通路、残りを岩盤にする。
     */
    const int w = 198;
    const int h = 66;
    frame.minimap.width = w;
    frame.minimap.height = h;
    frame.minimap.player_gx = player_gx;
    frame.minimap.player_gy = player_gy;
    frame.minimap.kinds.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h),
        static_cast<std::uint8_t>(MinimapKind::Wall));
    for (int y = 1; y < (h - 1); ++y) {
        for (int x = 1; x < (w - 1); ++x) {
            const bool room = ((x % 16) >= 2) && ((x % 16) <= 7) && ((y % 14) >= 2) && ((y % 14) <= 6);
            const bool corridor = ((x % 16) == 10) || ((y % 14) == 9);
            const auto kind = (room || corridor) ? MinimapKind::Floor : MinimapKind::Wall;
            frame.minimap.kinds[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)]
                = static_cast<std::uint8_t>(kind);
        }
    }
    frame.minimap.kinds[(static_cast<std::size_t>(player_gy) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(player_gx)]
        = static_cast<std::uint8_t>(MinimapKind::Player);

    const int ox = player_gx - (view_w / 2);
    const int oy = player_gy - (view_h / 2);
    frame.cells.reserve(static_cast<std::size_t>(view_w) * static_cast<std::size_t>(view_h));
    for (int vy = 0; vy < view_h; ++vy) {
        for (int vx = 0; vx < view_w; ++vx) {
            MapCellView cell{};
            cell.gx = static_cast<std::int16_t>(ox + vx);
            cell.gy = static_cast<std::int16_t>(oy + vy);
            cell.feature_flags = CELL_FEAT_KNOWN;
            /*
             * 溶岩の池を 1 つ置く（P7）。**点光源と自発光の両方の経路をこの検査に通すため**で、
             * ここが無いと「地下にブルームの相手が 1 つも無い」絵しか作れない
             * （実際 P7 で最初にそうなり、閾値の議論が空回りした）。
             * 部屋は x 98〜103・y 44〜48（16×14 の格子から出る）。**部屋の中に収まる**
             * 東へ 1〜2・南へ 2〜3 の 2×2 に置く（壁のマスに置くと床の板が出ない）。
             */
            const int lava_x = cell.gx - (player_gx + 1);
            const int lava_y = cell.gy - (player_gy + 2);
            if ((lava_x >= 0) && (lava_x < 2) && (lava_y >= 0) && (lava_y < 2)) {
                cell.feature_flags |= CELL_FEAT_LAVA;
            }
            /*
             * 地形の細別（P10）。ミニマップと同じ規則で床 / 壁を敷き、溶岩の池には
             * DEEP_LAVA を、壁の一部には鉱脈を入れる。**id は対応表と同じ本物の値**
             * （床 1・花崗岩 56・磁鉄鉱脈 50・深い溶岩 85。lib/edit/TerrainDefinitions.jsonc）。
             * これが無いと `--terrain-check` 系は役割の既定しか通らず、
             * 「terrain_id → プレハブ」の経路を 1 度も描かないまま PASS してしまう。
             */
            if ((cell.gx > 0) && (cell.gx < (w - 1)) && (cell.gy > 0) && (cell.gy < (h - 1))) {
                const bool room = ((cell.gx % 16) >= 2) && ((cell.gx % 16) <= 7)
                    && ((cell.gy % 14) >= 2) && ((cell.gy % 14) <= 6);
                const bool corridor = ((cell.gx % 16) == 10) || ((cell.gy % 14) == 9);
                if (room) {
                    // 部屋は**明るい部屋**として作る（CAVE_ROOM + CAVE_GLOW 相当。
                    // 松明の装飾は「明るいと確定した部屋」にしか出ないので、これが無いと
                    // 検査が松明の経路を一度も通らない）。
                    cell.feature_flags |= CELL_FEAT_ROOM | CELL_FEAT_GLOWING;
                }
                if ((cell.feature_flags & CELL_FEAT_LAVA) != 0u) {
                    cell.terrain_id = 85; // DEEP_LAVA
                } else if (room || corridor) {
                    cell.terrain_id = 1; // FLOOR
                } else {
                    cell.terrain_id = ((((cell.gx * 7) + (cell.gy * 3)) % 23) == 0) ? 50 : 56; // MAGMA_VEIN / GRANITE
                    cell.feature_flags |= CELL_FEAT_WALL;
                }
            }
            frame.cells.push_back(cell);
        }
    }
    return frame;
}

/*!
 * @brief 引数の**画面全体の色処理**（`--vignette=` `--sepia=` `--hdr=` `--exposure=`）を掛ける。
 * @details `apply_detail_options` と同じく**cfg を読まない道のための口**である
 * （素材見物・町の下見）。打たれていない欄（負）は触らない＝既定のまま。
 * @note セピアは LUT なので、呼ぶ側が `post.set_grade(sepia_grade(...))` を焼くこと。
 */
void apply_color_options(const AppOptions &options, PostParams &params)
{
    if (options.vignette_strength >= 0.f) {
        params.vignette_strength = options.vignette_strength;
    }
    if (options.hdr >= 0.f) {
        params.hdr = options.hdr;
    }
    if (options.exposure >= 0.f) {
        params.exposure = options.exposure;
    }
    //! 被写界深度の強さ（`--dof=`）。埃のぼけ方を見るのにここが要る。
    params.dof_strength = options.dof_strength;
}

/*!
 * @brief 引数の面のディテール（`--wear=` / `--leaf=`）を描き手へ渡す。
 *
 * @details **cfg を読まない道のための手段**である（素材見物 `--prefab=`・町の下見 `--town-view`）。
 * 本編は `Hd2dSettings` から毎フレーム渡すので、ここは通らない。
 *
 * 打たれなければ何もしない＝**以前の絵と 1 ビットも同じ**。最初これを町の下見にしか
 * 足さず、`--prefab=` で撮り比べて「off と on が 1 画素も違わない」と読み違えた
 * （渡していないのだから当然で、シェーダには何の問題も無かった）。
 */
void apply_detail_options(const AppOptions &options, VoxelRenderer &renderer)
{
    if (!options.wear.empty() || !options.wear_materials.empty()) {
        WearParams wear;
        bool ok = true;
        if (!options.wear.empty() && !parse_wear_amount(options.wear, wear.amount)) {
            std::fprintf(stderr, "[hd2d] --wear= の値が読めません: %s（off / on / 0.0〜2.0）\n",
                options.wear.c_str());
            ok = false;
        }
        //! **材質ごとの入切**（`--wear-materials=`。2026-08-23 に決めた）。
        if (!options.wear_materials.empty() && !parse_wear_materials(options.wear_materials, wear.materials)) {
            std::fprintf(stderr, "[hd2d] --wear-materials= が読めません: %s（all / none / stone,wood,…）\n",
                options.wear_materials.c_str());
            ok = false;
        }
        if (ok) {
            renderer.set_wear(wear);
        }
    }
    if (!options.leaf.empty()) {
        LeafParams leaf;
        if (parse_leaf_amount(options.leaf, leaf.amount)) {
            renderer.set_leaf(leaf);
        } else {
            std::fprintf(stderr, "[hd2d] --leaf= の値が読めません: %s（off / on / 0.0〜2.0）\n",
                options.leaf.c_str());
        }
    }
}

//! 2 枚の絵で「どこかの成分が `tolerance` を超えて違う」画素の数。
std::size_t count_changed_pixels(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b,
    int tolerance)
{
    if (a.size() != b.size()) {
        return a.size();
    }
    std::size_t changed = 0;
    for (std::size_t i = 0; i < a.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            if (std::abs(static_cast<int>(a[i + static_cast<std::size_t>(c)])
                    - static_cast<int>(b[i + static_cast<std::size_t>(c)]))
                > tolerance) {
                ++changed;
                break;
            }
        }
    }
    return changed;
}

/*!
 * @brief 2 枚の絵が**指定した矩形の中で**違う画素の数。
 * @details 全面を比べる `count_changed_pixels()` と違い、見たい板だけを見る。
 * ミニマップのように「窓の一角だけが変わる」ものは、全面で比べると
 * 他の板の 1 画素の揺れと見分けがつかない。
 */
std::size_t count_changed_pixels(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b,
    int width, int height, const RectPx &area)
{
    if (a.size() != b.size()) {
        return a.size();
    }
    std::size_t changed = 0;
    for (int y = area.y; y < (area.y + area.h); ++y) {
        for (int x = area.x; x < (area.x + area.w); ++x) {
            if ((x < 0) || (y < 0) || (x >= width) || (y >= height)) {
                continue;
            }
            int lhs[3]{};
            int rhs[3]{};
            pixel_at(a, width, height, x, y, lhs);
            pixel_at(b, width, height, x, y, rhs);
            if ((lhs[0] != rhs[0]) || (lhs[1] != rhs[1]) || (lhs[2] != rhs[2])) {
                ++changed;
            }
        }
    }
    return changed;
}

/*!
 * @brief 画面の四隅に**背景色（下塗り）が残っていないか**を数える（P2 の検証）。
 *
 * @details 設計書 §14-4／旧設計書 §17 R-1 の教訓そのもの:
 * > 四隅の欠けチェックが「0.0%」と報告し続けていたが、実際は**下塗り色を数えておらず、
 * > そもそも数えていなかった**。
 *
 * だからここは**消去色そのものを数える**。可視窓が足りなければ地形が届かず、
 * 消去色が四隅に残る。`--terrain-check` は窓をわざと狭められるようにしてあり、
 * **狭めれば必ずこの数が跳ね上がる**ことを確かめてある。
 *
 * @param clear 消去色（0..255 の RGB）。これと同じ画素を「欠け」と数える。
 * @return 四隅の枠のうち欠けていた画素の割合（0..1）。
 */
double count_corner_gaps(int width, int height, const unsigned char clear[3], int corner_px, std::string &report)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    const int size = std::min({ corner_px, width / 2, height / 2 });
    struct Corner {
        const char *name;
        int x0;
        int y0;
    };
    const Corner corners[4] = {
        { "左上", 0, height - size },
        { "右上", width - size, height - size },
        { "左下", 0, 0 },
        { "右下", width - size, 0 },
    };

    long long total = 0;
    long long gaps = 0;
    report.clear();
    for (const auto &corner : corners) {
        long long here = 0;
        for (int y = corner.y0; y < (corner.y0 + size); ++y) {
            for (int x = corner.x0; x < (corner.x0 + size); ++x) {
                const std::size_t at = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x)) * 4;
                // **消去色そのもの**を数える（±1 は 8bit の丸め差を吸うため）。
                const bool is_clear = (std::abs(static_cast<int>(pixels[at + 0]) - clear[0]) <= 1)
                    && (std::abs(static_cast<int>(pixels[at + 1]) - clear[1]) <= 1)
                    && (std::abs(static_cast<int>(pixels[at + 2]) - clear[2]) <= 1);
                if (is_clear) {
                    ++here;
                }
            }
        }
        total += static_cast<long long>(size) * size;
        gaps += here;
        char buf[64]{};
        std::snprintf(buf, sizeof(buf), "%s %.1f%%  ", corner.name,
            (100.0 * static_cast<double>(here)) / static_cast<double>(size * size));
        report += buf;
    }
    return (total > 0) ? (static_cast<double>(gaps) / static_cast<double>(total)) : 0.0;
}

//! ある矩形の中で、下塗りと違う色になった画素の数。
std::size_t count_painted_pixels(const std::vector<unsigned char> &image, int width, int height,
    const RectPx &area, const unsigned char clear[3])
{
    std::size_t painted = 0;
    for (int y = area.y; y < (area.y + area.h); ++y) {
        for (int x = area.x; x < (area.x + area.w); ++x) {
            if ((x < 0) || (y < 0) || (x >= width) || (y >= height)) {
                continue;
            }
            int rgb[3]{};
            pixel_at(image, width, height, x, y, rgb);
            if ((std::abs(rgb[0] - static_cast<int>(clear[0])) > 2)
                || (std::abs(rgb[1] - static_cast<int>(clear[1])) > 2)
                || (std::abs(rgb[2] - static_cast<int>(clear[2])) > 2)) {
                ++painted;
            }
        }
    }
    return painted;
}

/*!
 * @brief 検査用のフレームへ UI の中身を足す。
 * @details `make_synthetic_frame()` は地形しか持たない（P2〜P7 はそれで足りた）。
 * P8 が見たいのは**状態列・サブパネル・最下行・メッセージ**なので、ここで足す。
 * 実プレイに頼らないのは今までと同じ理由（セーブの中身しだいで通ったり落ちたりする）。
 */
void fill_synthetic_ui(GameFrame &frame)
{
    frame.hud.name = "検査用のキャラクター";
    frame.hud.hp = 137;
    frame.hud.hp_max = 214;
    frame.hud.sp = 12;
    frame.hud.sp_max = 40;
    frame.hud.gold = 12345;
    frame.hud.level = 27;
    frame.hud.depth = 5;

    // 状態列（コアの左フレーム 13 桁の写し。K-28）。
    for (int i = 0; i < 18; ++i) {
        char line[32]{};
        std::snprintf(line, sizeof(line), "STATUS %5d", i);
        frame.status_col_lines.push_back(SubPanelLine{ line, static_cast<std::uint8_t>(1 + (i % 15)) });
    }

    // サブパネル。**1 枚だけコアのサブウインドウ**にして、既定と両方の経路を通す。
    frame.sub_panels[2].kind = 3;
    frame.sub_panels[2].title_utf8 = "サブウインドウ";
    for (int i = 0; i < 10; ++i) {
        char line[64]{};
        std::snprintf(line, sizeof(line), "sub line %d ---------------", i);
        frame.sub_panels[2].lines.push_back(SubPanelLine{ line, 9 });
    }
    for (int i = 0; i < 8; ++i) {
        frame.sub2_lines.emplace_back("装備 " + std::to_string(i));
        frame.sub3_lines.emplace_back("敵 " + std::to_string(i));
        frame.sub5_lines.emplace_back("持ち物 " + std::to_string(i));
    }

    // 最下行（桁と色に意味がある。K-32）。
    frame.bottom_row_cols = 80;
    frame.bottom_row_runs.push_back(TermTextRun{ 0, "空腹", 11 });
    frame.bottom_row_runs.push_back(TermTextRun{ 56, "遅い(-3)", 12 });
    frame.bottom_row_runs.push_back(TermTextRun{ 72, "5 階", 9 });
    frame.controller_hint = "F8:パネル F9:画面の作り F10:機能メニュー";
    frame.depth.text_utf8 = "5 階";
    frame.depth.color = 9;

    for (int i = 0; i < 4; ++i) {
        MessageEvent message{};
        message.text_utf8 = "メッセージ " + std::to_string(i);
        message.color = 1;
        frame.messages.push_back(message);
    }

    /*
     * プロンプト。**サブパネルを開いていても消えないこと**を検査に通すために要る
     * （開閉で出たり消えたりするものだと、答えられない場面が作れてしまう）。
     */
    frame.prompt.text_utf8 = "本当に飲みますか? [y/n]";
    frame.prompt.choices.push_back(PromptChoice{ "はい", 'y', 12, 1 });
    frame.prompt.choices.push_back(PromptChoice{ "いいえ", 'n', 14, 1 });
}

/*!
 * @brief 北向き入口を模した合成フレーム（`--cutaway-check` 用）。
 *
 * @details 設計書 §10.2 の形をそのまま作る。**通りの南側に建っている店**である。
 * ```
 *              北（画面の奥）
 *      . . . . . . . . . .      街路
 *      . . . . @ . . . . .      ← プレイヤ（入口のマス）
 *      # # # # # # # # # #      ┐
 *      # # # # # # # # # #      │ 建物（敷地）
 *      # # # # # # # # # #      │
 *      # # # # # # # # # #      ┘
 *              南（カメラ）
 * ```
 * カメラは注視点の**南**にある（`Camera::eye()`）ので、建物はカメラとプレイヤの間に入る。
 * これが「カメラを回しても直らない、構造的な遮蔽」である（§10.2）。
 */
GameFrame make_entrance_frame(int view_w, int view_h, int player_gx, int player_gy)
{
    GameFrame frame{};
    frame.view_w = view_w;
    frame.view_h = view_h;
    frame.player_gx = player_gx;
    frame.player_gy = player_gy;
    frame.cam_x = player_gx;
    frame.cam_y = player_gy;
    frame.floor.dungeon_id = 0;
    frame.floor.dun_level = 0;
    frame.floor.generated_turn = 777;
    frame.floor.kind = static_cast<int>(FloorKind::Surface);
    frame.lighting.day_minute = 12 * 60;
    frame.lighting.daytime = true;
    frame.lighting.light_radius = 0; //!< 昼の町。松明は関係ない

    const int w = 198;
    const int h = 66;
    frame.minimap.width = w;
    frame.minimap.height = h;
    frame.minimap.player_gx = player_gx;
    frame.minimap.player_gy = player_gy;
    // 町なので**基本は開けた街路**。建物だけを壁にする。
    frame.minimap.kinds.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h),
        static_cast<std::uint8_t>(MinimapKind::Floor));
    const auto put = [&](int x, int y, MinimapKind kind) {
        if ((x < 0) || (y < 0) || (x >= w) || (y >= h)) {
            return;
        }
        frame.minimap.kinds[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)]
            = static_cast<std::uint8_t>(kind);
    };
    // 建物（敷地）。プレイヤの 1 マス南から 5 マスぶん、幅 13 マス。
    for (int dy = 1; dy <= 5; ++dy) {
        for (int dx = -6; dx <= 6; ++dx) {
            put(player_gx + dx, player_gy + dy, MinimapKind::Wall);
        }
    }
    put(player_gx, player_gy, MinimapKind::Player);

    const int ox = player_gx - (view_w / 2);
    const int oy = player_gy - (view_h / 2);
    frame.cells.reserve(static_cast<std::size_t>(view_w) * static_cast<std::size_t>(view_h));
    for (int vy = 0; vy < view_h; ++vy) {
        for (int vx = 0; vx < view_w; ++vx) {
            MapCellView cell{};
            cell.gx = static_cast<std::int16_t>(ox + vx);
            cell.gy = static_cast<std::int16_t>(oy + vy);
            cell.feature_flags = CELL_FEAT_KNOWN;
            frame.cells.push_back(cell);
        }
    }
    return frame;
}

/*!
 * @brief 合成の見せ場を 1 枚重ねる（`--combat-fx-check`）。
 * @param cx,cy 中心のマス（合成フロアは 100,44 に立っている）
 * @details **描くのは本番と同じ `draw_combat_fx()`**（記憶 `hengband-check-must-not-copy-the-draw`）。
 * ここが作るのは「積む縁」と「時計」だけである。
 *
 * 時計をずらして積むのは、**3 種を同時に、しかも途中の姿で**出すため。
 * 寿命は種類ごとに違う（飛道 180ms・命中 220ms・被弾 260ms）ので、同じ時刻に積むと
 * 飛道だけが先に消えるか、全部が出たての姿になる。ここでは
 * 「飛道は半分ほど進み、命中は開きかけ、被弾は薄れ始め」で止めてある。
 *
 * 属性は**束を全部**出す（`CombatFxElement` の 12 種）。色を決めているのは画面側なので、
 * 1 つでも取り違えると気づけない——並べて撮れば一目で分かる。
 */
void overlay_combat_fx(UiPaint &paint, const Camera &camera, int screen_w, int screen_h, float cx, float cy)
{
    const RectPx scene{ 0, 0, screen_w, screen_h };
    CombatFxView view;

    /*
     * 束を横一列に並べる。`Physical` から `Insanity` まで 12 種。
     * **束を足したらここも足すこと**——増やし忘れても落ちず、
     * **新しい束が検査の画に写らないだけ**なので気づけない
     */
    constexpr int kElements = 12;
    std::vector<CombatFxEvent> bolts;
    std::vector<CombatFxEvent> sparks;
    for (int i = 0; i < kElements; ++i) {
        const auto elem = static_cast<CombatFxElement>(i);
        //! 2 マスおきに置くので、左端は「枚数 − 1」マスぶん左（12 種なら 11）。
        const auto dx = static_cast<std::int16_t>(cx - (kElements - 1) + (i * 2));

        CombatFxEvent bolt{};
        bolt.kind = CombatFxKind::Bolt;
        bolt.element = elem;
        bolt.src_y = static_cast<std::int16_t>(cy + 5);
        bolt.src_x = dx;
        bolt.y = static_cast<std::int16_t>(cy - 4);
        bolt.x = dx;
        bolt.intensity = 1.f;
        bolts.push_back(bolt);

        CombatFxEvent spark{};
        spark.kind = CombatFxKind::HitMonster;
        spark.element = elem;
        spark.y = static_cast<std::int16_t>(cy - 4);
        spark.x = dx;
        spark.src_y = spark.y;
        spark.src_x = spark.x;
        //! 強さは端から端まで振る（大きさが強さで変わることも 1 枚で見えるように）。
        spark.intensity = 0.15f + (0.85f * (static_cast<float>(i) / static_cast<float>(kElements - 1)));
        sparks.push_back(spark);
    }
    //! 被弾は全面に敷く 1 件だけ（複数あっても一番強いものが勝つ作り）。
    std::vector<CombatFxEvent> hits;
    {
        CombatFxEvent hit{};
        hit.kind = CombatFxKind::HitPlayer;
        hit.element = CombatFxElement::Fire;
        hit.y = static_cast<std::int16_t>(cy);
        hit.x = static_cast<std::int16_t>(cx);
        hit.src_y = hit.y;
        hit.src_x = hit.x;
        hit.intensity = 0.55f;
        hits.push_back(hit);
    }

    //! 時計は 0 から。積む時刻をずらして「途中の姿」を作る（上の注記）。
    constexpr std::uint32_t kNow = 1000;
    view.push(hits, kNow - 150); //!< 被弾 260ms のうち 150ms 経過
    view.push(sparks, kNow - 80); //!< 命中 220ms のうち 80ms 経過
    view.push(bolts, kNow - 100); //!< 飛道 180ms のうち 100ms 経過（頭が 55% まで進む）
    view.update(kNow);

    const auto n_bolts = view.bolts().size();
    const auto n_sparks = view.sparks().size();
    FxColor flash_color;
    float flash_alpha = 0.f;
    const bool has_flash = view.flash(flash_color, flash_alpha);

    paint.begin(screen_w, screen_h);
    draw_combat_fx(paint, view, camera, scene, true);
    paint.flush();

    std::fprintf(stderr, "  戦闘の見せ場: 飛道 %zu 本・命中 %zu 個・被弾 %s（濃さ %.2f）\n",
        n_bolts, n_sparks, has_flash ? "あり" : "なし", static_cast<double>(flash_alpha));
    if ((n_bolts != kElements) || (n_sparks != kElements) || !has_flash) {
        std::fprintf(stderr, "  **戦闘の見せ場: 積んだ数と出た数が合いません"
                             "（束 %d 種のはず）**\n", kElements);
    }
}

//! ある画素の RGB を取り出す（左上原点で指す。人が図と突き合わせるため）。
void pixel_at(const std::vector<unsigned char> &image, int width, int height, int x, int y, int rgb[3])
{
    const int flipped = std::clamp(height - 1 - y, 0, height - 1);
    const std::size_t index = ((static_cast<std::size_t>(flipped) * static_cast<std::size_t>(width))
                                  + static_cast<std::size_t>(std::clamp(x, 0, width - 1)))
        * 4;
    for (int c = 0; c < 3; ++c) {
        rgb[c] = image[index + static_cast<std::size_t>(c)];
    }
}

/*!
 * @brief プレハブ全体（全パーツ）の外接直方体。**マス単位**で返す。
 * @details 世界の単位はマスで、パーツのローカルだけがボクセル単位（`VoxelRenderer::upload`
 * が 1/`voxels_per_cell` の縮尺を model 行列に入れている）。ここもそれに合わせる。
 */
void prefab_bounds(const Prefab &prefab, Vec3 &min_out, Vec3 &max_out)
{
    const float scale = 1.f / static_cast<float>(std::max(1, prefab.voxels_per_cell));
    bool first = true;
    for (const auto &part : prefab.parts) {
        const VoxModel &model = prefab.vox.models[static_cast<std::size_t>(part.model_index)];
        const Vec3 lo{ part.offset[0] * scale, part.offset[1] * scale, part.offset[2] * scale };
        const Vec3 hi{ lo.x + (static_cast<float>(model.size[0]) * scale),
            lo.y + (static_cast<float>(model.size[1]) * scale),
            lo.z + (static_cast<float>(model.size[2]) * scale) };
        if (first) {
            min_out = lo;
            max_out = hi;
            first = false;
            continue;
        }
        min_out.x = std::min(min_out.x, lo.x);
        min_out.y = std::min(min_out.y, lo.y);
        min_out.z = std::min(min_out.z, lo.z);
        max_out.x = std::max(max_out.x, hi.x);
        max_out.y = std::max(max_out.y, hi.y);
        max_out.z = std::max(max_out.z, hi.z);
    }
}

/*!
 * @brief `--terrain-check`: 合成フレームで地形を描き、往復検査と四隅の欠けを見る。
 * @details `--shrink-view=N` を足すと**要求する可視窓を N マス狭める**。
 * 検査が本物なら、狭めた瞬間に四隅の欠けが出る（＝検査の検査）。
 */
/*!
 * @name 画素を数える道具。**`run_terrain_check` より前に置いてある**
 * @details もとは後ろに在って地形の検査から使えなかった。アスキー実体の検査（(7)）が
 * 要ったので前へ動かした——**中身は 1 文字も変えていない**。
 * @{
 */
std::vector<unsigned char> read_framebuffer(int width, int height)
{
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    return pixels;
}

//! 2 つの矩形が重なっている面積（0 なら重なっていない）。
long long rect_overlap_area(const RectPx &a, const RectPx &b)
{
    if (a.empty() || b.empty()) {
        return 0;
    }
    const int x0 = std::max(a.x, b.x);
    const int y0 = std::max(a.y, b.y);
    const int x1 = std::min(a.x + a.w, b.x + b.w);
    const int y1 = std::min(a.y + a.h, b.y + b.h);
    if ((x1 <= x0) || (y1 <= y0)) {
        return 0;
    }
    return static_cast<long long>(x1 - x0) * static_cast<long long>(y1 - y0);
}

//! 作った LUT を CPU で三線形に引く（GPU の結果と突き合わせるため）。
void sample_lut_cpu(const std::vector<unsigned char> &lut, int size, const float in[3], float out[3])
{
    const float last = static_cast<float>(size - 1);
    float coord[3]{};
    int base[3]{};
    float frac[3]{};
    for (int c = 0; c < 3; ++c) {
        coord[c] = std::clamp(in[c], 0.f, 1.f) * last;
        base[c] = std::min(static_cast<int>(coord[c]), size - 2);
        frac[c] = coord[c] - static_cast<float>(base[c]);
    }
    for (int c = 0; c < 3; ++c) {
        out[c] = 0.f;
    }
    for (int corner = 0; corner < 8; ++corner) {
        const int ir = base[0] + (corner & 1);
        const int ig = base[1] + ((corner >> 1) & 1);
        const int ib = base[2] + ((corner >> 2) & 1);
        const float weight = (((corner & 1) != 0) ? frac[0] : (1.f - frac[0]))
            * ((((corner >> 1) & 1) != 0) ? frac[1] : (1.f - frac[1]))
            * ((((corner >> 2) & 1) != 0) ? frac[2] : (1.f - frac[2]));
        const std::size_t index = (static_cast<std::size_t>((ib * size * size) + (ig * size) + ir)) * 4;
        for (int c = 0; c < 3; ++c) {
            out[c] += weight * (static_cast<float>(lut[index + static_cast<std::size_t>(c)]) / 255.f);
        }
    }
}

/*!
 * @brief 無音の wav を 1 本書く（検査用。`--ui-check` の音の節が使う）。
 * @param frames 16bit 単耳 44100Hz の標本の数
 * @return 書けたら true
 * @details **素材を置かずに「wav から鳴らす道」を通す**ためだけにある。
 * 中身が無音なので、検査が机の前で音を出すことはない。
 */
bool write_silent_wav(const std::string &path, std::size_t frames)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    const std::uint32_t rate = 44100;
    const std::uint16_t channels = 1;
    const std::uint16_t bits = 16;
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(frames * 2);
    const auto put32 = [&file](std::uint32_t v) {
        const char bytes[4] = { static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff),
            static_cast<char>((v >> 16) & 0xff), static_cast<char>((v >> 24) & 0xff) };
        file.write(bytes, 4);
    };
    const auto put16 = [&file](std::uint16_t v) {
        const char bytes[2] = { static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff) };
        file.write(bytes, 2);
    };
    file.write("RIFF", 4);
    put32(36 + data_bytes);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    put32(16);
    put16(1); //!< PCM
    put16(channels);
    put32(rate);
    put32(rate * channels * (bits / 8));
    put16(static_cast<std::uint16_t>(channels * (bits / 8)));
    put16(bits);
    file.write("data", 4);
    put32(data_bytes);
    const std::vector<char> zeros(data_bytes, 0);
    file.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    return static_cast<bool>(file);
}

} // namespace hd2d
