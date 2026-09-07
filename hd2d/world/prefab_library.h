/*!
 * @file prefab_library.h
 * @brief プレハブライブラリと「terrain_id → プレハブ集合」の対応表（P10・設計書 §9.4）。
 *
 * ## 規則の進化（§9.4）
 * 「ui は地形テーブルを知らない」を**コードについてだけ**守り続けるための装置である。
 * 地形の細別はコードではなく `assets/voxel/terrain_prefabs.jsonc` が持つ。
 * 素材を足すときはプレハブを置いて表に 1 行足すだけで、**コードのビルドは要らない**。
 *
 * ## 読み込みは GL 不要
 * `load()` は CPU だけで完結する（`--world-check` が窓なしで対応表まで検査できる）。
 * GPU へ載せるのは窓のある経路が `upload_gpu()` を呼んだときだけ。
 *
 * ## 表と lib/edit の突き合わせ（§9.4「機械的に検査できる」）
 * `cross_check()` が `lib/edit/TerrainDefinitions.jsonc` を読み、
 * - 表の `key` が実在すること・`id` が一致すること（**食い違いは FAIL**。書き間違い）
 * - 表に無い id の一覧（**取りこぼしは情報**。役割の既定で描かれるだけで壊れない）
 * を返す。`--world-check` から呼ばれる。
 *
 * **`terrains_extra` の節はこの突き合わせに載せない。**あそこは変愚以外のコアだけが
 * 送る地形（Sil-Q の陽だまりなど）で、変愚の `TerrainDefinitions.jsonc` に key が無い。
 * 規則の効き方は `terrains` と同じ（引くのは同じ `rule_by_id_`）。
 *
 * ## 検査の検査（`HD2D_BREAK_TERRAIN_MAP`）
 * | 値 | 何を壊すか | どの検査が落ちるべきか |
 * |---|---|---|
 * | `prefab` | 表に無い名前のプレハブを 1 つ要求する | `load()` |
 * | `id` | 表の最初の行の id を 1 ずらす | `cross_check()` |
 */
#pragma once

#include "render/voxel_renderer.h"
#include "voxel/prefab.h"
#include "world/floor_meaning.h"

#include <cstdint>
#include <memory> //!< 載せる仕事（`UploadJob`）を不完全型のまま持つ
#include <string>
#include <unordered_map>
#include <vector>

namespace hd2d {

//! 対応表の 1 行（役割の既定にも同じ形を使う）。中身はプレハブの**索引**。
struct TerrainRule {
    std::vector<int> ground; //!< 地面（種で 1 つ選ぶ。カットアウェイは掛けない）
    std::vector<int> structure; //!< マスを塞ぐもの（0.08 マス沈める。罠 4）
    std::vector<int> object; //!< 地面の上に立つもの
    /*!
     * @brief **東西に通る所へ置く姿勢**（P10 レビュー 11）。空なら向きを見ない。
     *
     * @details 扉は「通る向き」を持つ数少ない素材である。基の `object` は**南北に通る**形
     * （板が東西に張る）なので、東西の通路にそのまま置くと**壁と平行に扉が立つ**
     * （2026-08-09 に決めた:「扉の向きは東西に通過の場合は 90 度回転して設置して」）。
     *
     * 設計書 §7.1-2 は「向きはデータを 1 方向で持ち、姿勢のほうを用意する」と言っている。
     * ここはその素朴な形で、**意匠は 1 つ・姿勢だけ 2 つ**を素材の対として持つ
     * （`door_closed` と `door_closed_ew`。後者は `gen_prefabs.py` が前者を 90° 回して作る）。
     * どちらを使うかは `terrain_view.cpp` が隣のマスの壁の並びから決める。
     */
    std::vector<int> object_ew;
    /*!
     * @brief **店の看板**（1 マス幅の吊り看板。P10 第 2 期・2026-08-09 に決めた）。
     * @details 「建物の内容がわかる絵」を持たせるので、店の種別ごとに違う板になる。
     * 引くのは町の門のマスで、置き場所を決めるのは `town_plan.cpp` の敷地の分解。
     * ここに無い（＝まだ間近で見ていない）門は無地の板になる。
     * @note **姿勢の対は無い。**板は常に南を向く（同日の指示「建物の看板は常に南向に」）
     * ので、向きは置く側が**マスをずらして**吸収する（`terrain_view.cpp` の門の分岐）。
     */
    std::vector<int> sign;
    float density{ 1.f }; //!< object を置く確率
    float jitter{ 0.f }; //!< object の位置の散らし（マス）
    float emissive[3]{ 0.f, 0.f, 0.f }; //!< 自発光（線形 HDR）
    float tint[3]{ 1.f, 1.f, 1.f };
    /*!
     * @name **上空から差し込む光の柱**（`render/light_shaft.h`。2026-08-22 に決めた）
     *
     * @details `"light_shaft": { "color": [r,g,b], "radius": r, "height": h, "alpha": a }`。
     * **`alpha` が 0 なら立てない**（既定）ので、書いていない規則の絵は 1 画素も変わらない。
     *
     * `emissive` とは**役割が違う**。あちらは「光を浴びている床」、こちらは
     * 「空間に浮かぶ筋」で、**両方が要る**——片方だけだと「光っている床の上に何も無い」か
     * 「宙に浮いた筋の下が暗い」になる。
     * @{
     */
    float shaft_color[3]{ 1.f, 0.96f, 0.80f };
    float shaft_radius{ 0.5f }; //!< 幅の半分（マス）。**マスの形に合わせる**なら 0.5
    float shaft_height{ 3.4f }; //!< 立ち上がる高さ（マス）
    //! **満ちている高さ**（マス）。ここまでは一様で、上は中央から抜ける（`light_shaft.h`）。
    float shaft_full{ 2.f };
    //! 上端の水平のずれ（マス）。**斜めに落とす**ぶん（天使の階段）。
    float shaft_slant[2]{ 0.95f, 0.30f };
    //! 上端の幅（床での幅に対する比）。**1 未満で上ほど狭くなる**（`light_shaft.h`）。
    float shaft_taper{ 0.82f };
    float shaft_alpha{ 0.f }; //!< **0 なら柱を立てない**
    /*! @} */

    bool any() const { return !this->ground.empty() || !this->structure.empty() || !this->object.empty(); }
};

//! プレハブライブラリの 1 件。プレハブ本体と、置くとき・描くときに要る導出値。
struct LibraryEntry {
    std::string name;
    Prefab prefab;
    GpuPrefab gpu;
    bool gpu_ready{ false };
    float top_z{ 0.f }; //!< 天面（マス単位）。構造の沈み込み補正 `sz=(top+0.08)/top` に使う
    float anchor_x{ 0.f }; //!< footprint の最初のマスを目標マスへ合わせる引き算
    float anchor_y{ 0.f };
    /*!
     * @name **水平に覆う範囲**（プレハブ原点からのマス。P10 レビュー 6）
     *
     * @details `footprint`（当たり判定）とは別物である。footprint は「接地するマス」で、
     * 木なら幹の 1 マスしかない。だが**葉は 3〜4 マスに張り出していて、視線はそこで遮られる**
     * （2026-08-09 に気づいた:「透過の判定をブロックの位置だけでなく、
     * オブジェクトが視界を遮るかどうかにしてくれ」）。
     *
     * ここはボクセルの実体そのものの広がりで、**カットアウェイの遮蔽判定にだけ**使う。
     * 当たり判定には一切関わらない（§8.2-2 の「当たり判定は動かさない」と同じ線引き）。
     * @{
     */
    int cover_x0{ 0 };
    int cover_x1{ 0 };
    int cover_y0{ 0 };
    int cover_y1{ 0 };
    /*! @} */
    bool has_motion{ false }; //!< 剛体の動きを持つか（行列が要るか）
    bool is_ground{ false }; //!< 天面が z<=0.05（カットアウェイも沈み込みも掛けない）
};

class PrefabLibrary {
public:
    PrefabLibrary();
    /*!
     * @note 看取りは `.cpp` にある。**載せる仕事（`UploadJob`）が不完全型**なので、
     * 既定の看取りをここに置くと `unique_ptr` が中身を知らずに壊そうとして通らない。
     */
    ~PrefabLibrary();
    PrefabLibrary(const PrefabLibrary &) = delete;
    PrefabLibrary &operator=(const PrefabLibrary &) = delete;

    /*!
     * @brief `<voxel_dir>/terrain_prefabs.jsonc` と、そこから参照される全プレハブを読む。
     * @return 成功したか。**失敗しても致命ではない**（呼び手は仮の箱の見た目へ落とす）。
     */
    bool load(const std::string &voxel_dir, std::string &err);

    bool ready() const { return this->loaded_; }
    int count() const { return static_cast<int>(this->entries_.size()); }
    const LibraryEntry &entry(int i) const { return this->entries_[static_cast<std::size_t>(i)]; }
    //! 描画は `GpuPrefab` を非 const で要求する（インスタンス VBO を詰め替えるため）。
    LibraryEntry &entry_mut(int i) { return this->entries_[static_cast<std::size_t>(i)]; }
    //! 名前から索引。無ければ -1。
    int find(const std::string &name) const;

    /*!
     * @brief 地形の細別から。表に無ければ nullptr（役割の既定へ落とす）。
     * @param surface **地上（町・荒野）か。**真なら `terrains_surface` の同じ id を先に見る。
     * @details 同じ地形でも空の下と地下で置くものを変えたいことがある。いまの用途は木で、
     * 2026-08-09 に決めた「ダンジョン内の木は高さ 2 ブロックまでに制限して。
     * 地上は今のままでいい」を、**表の 1 行**で解いている（コードは高さを知らないまま）。
     */
    const TerrainRule *rule_for_terrain(std::uint16_t terrain_id, bool surface = false) const;
    /*!
     * @brief 役割の既定。表の `roles` に無い役割は nullptr。
     * @param surface **地上（町・荒野）か。**真なら `roles_surface` を先に見る。
     * @details 町の街路をダンジョンの通路と同じ土にすると、街が野原に見える
     * （2026-08-09 に決めた「街の地面は部屋と同じくタイル調に」）。
     * 地形の細別（草・土・水）は表が持っているので、ここで変わるのは**細別が届いて
     * いないマス**＝街路だけである。
     */
    const TerrainRule *rule_for_role(CellRole role, bool surface = false) const;

    /*!
     * @brief 窓のある経路だけが呼ぶ。全プレハブをメッシュ化して GPU へ。**戻るまで塞ぐ。**
     * @details 中身は `begin_upload()` ＋ 予算無制限の `pump_upload()` である。
     */
    bool upload_gpu(VoxelRenderer &renderer, std::string &err);
    /*!
     * @brief 載せる仕事を起こす（メッシュ化のスレッドが走り出す）。**GL は触らない。**
     * @details すでに走っていれば何もしない。`load()` の後に呼ぶこと。
     */
    void begin_upload();
    /*!
     * @brief 予算のぶんだけ GPU へ載せて戻る。**GL を触るので描画スレッドから。**
     * @param budget_ms この呼び出しで使ってよい時間（ミリ秒）。**0 なら終わるまで塞ぐ。**
     * @return 失敗していないか。まだ残っているかは `upload_busy()` で見る。
     * @details コア選択の演出（1.5 秒）の各フレームから 1 回ずつ呼ぶと、
     * メッシュ化と GPU 転送が演出の裏で終わる。
     * 仕事が無ければ即座に真で戻る。
     */
    bool pump_upload(VoxelRenderer &renderer, int budget_ms, std::string &err);
    //! まだ載せ終えていないものがあるか。
    bool upload_busy() const;
    //! メッシュ化と GPU 転送に使った時間の合計（ミリ秒。汲んだぶんを足したもの）。
    unsigned upload_ms() const { return this->upload_ms_; }
    void release_gpu(VoxelRenderer &renderer);

    /*!
     * @brief このフレームの剛体の動き（P6）を作る。**1 フレーム 1 回。**
     * @details 影のパスと本描画は**同じ結果**を読む（§8.2-1。2 回呼ぶと位相がずれる）。
     */
    void update_motions(float time);
    //! そのプレハブの動きの行列。持たなければ nullptr（静止）。
    const Mat4 *motions_of(int i) const;
    std::size_t motion_count_of(int i) const;

    /*!
     * @brief 表を `lib/edit/TerrainDefinitions.jsonc` と突き合わせる（§9.4）。
     * @param[out] report 取りこぼし（表に無い id）の一覧。**情報であって失敗ではない。**
     * @param[out] err key の不在・id の食い違い。**これは失敗**（表の書き間違い）。
     * @return 突き合わせに通ったか。
     */
    bool cross_check(const std::string &terrain_defs_path, std::string &report, std::string &err) const;

    /*!
     * @brief 表に載っている `key`（`GENERAL_STORE` など）から `terrain_id` を引く。無ければ 0。
     * @details `--town-check` が**町のデータの記号**（`F:1:GENERAL_STORE`）を
     * `terrain_id` へ翻訳するのに使う。表は人が読める key を持っているので、
     * 検査だけのために記号 → id の対応をもう 1 つ書かずに済む。
     */
    int terrain_id_for_key(const std::string &key) const;

private:
    bool parse_rule(const void *json_node, TerrainRule &out, std::string &err);
    //! 走っている「載せる仕事」の中身（スレッド・ロック・待ち・作りかけのメッシュ）。定義は `.cpp`。
    struct UploadJob;
    void finish_upload_job(); //!< スレッドを畳んで仕事を捨てる（成功でも失敗でも通る道）

    std::unique_ptr<UploadJob> upload_job_;
    unsigned upload_ms_{ 0 };
    std::vector<LibraryEntry> entries_;
    std::unordered_map<std::string, int> by_name_;
    std::vector<TerrainRule> terrain_rules_;
    std::unordered_map<int, int> rule_by_id_; //!< terrain_id → `terrain_rules_` の添字
    std::unordered_map<int, int> rule_by_id_surface_; //!< `terrains_surface` の上書き
    std::vector<std::pair<std::string, int>> mapped_keys_; //!< 表の (key, id)。突き合わせ用
    TerrainRule role_rules_[8];
    bool role_present_[8]{};
    TerrainRule role_surface_rules_[8]; //!< `roles_surface`（地上の上書き）
    bool role_surface_present_[8]{};
    std::vector<std::vector<Mat4>> motions_;
    bool loaded_{ false };
};

} // namespace hd2d
