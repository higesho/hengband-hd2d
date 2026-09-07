/*!
 * @file map_cell_view.h
 * @brief マップセル表示スナップショット（設計書 §4.3）
 */
#pragma once

#include <cstdint>

struct MapCellView {
    int16_t gx{};
    int16_t gy{};
    uint16_t terrain_id{};
    uint16_t feature_flags{};
    uint16_t monster_id{};
    /*!
     * @brief **その 1 体を指す通し番号**（コアの `m_idx`）。0 なら「居ない／見えていない」。
     *
     * @details 版 3.0（K-41）で新設。`monster_id` は**種族**の番号なので、同じ種族が 2 体
     * 並んでいると「どっちがどっちへ動いたか」が分からない。なめらか移動は
     * 「前のフレームでこの個体がどこに居たか」を引く必要があるので、個体を一意に指す番号が要る。
     * 立てる条件は `monster_id` と同じ（`mon.ml`＝視認中のみ）。見えていない敵の位置は漏らさない。
     * @note 死んだ枠は再利用されるので、番号が同じでも別の個体でありうる。遠くへ跳んだときは
     * 補間せず吸着する作りにしてあるので、取り違えても長い流し撮りにはならない。
     */
    uint16_t monster_slot{};
    uint16_t object_id{};
    uint8_t light_level{};
    uint8_t fg_color{};
    uint8_t bg_color{};
    char ascii_fallback{};
    uint16_t tile_index{};
    /*!
     * @brief **足元に敷く地面**のタイル索引。0 なら「無い」（ui は従来どおり `tile_index` を敷く）。
     *
     * @details 版 2.7 で新設。立ち木・岩を垂直な板で描くと、
     * そのマスの床が抜けて穴になる。板の下に敷く地面が要るが、**ui は地形テーブルを知らない**
     * （設計書 §4.1）ので「木なら草・岩なら床」という**地形 → 地形の対応付けは Bridge の仕事**。
     * `presentation::lookup_terrain_tile()` で引いた索引をそのまま渡す。
     * 索引が引けなければ 0 のままにして、ui 側を従来の見え方へ落とす（捏造しない）。
     */
    uint16_t under_tile_index{};

    /*!
     * @name 8px/16px タイル面（`lib/xtra/graf/`）の位置。負値は「無い」。
     *
     * @details `SdlUiOptions::map_style_index` が 8px／16px のときだけ入る。
     * コアはグラフィックモードで **attr の下位 7bit を行・char の下位 7bit を列**として
     * タイル面の位置を返す（`src/main-win.cpp` の `term_pict_win` と同じ約束）。
     * どの実体がどのタイルかを決めているのはコアの prf（`graf-new.prf` /
     * `graf-xxx.prf`）なので、**ui 側に対応表は持たない**。
     *
     * `bg_*` は下敷き（そのマスの地形）。16px 版は `mask.bmp` で前景が抜けるので、
     * 下敷き → 前景の順に重ねないとモンスターの周りが黒く四角く残る。
     * マスクを持たない 8px 版では前景が不透明なので下敷きは出番が無い。
     * @{
     */
    int16_t graf_fg_row{ -1 };
    int16_t graf_fg_col{ -1 };
    int16_t graf_bg_row{ -1 };
    int16_t graf_bg_col{ -1 };
    /*! @} */
};
