/*!
 * @file floor_meaning.h
 * @brief 意味づけ層 — **マスの「並び」を読んで意味を作る**（P3 ②）。
 *
 * 段取りは P3 ②。
 *
 * ## なぜ要るのか（§9 の冒頭）
 * 現行 HD2D は **マス → 面** の 1 対 1 で、ここに何を足してもチープさが抜けない。
 * **マス 1 つを見てもそこに何を置くべきかは決まらない**からである。
 * 「幅 1 マスの連なりなら通路」「厚い壁の塊なら岩盤」は、**並びを見て初めて言える**。
 *
 * ## フロア全域はどこから来るか（§9 ① からの変更）
 * 設計書 ① は「可視窓のマスを ui 側で**蓄積**して永続マップを作る」としていたが、
 * **`GameFrame::minimap` が既にフロア全域を毎フレーム運んでいる**
 * （`MinimapSnapshot`。1 マス 1 バイト・198×66 で約 13KB。§9 が「コストは無視できる」と
 * 見積もった量そのもの）。蓄積しないほうが
 *
 * - コアの既知情報と**必ず一致する**（蓄積は古い記憶が残りうる）
 * - フロアを移ったときの捨て忘れが起きない
 *
 * ので、**蓄積はやめてミニマップを読む**。§9 ① の目的（可視窓の外を知る）はこれで満たされる。
 *
 * ## 作り直す条件
 * **フロア識別子（`GameFrame::floor`）が変わったとき**と、既知マスが増えたとき。
 * 毎フレーム作り直すのは無駄だが、探索で既知が増えるので追随はしなければならない。
 */
#pragma once

#include "frame/game_frame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hd2d {

//! 1 マスの役割。**描く側はこれだけを見る**（地形テーブルは知らない。§9.4）。
enum class CellRole : std::uint8_t {
    Unknown = 0, //!< 未探知。何も置かない
    RoomFloor, //!< 部屋の床（加工された石積みの中）
    CorridorFloor, //!< 通路の床（掘りっぱなし）
    StructuralWall, //!< 部屋・通路に面した薄い壁
    Bedrock, //!< 厚い壁の塊＝岩盤
    Doorway, //!< 扉
    Stairs, //!< 階段・坑道
};

//! 1 マスに付く印（役割と直交する性質）。
enum CellMark : std::uint8_t {
    MARK_WALL_SIDE = 0x01, //!< 床のうち壁に接するもの（松明・樽・骨・草の置き場所）
    MARK_DEAD_END = 0x02, //!< 行き止まり（瓦礫・落石・きのこ・蜘蛛の巣）
    MARK_ROOM_EDGE = 0x04, //!< 部屋の床のうち壁に接するもの（柱の置き場所）
};

/*!
 * @brief フロア全域の意味づけ結果。
 * @details 添字はすべて `y * width + x`（ミニマップと同じ）。
 */
struct FloorMeaning {
    int width{ 0 };
    int height{ 0 };
    FloorIdentity identity;
    std::vector<std::uint8_t> roles; //!< `CellRole`
    std::vector<std::uint8_t> marks; //!< `CellMark` のビット和
    /*!
     * @brief 壁の厚み（開けた場所からのチェビシェフ距離）。壁でなければ 0。
     * @details 1 = 開けた場所に面している。**3 以上を岩盤**とみなす（§9.1「壁成分の厚み」）。
     */
    std::vector<std::uint8_t> wall_depth;
    /*!
     * @brief そのマスは**山**か（1 = 山。`MinimapKind::Mountain`）。
     *
     * @details 役割（`CellRole`）とは直交する。山のマスの役割は従来どおり
     * `StructuralWall` / `Bedrock` で、**描き方は 1 つも変えない**——この欄を見るのは
     * 町の読み方だけである（`town_plan.cpp` の `TownRole::Crag`）。
     *
     * **同じフロアのあいだは消さない。**ミニマップの種別はそのフレームの見えなので、
     * 山のマスに飛ぶ敵が載ると `Monster` に化けて山が消える（階段で同じことが起きたので
     * `was_stairs` がある）。山は動かないので、一度«山»と読めたマスは山でよい。
     */
    std::vector<std::uint8_t> mountains;

    //! 画面と記録に出す内訳。
    int room_cells{ 0 };
    int corridor_cells{ 0 };
    int structural_cells{ 0 };
    int bedrock_cells{ 0 };
    int mountain_cells{ 0 }; //!< 山のマス（`mountains` の 1 の数）
    int known_cells{ 0 };
    /*!
     * @brief ミニマップの**中身の指紋**（FNV-1a。実体の印は床へ均してから混ぜる）。
     * @details 「既知が増えた」だけを見ていると、**既知のまま中身が変わる**書き換えに
     * 追随できない——掘削・岩溶解で壁が床になっても既知の数は 1 つも増えないので、
     * 掘った壁がそのまま描かれ続けた（2026-08-11 に気づいた「掘った壁がそのまま
     * 表示されてしまうことがある」。探索で既知が増えれば直っていたので「ことがある」）。
     */
    std::uint64_t content_hash{ 0 };

    bool valid() const { return (this->width > 0) && (this->height > 0); }
    CellRole role_at(int x, int y) const;
    std::uint8_t mark_at(int x, int y) const;
    //! そのマスは山か（範囲外は偽）。
    bool mountain_at(int x, int y) const;
};

//! 同じフロアか（識別子の一致）。
bool same_floor(const FloorIdentity &a, const FloorIdentity &b);

/*!
 * @brief 必要なら作り直す。
 * @return 作り直したか。
 * @details 作り直すのは **(a) フロア識別子が変わった (b) 既知マスの数が増えた** とき。
 * 探索で既知が増えるので (b) が要る。毎フレーム作り直すのは 13,000 マスの走査 3 回ぶんで、
 * 実測はしていないが安くはない。
 */
bool rebuild_floor_meaning(const GameFrame &frame, FloorMeaning &out);

//! 内訳の 1 行（画面と記録に出す）。
std::string floor_meaning_summary(const FloorMeaning &meaning);

} // namespace hd2d
