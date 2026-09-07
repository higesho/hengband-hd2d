/*!
 * @file prefab_writer.h
 * @brief プレハブ（`.vox` ＋ `.jsonc` の対）の**書き出し**と、書いた直後の読み戻し照合（P9 ①）。
 *
 * 段取りは P9。
 * **この TU はエディタ側（`HD2D_EDITOR`）にしか入らない。**リリース用ビルド
 * （`/p:HengbandHd2dEditor=0`）には読み込み（`voxel/prefab.cpp`）だけが残る。
 *
 * ## 書く形は `tools/voxel/export_vox.py` と同じ
 * | | |
 * |---|---|
 * | `.vox` | 版 150。`SIZE`/`XYZI` を**パーツの並び順**に、`nTRN(ルート)→nGRP→[nTRN(名前)→nSHP]`、`RGBA` |
 * | `_t` | **模型の中心**（最小隅 + size/2 の整数除算）。人が MagicaVoxel で開いて直せることが R13 の前提 |
 * | `.jsonc` | 鍵の順序も既存ファイルと同じ（`nlohmann::ordered_json`）。**先頭の `//` 注釈は残す** |
 *
 * ## 保存は「書く → 読み戻す → 照合する」まで
 * 書けたことと読めることは別である（frame codec の `encode(decode(encode))` と同じ考え方）。
 * `save_prefab()` は書いた対を `load_prefab()` で読み戻し、**§7.2 の検査を通ったうえで**
 * 意味が等しいことまで確かめて初めて成功を返す。
 *
 * ## わざと壊すオプション（検査の検査。設計書 §14-4）
 * 環境変数 `HD2D_BREAK_EDIT` に次を入れると**わざと**壊れる。`--edit-check` が
 * FAIL になることを確かめるためにある。ふだんは空にしておくこと。
 * | 値 | 壊しかた | 何が捕まえるか |
 * |---|---|---|
 * | `vox` | `.vox` を書くとき最初のボクセルを 1 個落とす | 読み戻し照合（模型の中身の比較） |
 * | `footprint` | 保存時の footprint 更新を黙って飛ばす | `load_prefab` の §7.2-1（宣言と実体の照合） |
 */
#pragma once

#include "voxel/prefab.h"

#include <string>

namespace hd2d {

//! `save_prefab()` の報告。**黙って直したことを黙らせない**ための手段。
struct SaveReport {
    //! footprint を実体から作り直したか（作り直したら宣言のマス数も入る）。
    bool footprint_refreshed{ false };
    std::size_t footprint_cells{ 0 };
    std::size_t vox_bytes{ 0 };
    std::size_t jsonc_bytes{ 0 };
};

/*!
 * @brief `.vox` を書く。シーングラフは `prefab.parts` から作り直す（模型はパーツの並び順に詰め直す）。
 * @details `prefab.vox.models` のうち**どのパーツからも参照されない模型は書かれない。**
 * 座標は形式の都合で 0..255（`SIZE` は 256 まで）。超えていたら書かずに失敗する。
 * @param[out] err 失敗の理由。
 */
bool write_vox_file(const std::string &path, const Prefab &prefab, std::string &err, std::size_t *bytes_out = nullptr);

/*!
 * @brief `.jsonc` を書く。既存ファイルがあれば**先頭の `//` 注釈だけ**引き継ぐ。
 * @details 中ほどに手で書かれた注釈は**保存で消える**（`export_vox.py` と同じ制約）。
 * 消えることは新しい既定の先頭注釈にも書いてある。改行は既存ファイルの流儀
 * （CRLF / LF）に合わせ、新規なら LF で書く（記憶 `hengband-docs-line-endings` の教訓）。
 */
bool write_prefab_jsonc(const std::string &path, const Prefab &prefab, std::string &err, std::size_t *bytes_out = nullptr);

/*!
 * @brief 2 つのプレハブが**意味として**等しいか（読み戻し照合の中身）。
 * @details 模型は `parts[i].model_index` 経由で突き合わせる（書き出しで模型が詰め直されるので、
 * 添字そのものは比べない）。浮動小数は 1e-3 の幅で比べる（書き出しが 1e-6 へ量子化するため）。
 * @param[out] why 等しくないとき、最初に見つかった食い違い。
 */
bool prefabs_equivalent(const Prefab &a, const Prefab &b, std::string &why);

/*!
 * @brief 保存の入口。footprint を実体から作り直し（宣言の基準は保存側。`export_vox.py` と同じ）、
 * `.vox` と `.jsonc` を書き、**読み戻して照合するまで**やる。
 * @param[in,out] prefab footprint の作り直しが在中に反映される。
 * @param[out] report 何をしたか（呼び出し側が画面に出す）。
 * @return 読み戻し照合まで通ったか。
 */
bool save_prefab(const std::string &dir, const std::string &name, Prefab &prefab, SaveReport &report, std::string &err);

} // namespace hd2d
