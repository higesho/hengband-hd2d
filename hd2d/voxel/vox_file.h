/*!
 * @file vox_file.h
 * @brief MagicaVoxel `.vox` の読み込み（**シーングラフ＝パーツ対応**）。
 *
 * 段取りは P1 ①。
 *
 * ## なぜ自前で書くのか
 * 形式は RIFF 風の入れ子チャンクで、要るのは `SIZE` / `XYZI` / `RGBA` と
 * `nTRN` / `nGRP` / `nSHP` の 6 種類だけである（計画 §1「.vox の解析は自前で書く」）。
 *
 * ## パーツ
 * シーングラフを辿って**名前つきの節点ごとに 1 パーツ**として取り出す。名前は
 * `nTRN` の属性 `_name`。`.jsonc`（`prefab.h`）はこの名前で部品を指す。
 *
 * ## 座標
 * MagicaVoxel の平行移動 `_t` は**模型の中心の位置**なので、
 * 最小隅 = `_t - size / 2`（整数除算）に直して持つ。人が MagicaVoxel で開いて
 * 直せることが R13 の前提なので、こちらの都合で約束を変えない。
 *
 * ## 一辺 256 を超える模型（`XYZ2`。2026-08-21）
 * `XYZI` は座標を**1 バイト**で持つので、**一辺 256 までしか表せない**。実体の板を
 * 512px で焼けるようにするため（こう決めた——「人物・アイテム・モンスターは
 * 8/16/32/64/128/256/512 を受け入れられるように」）、座標を 16 ビットにした
 * **`XYZ2` チャンク**を足した。中身は `int32 個数` ＋ 個数 × `(u16 x, u16 y, u16 z, u8 索引, u8 詰め)`。
 *
 * - **256 までは従来どおり `XYZI`** で書く。既存の素材は 1 バイトも変わらない。
 * - MagicaVoxel は `XYZ2` を知らないので**開けない**。256 を超える模型は元より
 *   MagicaVoxel が扱えない（形式の上限がそこにある）ので、失うものは無い。
 * - `XYZI` なのに SIZE が 256 を超えている物は**読み込みを失敗させる**。
 *   黙って捨てると絵が折り返して重なる（実際に 512 の板で踏んだ。1 バイトへ
 *   切り詰めると x=256 が x=0 に化けて、左半分に右半分が重なった）。
 *
 * ## いま対応していないもの（黙って間違えるより落とす）
 * - `nTRN` の回転 `_r` が単位行列でないもの → **読み込みを失敗させる**。
 *   §7.1-2 の「4 方向はロード時に用意する」はデータを 1 方向で持つ前提なので、
 *   素材の側が回っている必要が無い。必要になったら P3 の向き対応で入れる
 * - 複数フレーム（アニメーション）の `nTRN` → 先頭フレームだけ見る
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hd2d {

//! `XYZI`（1 バイト座標）で表せる一辺の上限。これを超える模型は `XYZ2` で書く。
constexpr int kVoxClassicMaxSide = 256;
/*!
 * @brief 1 模型のボクセル数の上限（密配列なので、そのままメモリになる）。
 * @details 512 の板は詰めて `512×32×512 ≒ 8.4M` なので通る。**512³ の立方体（134M）は
 * 落とす**——壊れた SIZE で 134MB を確保して落ちるより、理由を出して止まるほうがよい。
 */
constexpr std::size_t kVoxMaxVoxels = 32u << 20;

//! 1 つの密なボクセル配列。索引 0 は空。
struct VoxModel {
    int size[3]{}; //!< x, y, z
    std::vector<std::uint8_t> voxels; //!< `x + (y * sx) + (z * sx * sy)`

    std::uint8_t at(int x, int y, int z) const
    {
        return this->voxels[static_cast<std::size_t>(x)
            + (static_cast<std::size_t>(y) * static_cast<std::size_t>(this->size[0]))
            + (static_cast<std::size_t>(z) * static_cast<std::size_t>(this->size[0]) * static_cast<std::size_t>(this->size[1]))];
    }
};

//! シーングラフから取り出した 1 パーツ。
struct VoxPart {
    std::string name; //!< `nTRN` の `_name`。無ければ `"part<番号>"`
    int model_index{ 0 };
    int origin[3]{}; //!< プレハブ原点から見た**最小隅**（ボクセル単位）
};

struct VoxFile {
    std::vector<VoxModel> models;
    std::vector<VoxPart> parts;
    //! 索引 1..255 の RGBA。`palette[i]` が索引 i の色（`palette[0]` は使わない）。
    std::uint8_t palette[256][4]{};

    const VoxPart *find_part(const std::string &name) const;
};

/*!
 * @brief `.vox` を読む。
 * @param[out] err 失敗の理由。
 * @return 成功したか。**失敗したら `out` は使わないこと**（途中まで埋まっている）。
 */
bool load_vox_file(const std::string &path, VoxFile &out, std::string &err);

} // namespace hd2d
