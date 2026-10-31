/*!
 * @file nh_tiles.h
 * @brief 原作のタイルの絵（16×16）の一覧の読み方。メニューと持ち物の行の頭に絵を出す（D16）。描画にも GL にも触れない。
 *
 * ## 何のためのものか
 * 組み立てで `nethack/data/<言語>/tiles.png`（16×16 を 1 行 40 個で並べた絵）と `tiles.json`
 * （`glyph2tile` ほか。`tools/nethack/build_tiles.py`）ができる。メニューの項目は原作の glyph の値（`items[].glyph`）を持つので、
 * 画面が `glyph2tile` で絵の番号を引き、`tiles.png` から切り出す（コアは絵の番号を入れない。設計書 §3.1）。
 * 未鑑定の物は原作どおり見た目の glyph が入るので、そのまま引けば見た目の絵になる。
 *
 * ## 規則
 * - 絵の番号 n の位置は `(n % columns, n / columns)`（原作の GUI 版 `mhmap.c` と同じ）。
 * - 範囲外・負の glyph、表の値が負のものは「絵なし」（-1）。
 */
#pragma once

#include <string>
#include <vector>

namespace hd2d::nethack {

struct NhTileTable {
    int tile_size{ 16 };
    int columns{ 40 };
    int rows{ 0 };
    int tile_count{ 0 };
    std::vector<int> glyph2tile;

    bool valid() const { return (this->tile_size > 0) && (this->columns > 0) && !this->glyph2tile.empty(); }
    //! glyph から絵の番号（無ければ -1）。
    int tile_for_glyph(int glyph) const;
    //! 絵の番号から、絵の一覧の中の左上の画素の位置。範囲外なら偽。
    bool tile_origin(int tile, int &x, int &y) const;
};

/*!
 * @brief `tiles.json` の中身を読む。
 * @return 読めたら真。壊れている・必要な欄が無いときは偽で `err` に理由。
 */
bool nh_tiles_parse(const std::string &json_text, NhTileTable &out, std::string &err);

//! 絵の一覧の画像（幅 `tex_w`・高さ `tex_h`）の中の、絵 1 つの UV。隣の絵を拾わないよう、ごくわずかに内側を取る（貼るときは最近傍にする）。
bool nh_tile_uv(const NhTileTable &table, int tile, int tex_w, int tex_h, float &u0, float &v0, float &u1, float &v1);

} // namespace hd2d::nethack
