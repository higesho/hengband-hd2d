/*!
 * @file prefab.h
 * @brief プレハブ（`.vox` ＋ `.jsonc` の対）の読み込みと**検査**。
 *
 *
 * ## 何をどちらが持つか
 * | | 持ち物 |
 * |---|---|
 * | `.vox` | 実体（ボクセルとパレット）と**配置**（シーングラフの平行移動） |
 * | `.jsonc` | 意味づけ（footprint・親子・ピボット・動き・接地・風の係数・種） |
 *
 * **配置を 2 か所に書かない。**`.jsonc` の `offset` は省略可で、書いた場合は `.vox` と
 * 一致することを照合する（片方だけ直したときに落ちる）。
 *
 * ## 検査（§7.2）— 「検出したいものを検出できない検査は、無いより悪い」
 * 1. 宣言 footprint と、**地面の高さ（z = 0）**のボクセルの一致
 * 2. 各パーツのピボットがそのパーツの範囲内にあること
 * 3. 親子関係に循環が無いこと
 * 4. `parts[].voxels` が `.vox` に実在すること
 *
 * **`tools/voxel/break_footprint.py` でわざと壊した `.jsonc` を作り、1 が落ちることを
 * 確かめてある**。検査を書いただけで満足しない。
 */
#pragma once

#include "voxel/vox_file.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hd2d {

//! 動きの種類（§7 の `motion`）。P1 では**読むだけ**で、動かすのは P6。
enum class MotionKind {
    Static,
    Rotate, //!< 水車・風車
    Pendulum, //!< 看板・幟・吊りランプ
    Wind, //!< 草・葉・布（頂点シェーダ）
    StateLinked, //!< 扉の開閉
    /*!
     * @brief **明滅**（2026-09-05。こう決めた——「噴水の水はボクセルをうまく順番に明滅させ
     * 水滴が動くようにアニメーションさせて」）。
     * @details 周期 `period` の中の `phase`〜`phase + duty` のあいだだけ見える（それ以外は
     * 縮尺 0 で消す）。位相をずらした部品を並べると、点いた所が順に移って**粒が動いて見える**。
     * 剛体の動きしか無い枠の中で「順番に点く」を作るための種類で、形は動かさない。
     */
    Blink,
};

struct Motion {
    MotionKind kind{ MotionKind::Static };
    int axis{ 0 }; //!< 0=x 1=y 2=z（Rotate / Pendulum）
    float speed{ 0.f };
    float phase{ 0.f };
    float amplitude{ 0.f };
    float period{ 0.f };
    float damping{ 0.f };
    float duty{ 0.5f }; //!< Blink: 周期のうち見えている割合（0〜1）
    std::string state_name; //!< StateLinked
};

struct PrefabPart {
    std::string name;
    std::string voxels; //!< `.vox` の節点名
    int model_index{ 0 };
    float offset[3]{}; //!< プレハブ原点からの最小隅（`.vox` 由来。`.jsonc` にあれば照合する）
    int parent{ -1 }; //!< `parts` の添字。-1 = 根
    bool has_pivot{ false };
    float pivot[3]{}; //!< **プレハブ原点から**（§7 の「親からの相対」）
    Motion motion;
    bool grounded{ true };
    float wind_k{ 1.f };
};

/*!
 * @brief エンジンが受け入れる `voxels_per_cell` の段（2026-08-21 に決めた）。
 *
 * @details 「人物・アイテム・モンスターは 8/16/32/64/128/256/512 を受け入れられるように。
 * **コアは問わずエンジンとして**」。実際に使っているのは地形と小物が 32・実体の板が
 * 64（変愚／幻想）と 128（Sil-Q）だが、**受け口はこの 7 段すべてを通す**。
 *
 * 段を 2 の冪に限るのは、`downsample_model()` の LOD が 1/2 ずつ落とすからである
 * （半端な値だと段ごとに端数が出て、遠景で 1 ボクセルぶん形がずれる）。
 * 描く側は `1/voxels_per_cell` の縮尺を model 行列に入れるだけなので、
 * **粗さの違う板が同じ場面に並んでも大きさは狂わない**（`voxel_renderer.cpp`）。
 */
constexpr int kVoxelsPerCellLadder[] = { 8, 16, 32, 64, 128, 256, 512 };

//! `voxels_per_cell` が受け入れる段か。
bool is_supported_voxels_per_cell(int value);

//! 受け入れる段を "8/16/32/64/128/256/512" の形で返す（エラー文用）。
std::string voxels_per_cell_ladder_text();

struct Prefab {
    std::string name;
    int voxels_per_cell{ 32 };
    //! 接地するマス。**当たり判定はこれで決まる**（§7.1-1）。
    std::vector<std::pair<int, int>> footprint;
    int anchor[2]{};
    std::vector<PrefabPart> parts;
    /*!
     * @brief **空から見えない面を作らない**（`.jsonc` の `hide_unseen_from_above`）。
     * @details 深淵（`ground_chasm_sq_*`）のためにある。**穴は地面の側からだけ描く**
     * ——殻の外側や底へ向く面を作らない（2026-08-25 に決めた
     * 「深淵等の地下部分は外側は描画しなくていいね。穴が地面側からだけ描画されるように」）。
     * 規則そのものは `mesh_model()` の説明にある。**既定は偽**で、旗を立てない
     * プレハブは 1 バイトも変わらない。
     */
    bool hide_unseen_from_above{ false };
    VoxFile vox;
    /*!
     * @name **このプレハブだけのパレットの材質**（`.jsonc` の `palette`）
     *
     * @details 面の汚しと木の葉が「木か鉄か布か」で掛け方を変えるために要る
     * （`hd2d/render/surface_wear.h`）。
     *
     * **色から当てるのはやめた**（2026-08-23 に決めた:「パレットの色で汚しを
     * 入れるのをやめよう。パレットの共有もやめて、ベースのパレットのコピーを作り
     * 個別に使うように」）。パレットはライブラリじゅうで色を使い回しているので、色は材を指さない
     * ——同じ灰色が切石にも鉛板にもなる。書き出す側（`tools/voxel/gen_prefabs.py`）だけが
     * `C['bark']` と書いた事実を知っているので、**そこで焼いて `.jsonc` に持たせる**。
     *
     * `declared` が偽なら宣言が無い（手で作った `.vox`・C++ で組んだ箱）。
     * そのときだけ色から当てる予備の道へ落ちる（`material_for_color`）。
     * @{
     */
    //! 索引ごとの材質（`MaterialClass` の番号）。索引 0 は空なので使わない。
    std::uint8_t palette_material[256]{};
    //! `.jsonc` に `palette` が在って `.vox` と食い違わなかったか。
    bool palette_material_declared{ false };
    /*! @} */
};

/*!
 * @brief `<dir>/<name>.jsonc` と `<dir>/<name>.vox` を読んで検査まで通す。
 * @param[out] err 失敗の理由（**検査に落ちた理由も含む**）。
 * @return 成功したか。
 */
bool load_prefab(const std::string &dir, const std::string &name, Prefab &out, std::string &err);

/*!
 * @brief 実際に接地しているマス（＝地面の高さ z = 0 のボクセルが乗るマス）を数える。
 * @details `load_prefab` の検査（§7.2-1）の「実体」側。**エディタ（P9）が保存時に宣言を
 * 作り直すときも同じこれを使う。**規則を 2 か所に書くと、片方だけ直された状態に必ずなる
 * （引き継ぎ §3 罠 25 と同じ種類の事故）。戻り値は昇順で重複なし。
 */
std::vector<std::pair<int, int>> compute_actual_footprint(const Prefab &prefab);

} // namespace hd2d
