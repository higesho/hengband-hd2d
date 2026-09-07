/*!
 * @file cell_feature_bits.h
 * @brief MapCellView::feature_flags のビット定義（HD2D 立体化の入力）
 *
 * HD2D は「壁はブロック／扉は薄板／階段は透過対象」のように**地形の性質**で描き分ける。
 * tile_index や terrain_id から性質を逆引きすると ui が地形テーブルを知ることになるため、
 * Bridge が `TerrainCharacteristics` を読んでここのビットへ翻訳する。
 *
 * 所属は presentation/frame 専属。ui は include するのみ（循環禁止）。
 * コア型・SDL 依存なし。
 */
#pragma once

#include <cstdint>

//! MapCellView::feature_flags のビット。見た目（MIMIC）に基づく。
enum CellFeatureBit : uint16_t {
    //! 壁・鉱脈など。HD2D ではブロックとして立ち上げる。
    CELL_FEAT_WALL = 0x0001u,
    //! 扉（開閉問わず）。HD2D では 1/3 厚の薄板。
    CELL_FEAT_DOOR = 0x0002u,
    //! 開いた扉／壊れた扉。HD2D では開口部を抜いて奥が見えるようにする。
    CELL_FEAT_DOOR_OPEN = 0x0004u,
    //! 階段・坑道・クエスト入口。遮蔽されたら半透明で示す対象。
    CELL_FEAT_STAIRS = 0x0008u,
    //! 永久壁（外周など）。
    CELL_FEAT_PERMANENT = 0x0010u,
    /*!
     * @brief 立ち木。壁ではないが高さのある障害物。
     * @details 版 2.7 から**ブロックではなく板**として描く（設計書 §4.6）。
     */
    CELL_FEAT_TREE = 0x0020u,
    //! 水面。床より僅かに低く描く。
    CELL_FEAT_WATER = 0x0040u,
    //! 溶岩。水と同じく低く、かつ自発光扱い。
    CELL_FEAT_LAVA = 0x0080u,
    //! 常時発光地形。ほこり粒子の光源判定に使う。
    CELL_FEAT_GLOW = 0x0100u,
    //! プレイヤが立っているマス。
    CELL_FEAT_PLAYER = 0x0200u,
    //! 地形が既知（描画してよい）。未知マスは 0。
    CELL_FEAT_KNOWN = 0x0400u,
    /*!
     * @brief **通り抜けられる床**（`MOVE` があり、壁でも扉でも立木でもない）。
     * @details 扉の薄板の向きを決めるのに要る（旧 HD2D の場面づくり）。
     * 「隣が壁か」で向きを決めると暗いダンジョンで破綻する。**扉は見えていてもその扉が
     * はまっている壁はまだ未探知**という状況が普通に起きるからで、そのとき壁の数は
     * 両軸とも 0〜1 に落ち、判定が情報の無い側へ倒れる。
     * 一方**通路や部屋の床は必ず既知**である（プレイヤがそこに立っている／灯りが届いている
     * から扉が見えている）。したがって「どちらへ通り抜けられるか」を主にする方が頑健。
     * 本ビットは `CELL_FEAT_KNOWN` の付くマスにしか立たないので、
     * 「未探知の隣は判定に使わない」が自動的に満たされる。
     */
    CELL_FEAT_PASSABLE = 0x0800u,
    /*!
     * @brief 岩石（`RUBBLE` とその水上派生）。版 2.7 で新設。
     * @details 立ち木と同じく**立ち木・岩の板**で描く（設計書 §4.6）。
     * 足元には床タイルを敷く（立ち木は草）。索引は Bridge が `under_tile_index` で渡す。
     */
    CELL_FEAT_RUBBLE = 0x1000u,
    /*!
     * @brief 生成された部屋の一部（コアの `CAVE_ROOM`）。P10 で新設。
     * @details 部屋か通路かの**正**。ミニマップの「床の連なりの幅」からの推定は、暗い部屋を
     * 歩いた直後に幅 1 で「通路」と誤読し、探索が進むと部屋へ再分類されて**床の見た目が
     * 後から化ける**（実機で見つけた）。既知マスにしか立たない。
     */
    CELL_FEAT_ROOM = 0x2000u,
    /*!
     * @brief マス自体が明るい（コアの `CAVE_GLOW`。消灯中 `CAVE_MNDK` は除く）。P10 で新設。
     * @details プレイヤの灯りではなく**場所の明るさ**。明るい部屋にだけ松明の装飾を
     * 置くための材料。既知マスにしか立たない。
     */
    CELL_FEAT_GLOWING = 0x4000u,
};

/*!
 * @brief HD2D で「高さを持つ直方体（ブロック）」とみなすか。
 * @details 版 2.7 で**立ち木を外した**。立ち木と岩は `cell_feature_is_object_board()` の側で、
 * 透過タイルを貼った垂直な板として描く（人間の指示。設計書 §4.6）。
 * ここへ戻すと「木が壁の 1.3 倍の高さの箱」に逆戻りするので足さないこと。
 */
inline bool cell_feature_is_block(uint16_t flags)
{
    return (flags & CELL_FEAT_WALL) != 0u;
}

/*!
 * @brief HD2D で「オブジェクトの板」（正面を向いた垂直な板 1 枚）として描くか。
 * @details 立ち木と岩。**ブロックではないが遮蔽物ではある**ので、描く側は深度を必ず書くこと
 * （アルファテストで抜く＝パス 3 と同じ扱い）。設計書 §4.6。
 * @note 版 2.7〜2.9 は＋字に交差した 2 枚だった。版 3.0 で正面 1 枚へ戻した（人間の指示）。
 */
inline bool cell_feature_is_object_board(uint16_t flags)
{
    return (flags & (CELL_FEAT_TREE | CELL_FEAT_RUBBLE)) != 0u;
}

//! HD2D で「薄板（扉）」とみなすか。
inline bool cell_feature_is_door(uint16_t flags)
{
    return (flags & CELL_FEAT_DOOR) != 0u;
}

//! HD2D で「通り抜けられる床」とみなすか（未探知は必ず false）。
inline bool cell_feature_is_passable(uint16_t flags)
{
    return (flags & CELL_FEAT_PASSABLE) != 0u;
}
