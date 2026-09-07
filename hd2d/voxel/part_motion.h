/*!
 * @file part_motion.h
 * @brief パーツの動き（剛体）を行列にする。P6 ①④。
 *
 * 段取りは P6。
 *
 * ## どこまでがここの仕事か
 * | | |
 * |---|---|
 * | ここ | **剛体**（回転・振り子・状態連動）。パーツ丸ごとをピボットのまわりで動かす |
 * | シェーダ | **風**（草・葉・布）。頂点ごとに曲げるので CPU では持てない（§8.1） |
 *
 * ## ピボットは「プレハブ原点から」
 * `PrefabPart::pivot` はプレハブ原点を基準にしたボクセル座標である（`prefab.h`）。
 * したがって 1 パーツぶんの変換は
 *
 * ```
 * A = T(pivot) · R · T(-pivot)          （すべてプレハブ空間・マス単位）
 * ```
 *
 * で、親を持つパーツは**親の A を左から掛ける**。親のピボットも同じ基準なので、
 * 親子で座標系を変換し直す必要がない。
 *
 * ## 当たり判定は動かさない（§8.2）
 * ここが返すのは**見た目の行列だけ**である。`Prefab::footprint` にも
 * `PrefabPart::grounded` にも一切触らない。`--motion-check` がそれを毎回確かめる。
 */
#pragma once

#include "render/math3d.h"
#include "voxel/prefab.h"

#include <string>
#include <vector>

namespace hd2d {

/*!
 * @brief 動きを決める外からの入力。
 * @details 時刻以外に「状態」を持つのは `MotionKind::StateLinked`（扉の開閉）のため。
 */
struct MotionContext {
    //! 秒。**世界時刻**（インスタンスごとに変えない。風が波として渡るのと同じ理由）。
    float time{ 0.f };
    /*!
     * @brief 名前つきの状態（0 = 閉 〜 1 = 開）。`StateLinked` が引く。
     * @details いまは `--prefab` の見物で人が動かす口しかない。コアの扉の開閉に繋ぐのは
     * プロトコル §12-10（イベントの縁）が要るので P6 では繋いでいない。
     */
    std::vector<std::pair<std::string, float>> states;

    //! 名前で引く。無ければ 0。
    float state(const std::string &name) const;
};

/*!
 * @brief パーツ 1 つぶんの動きの行列（プレハブ空間・マス単位）。**親は含まない。**
 * @param voxels_per_cell 1 マスあたりのボクセル数（ピボットをマス単位へ直すのに使う）。
 * @details 静止パーツと `Wind`（シェーダ側の仕事）は単位行列を返す。
 */
Mat4 part_motion_matrix(const PrefabPart &part, int voxels_per_cell, const MotionContext &ctx);

/*!
 * @brief プレハブ全体の、親を辿って合成済みの動きの行列を作る。
 * @param[out] out `prefab.parts` と同じ並び。
 * @details 親は必ず自分より前にあるとは限らないので、親を先に解決してから使う。
 * 循環は `load_prefab` の検査（§7.2-3）で弾かれているので、ここでは仮定してよい。
 */
void compose_part_motions(const Prefab &prefab, const MotionContext &ctx, std::vector<Mat4> &out);

} // namespace hd2d
