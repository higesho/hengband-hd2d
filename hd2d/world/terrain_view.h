/*!
 * @file terrain_view.h
 * @brief 意味づけの結果を「置き場所」へ翻訳する（P3 ③④⑤）。
 *
 * 基準はボクセル HD2D の設計 §9.2・§9.3、段取りは P3。
 *
 * ## 3 層（§9.2）
 * | 層 | 中身 | マスとの関係 |
 * |---|---|---|
 * | 地面 | 各マスに 1 枚の板 | マスに従う |
 * | 構造 | 壁・岩盤・扉・階段 | **接地シルエットだけ**がマスに従う |
 * | **装飾** | 瓦礫・柱 | **当たり判定に関係しない。**密度で撒く |
 *
 * 「リッチだ」という実感を一番作るのは装飾層である（§9.2）。当たり判定を持たないので
 * 設計上の危険が無く、量が効く。
 *
 * ## 種（§9.3）— **絶対格子座標から引く**
 * ```
 * seed = hash(絶対gx, 絶対gy, フロア識別子)
 * ```
 * カメラが動いた瞬間に岩の形が変わって画面がざわつくのを防ぐ。**乱数器を 1 本回して
 * 順に配ってはならない**（配る順序が視界に依存すると、同じマスが毎フレーム違う姿になる）。
 *
 * ## いま置いているのは**仮の形**である
 * 箱と板を拡大率で変えているだけで、プレハブ（`.vox`）はまだ使っていない。
 * 部屋と通路の作りの違いが**絵として出る**ことがこの段の目的で、素材は P10。
 */
#pragma once

#include "render/frustum.h"
#include "render/light_shaft.h"
#include "render/voxel_renderer.h"
#include "voxel/prefab.h"
#include "world/floor_meaning.h"
#include "world/prefab_library.h"
#include "world/terrain_memory.h"
#include "world/town_plan.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct GameFrame;

namespace hd2d {

//! 1 フレーム分の置き場所。プレハブごとに分けて持つ（描画は種類ごとに 1 回）。
/*!
 * @brief 置き場所と一緒に記録する**点光源のリクエスト**（2026-08-18。祠のライトアップ）。
 *
 * @details terrain_view は描画の置き場所しか作らないが、龍神像の「夜は下から
 * ライトアップ」には本物の点光源が要る（自発光は自分しか光らせない＝周りの石を
 * 照らせない）。ここにリクエストを積んでおき、光を組み立てる側（`hd2d_app`）が
 * **夜の度合いを掛けて**点光源の列へ足す。`render/lighting.h` に依存しないよう、
 * 素の数値だけを持つ。
 */
struct TerrainViewLight {
    float x{ 0.f };
    float y{ 0.f };
    float z{ 0.f };
    float radius{ 0.f }; //!< 届く距離（マス）
    float r{ 1.f };
    float g{ 1.f };
    float b{ 1.f };
    float intensity{ 0.f }; //!< 昼夜を掛ける前の強さ
};

struct TerrainView {
    //! 床の板（`slab` プレハブ）。
    std::vector<InstanceData> slabs;
    //! 夜だけ灯す点光源のリクエスト（龍神像のライトアップなど。置く側が夜の度合いを掛ける）。
    std::vector<TerrainViewLight> lights;
    /*!
     * @brief **壁の下に敷く地面**（同じ `slab` プレハブ。P7 で足した）。
     *
     * @details 設計書 §9.2 の地面層は「各マスに 1 枚の板」なのに、壁のマスだけ抜けていた。
     * 壁が立っている限り完全に隠れるので P6 まで誰も困らなかったが、**カットアウェイが
     * 壁を抜いた瞬間にそこだけ背景が見えた**（`--cutaway-check` の (3)）。
     *
     * `slabs` と分けてあるのは**影のパスへ渡さないため**である。壁の真下は光が
     * 当たらないので影は 1 テクセルも変わらず、渡せば深度の書き込みが増えるだけになる。
     */
    std::vector<InstanceData> under_slabs;
    //! 箱（`box` プレハブ）。壁・岩盤・装飾・柱を**全部ここへ入れる**（拡大率で作り分ける）。
    std::vector<InstanceData> boxes;
    /*!
     * @brief 本物のプレハブの置き場所（P10）。添字は `PrefabLibrary` のプレハブ索引。
     * @details ライブラリを渡したときだけ使う。渡さなければ空のままで、
     * 従来どおり `slabs`/`boxes`（仮の箱と板）に出る。**描く側は
     * `LibraryEntry::is_ground` で 2 群に分ける**（地面はカットアウェイを掛けない）。
     */
    std::vector<std::vector<InstanceData>> lib;

    int considered_cells{ 0 }; //!< 意味づけ済みのマスのうち走査した数
    int culled_cells{ 0 }; //!< 視錐台の外で捨てた数
    int drawn_cells{ 0 };
    int prop_count{ 0 }; //!< 装飾（当たり判定に関係しないもの）の数
    int lib_instances{ 0 }; //!< プレハブの置き場所の総数（画面の下の行に出す）

    /*!
     * @name 視線を遮る高さ（P10 レビュー 4。カットアウェイの判定に使う）
     *
     * @details 2026-08-09 に決めた:「壁の横にいる際に透過は不要。**視線がブロックや
     * オブジェクトで通らないときのみ**透過を発生させて」。
     *
     * それを決めるには「カメラとプレイヤの間に、視線より高いものがあるか」が要る。
     * 描くために作った置き場所からは引けない（プレハブの天面の高さはライブラリが持っている）ので、
     * **置くときに一緒に記録する**。添字は意味づけと同じ `y * width + x`、単位はマス。
     * 視錐台の外は 0 のまま（カメラとプレイヤの間は必ず視錐台の中なので困らない）。
     * @{
     */
    int occluder_w{ 0 };
    int occluder_h{ 0 };
    std::vector<float> occluder_z;

    //! そのマスの遮蔽の高さ（マス）。範囲外は 0。
    float occluder_at(int gx, int gy) const
    {
        if ((gx < 0) || (gy < 0) || (gx >= this->occluder_w) || (gy >= this->occluder_h)) {
            return 0.f;
        }
        return this->occluder_z[(static_cast<std::size_t>(gy) * static_cast<std::size_t>(this->occluder_w))
            + static_cast<std::size_t>(gx)];
    }
    /*! @} */

    /*!
     * @name 覆いの町（岩天井。2026-08-18 その3。旧地獄街道）
     * @details 意匠が `cave_roof` を立てている町では、全マスの頭上に岩天井が敷かれる。
     * 描く側はこれを見て**屋根外しの層**（`make_roof_cutaway`）を常時入れ、
     * 影の直方体の高さを天井まで持ち上げる。天井は遮蔽の記録（`occluder_z`）には
     * **入れない**——入れると全マスが「遮られている」になり、実体の穴が枠（12）を
     * 食い尽くす。
     * @{
     */
    bool covered{ false };
    //! 屋根外しの層が刳り抜く下限（マス）。覆いの町でだけ意味を持つ。
    float cave_min_z{ 3.1f };
    //! 天井の天面の高さ（マス）。影の直方体をここまで持ち上げる。
    float cave_top{ 0.f };
    /*! @} */

    /*!
     * @name 湖の霧（デザイン4・2026-08-18。紅魔館の霧の湖）
     * @details 意匠が `lake_mist` の高さを持つ町では、深水のマスをここへ積む。
     * 描くのは `CloudLayer::draw_patch`（半透明なので不透明な世界の後・`end_scene()` の
     * 前に描く）——terrain_view は**マスと高さのリクエスト**だけを持つ（`TerrainViewLight` と
     * 同じ流儀で、描き手には依存しない）。
     * @{
     */
    std::vector<std::array<int, 2>> mist_cells;
    float mist_height{ 0.f }; //!< 0 以下なら霧は無い
    /*!
     * @name 地表の霧（デザイン8 その2・2026-08-19。永遠亭の迷いの竹林）
     * @details 意匠が `grove_mist` の高さを持つ町では、**空の雲の網をそのまま低い所へ**
     * 敷く（`CloudLayer::draw_ground`）。マスの集合は持たない——マスごとの箱は一人称で
     * 白い板の壁になる（実機で見た様子）。ここが持つのは高さと濃さのリクエストだけ。
     * @{
     */
    float ground_mist_height{ 0.f }; //!< 0 以下なら敷かない
    float ground_mist_alpha{ 0.34f };
    /*! @} */
    /*! @} */

    /*!
     * @brief **配管の蒸気の噴き出し口**（デザイン7 その2・2026-08-19。河童のバザー）。
     * @details 継ぎ手（`pipe_cross`）と立ち上がり（`pipe_riser_*`）の口の**世界座標**
     * {x, y, z}。描くのは `CloudLayer::draw_steam`——半透明はプレハブの経路に無いので、
     * terrain_view は**噴き出し口のリクエスト**だけを持つ（`mist_cells` と同じ流儀）。
     */
    std::vector<std::array<float, 3>> steam_jets;

    /*!
     * @brief **上空から差し込む光の柱**（2026-08-22 に決めた。陽だまり）。
     * @details 対応表が `light_shaft` を持つマスだけ積む（`prefab_library.h`）。
     * 描くのは `LightShaftRenderer`——半透明はプレハブの経路に無いので、
     * terrain_view は**柱のリクエスト**だけを持つ（`mist_cells` / `steam_jets` と同じ流儀）。
     */
    std::vector<LightShaftInstance> light_shafts;
};

/*!
 * @brief カメラからプレイヤへの視線が、途中のマスの何かに遮られているか（P10 レビュー 4）。
 * @param eye カメラの位置（マス単位・z も）。
 * @param player_gx,player_gy プレイヤのマス。
 * @details 2 マスの間を DDA で辿り、**そのマスでの視線の高さ**より `occluder_at()` が
 * 高ければ遮られていると判定する。プレイヤ自身のマスとカメラのマスは見ない。
 * 遮蔽が無ければカットアウェイは要らない（＝壁の横に立っているだけ）。
 */
bool line_of_sight_blocked(const TerrainView &view, const Vec3 &eye, int player_gx, int player_gy);

/*!
 * @brief 種。**絶対格子座標とフロア識別子だけ**から決まる（§9.3）。
 * @details カメラにも視界にも走査順にも依存しない。決定性の検査（`--world-check`）の対象。
 */
std::uint64_t cell_seed(int gx, int gy, const FloorIdentity &floor);

/*!
 * @brief 一度置いた小物 1 つ（`PropLatch` の中身）。
 */
struct LatchedProp {
    int prefab{ -1 }; //!< ライブラリの索引
    InstanceData instance{}; //!< そのとき決めた位置・大きさ・色・自発光
    float cover_top{ 0.f }; //!< >0 なら視線を遮る（`note_cover` に渡した高さ）
};

/*!
 * @brief **小物のロック**（2026-08-11 に決めた:「ダンジョンの小物は一度生成されたら
 *   書き換わらないように」）。
 *
 * ## なぜ要るのか
 * 装飾層（§9.2）は**並びの意味づけ**（`FloorMeaning::marks`。行き止まり・壁際・部屋の縁）
 * から置く物を決める。ところが意味づけは**ミニマップの既知マスから作り直される**ので、
 * 探索が進むと形が変わる:
 *
 * - 行き止まりだと思っていたマスの先が見えると `MARK_DEAD_END` が消える → 瓦礫が消える
 * - 壁だと思っていた隣が通路だと分かると `MARK_WALL_SIDE` が付く → 樽が生える
 * - 種の消費数が枝で変わるので、**同じマスでも引く乱数がずれて別の物になる**
 *
 * 種（`cell_seed`）はマスとフロアだけで決まるので「同じマスなら同じ物」は保証されているが、
 * それは**同じ枝を通ったとき**の話である。枝そのものが動くとこの保証は効かない。
 *
 * そこで、**一度置いたマスは置いたものをそのまま覚えて replay する**。
 *
 * ## 覚えるのは「置いたマス」だけ
 * 何も置かなかったマスは覚えない。覚えてしまうと、まだ細別（`terrain_id`）が届いていない
 * 段で「何も無い」と決まってしまい、近づいても木も溶岩の泡も出なくなる。
 *
 * ## 地下だけ
 * 町と地上では掛けない。街灯の火は**夜の度合い**で明るさが変わるので、
 * 置いたときの自発光を覚えると日が暮れても灯らなくなる。
 * 地下の装飾に時刻で変わるものは無い（松明の自発光は定数）。
 */
struct PropLatch {
    FloorIdentity identity{};
    bool valid{ false };
    std::unordered_map<int, std::vector<LatchedProp>> cells; //!< マスの添字 → 置いたもの

    //! 別の階になったら捨てる（前の階の小物は意味を持たない）。
    void follow(const FloorIdentity &floor);
};

/*!
 * @brief 意味づけ済みのフロアから、いま描くべき置き場所を作る。
 * @param frustum 視錐台。**外のマスは捨てる**（P3 ⑤）。
 * @param memory 地形の細別の記憶（P10）。nullptr なら役割の既定だけで置く。
 * @param library プレハブライブラリ（P10）。nullptr か未読なら仮の箱と板（P3 の見え）へ落とす。
 * @param town 町の敷地の分解（P10 第 2 期・§10）。nullptr か地上でなければ従来の経路。
 * @param night **夜の度合い**（0 = 昼 / 1 = 真夜中）。窓と街灯の火の強さに掛ける。
 * フレームごとに変わる値なので、置き場所を作るときに渡す（ライブラリにも計画にも持たせない）。
 * @param wall_inset **一人称のときだけ**壁と岩盤を痩せさせる量（マス・片側。既定 0＝従来）。
 *   痩せるのは**隣がブロックでない辺だけ**で、一直線に続く壁は面一のまま繋がる。
 *   角で接する 2 つの間に隙間が開き、斜めに抜けられることが見て分かる（`ui/fps_mode.h`）。
 * @param latch **小物のロック**（`PropLatch`。nullptr なら掛けない＝検査は従来のまま）。
 *   渡すと、**地下で一度置いた装飾はそのまま覚えて置き直す**（2026-08-11 に決めた）。
 * @param black_unknown **未探知のマスを真っ黒な塊で埋める**（既定 false＝従来どおり何も置かない）。
 *   2026-08-11 に決めた「FPS モードの時のみダンジョン内は視界が通っている
 *   未踏破マスは真っ黒ブロックにしよう」。**一人称かつ地下のときだけ**真にすること。
 *
 *   見下ろしでは要らない。俯瞰の絵で未探知が黒く塗り潰されると、地図の形が読めなくなる
 *   （「まだ行っていない所」と「壁」が同じ色になる）。一人称は逆で、**何も置かないと
 *   そこだけ背景が抜けて世界の外が見えてしまう。**目の高さからは、未探知は
 *   「向こうが見えない闇」であるべきで、穴であってはならない。
 * @param latch **小物のロック**（`PropLatch`。nullptr なら掛けない＝検査は従来のまま）。
 * @param fps_wall_upper **壁の 2 段目**（2026-08-11 に決めた「FPS モードの際に、
 *   ダンジョンの壁の高さを 2 ブロックにして」）。壁・岩盤・扉のマスの z=1〜2 に
 *   **そのダンジョンの基本ブロック**（意匠の壁 → 役割の既定の壁の順で引く）を積む。
 *   扉のマスにも積むのは、扉の上（z=1〜2）が抜けていると**扉越しに隣の部屋が
 *   覗けてしまう**ため（まぐさ石として壁の並びが繋がる）。
 *   **一人称かつ地下のときだけ**真にすること。見下ろしは従来どおり 1 段
 *   （2026-08-11 に決めた「見下ろしモードの時は今まで通り 1 ブロックの高さを維持」）。
 * @param fps_ceiling **天井**（同指示「天井も作ってみよう」）。開けたマス（床・通路・階段）の
 *   上に基本ブロックを 1 層敷く。高さは**壁の天面に載る**——2 段目が入なら z=2〜3、
 *   切なら z=1〜2。扉のマスは 2 段目の側が z=1〜2 を塞ぐので天井は敷かない。
 *
 *   どちらの層も**種は本流と別系統**で引く。本流の乱数列から引くと、切り替えるたびに
 *   小物や色むらが変わってしまう（`PropLatch` が守っている「絵が動かない」が壊れる）。
 * @param blocks_only **マス 1 つにつき立方体 1 つだけ**にする（TRON 画調。
 *）。2026-08-19 に決めた:
 *   「ブロックは既存ブロックのままではなく、全てシンプルな立方体。
 *   ダンジョンも地上も全て立方体ブロック」「小物オブジェクトは不要」。
 *
 *   真にすると 3 つが同時に効く:
 *   | | |
 *   |---|---|
 *   | ライブラリ | **引かない**（`library` を渡しても無視する）。壁も地面も素の箱と板になる |
 *   | 装飾層 | **1 つも置かない**（瓦礫・柱・門柱・草・街路樹・街灯——全部） |
 *   | 岩盤の高さ | 種で散らさず **1.0 ちょうど**（散らすと立方体に見えない） |
 *
 *   結果として**アスキー地図が言っているものだけ**がマスごとの立方体で建つ。
 *   町の建物も「壁のマスの塊」として出る（ライブラリの建物プレハブは使わない）。
 * @details 走査するのはフロア全域（198×66 で約 13,000 マス）。可視窓ではなくフロア全域を
 * 見るのは、**背の高いものが窓の外から画面へ写り込む**ため（§9 ①の理由そのもの）。
 */
void build_terrain_view(const FloorMeaning &meaning, const Frustum &frustum, TerrainView &out,
    const TerrainMemory *memory = nullptr, const PrefabLibrary *library = nullptr,
    const TownPlan *town = nullptr, float night = 0.f, float wall_inset = 0.f, bool black_unknown = false,
    PropLatch *latch = nullptr, bool fps_wall_upper = false, bool fps_ceiling = false,
    bool blocks_only = false);

/*!
 * @brief 中身の詰まった直方体のプレハブをその場で作る（ファイルを読まない）。
 * @param sx,sy,sz ボクセル数。
 * @param origin_z プレハブ原点からの高さ（ボクセル）。床の板を地面より下げるのに使う。
 * @details 色は白 1 色で、実際の色は**インスタンスごとの色**が掛かる。
 */
Prefab make_box_prefab(const std::string &name, int sx, int sy, int sz, int origin_z, int voxels_per_cell);

} // namespace hd2d
