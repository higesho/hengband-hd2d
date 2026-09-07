/*!
 * @file minimap_snapshot.h
 * @brief ミニマップ用のフロア全域スナップショット（設計書 §4.6 追補）
 *
 * `GameFrame::cells` は**視界内のビューポートだけ**を持つため、フロア全体を見せる
 * ミニマップには使えない。ここでフロア全域を 1 格子 1 バイトの種別コードで持つ。
 *
 * 所属は presentation/frame 専属。ui は include するのみ（循環禁止）。
 * コア型・SDL 依存なし。
 */
#pragma once

#include <cstdint>
#include <vector>

//! ミニマップの 1 格子の種別。描画側はこれだけを見て色を決める。
//!
//! 1〜4 と 8 が地形、5〜7 が**地形の上に重ねて書かれた印**（実体）で、
//! 実体は地形を上書きする（自分 > 敵 > アイテム > 地形）。
//! 番号の大小がその優先順位だったのは 7 までで、**8 は地形である**——
//! 既に記録済みのフレーム（ボットの JSON）と値が食い違わないよう、
//! 詰め直さずに末尾へ足した。
enum class MinimapKind : uint8_t {
    Unknown = 0, //!< 未踏破。描かない
    Floor = 1, //!< 床・通路
    Wall = 2, //!< 壁・鉱脈
    Door = 3, //!< 扉
    Stairs = 4, //!< 階段・坑道・クエスト入口
    Item = 5, //!< 床に落ちているアイテム
    Monster = 6, //!< 見えている敵
    Player = 7, //!< 自分
    /*!
     * @brief **山**（`TerrainCharacteristics::MOUNTAIN`）。壁の一種だが山である。
     *
     * @details 町の読み方が「侵入不可の塊は敷地（建物）か岩山か」を決めるのに要る
     * （`hd2d/world/town_plan.cpp` の `TownRole::Crag`）。**並びの形では見分けられない**
     * ——イークの洞窟の口を抱いた露岩（25 マス・充填率 0.78）と、モリバントの雑貨屋の
     * 敷地（39 マス・充填率 0.93）は、どちらも「壁の塊に 1 マスの穴が開いた形」で、
     * 穴の中身（ダンジョンの口かクエストの入口か）も `Stairs` で同じに見える。
     *
     * 山かどうかは**地形テーブルだけが知っている**が、可視窓の `terrain_id` は
     * 間近で見たマスにしか無い（罠 40）。フロア全域を運ぶのはここだけなので、
     * ここに 1 種別足すのがいちばん狭い直しになる。
     *
     * @note 実体（敵・アイテム）が載ると上書きされて消える。山は動かないので、
     * 受け取る側（`FloorMeaning::mountains`）が**フロアのあいだ覚えておく**。
     */
    Mountain = 8,
    /*!
     * @name 歩ける地形の細別（9〜13。FH-09。2026-08-24）
     *
     * @details どれも**役割は `Floor` と同じ**（歩ける。経路判定・町の読み方は
     * `Floor` と同じに扱うこと）で、違うのはミニマップの塗り分けだけである。
     * FroxComposband の町と荒野は草・木・水が大半で、全部 `Floor` に畳むと
     * 一色塗りになって読めない（Outpost がほぼ青一色）のが出どころ。
     *
     * 送るのは今のところ Frox だけ。**他のコアは `Floor` のまま**で、それは
     * 欠けではない（`Mountain` の追補と同じく、詰め直さず末尾へ足した——
     * 記録済みのフレームと値が食い違わないように）。
     * @{
     */
    Water = 9, //!< 水（深浅は分けない）
    Tree = 10, //!< 木・雪をかぶった木
    Grass = 11, //!< 草・花
    Lava = 12, //!< 溶岩
    Snow = 13, //!< 雪原・雪解け（Snow castle の床）
    /*! @} */
};

struct MinimapSnapshot {
    int width{}; //!< フロアの格子幅。0 なら未充填（描画しない）
    int height{};
    int player_gx{-1};
    int player_gy{-1};
    //! width * height 個。添字は y * width + x。
    std::vector<uint8_t> kinds;

    bool valid() const
    {
        return this->width > 0 && this->height > 0 &&
               this->kinds.size() == static_cast<size_t>(this->width) * static_cast<size_t>(this->height);
    }

    MinimapKind at(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= this->width || y >= this->height) {
            return MinimapKind::Unknown;
        }
        return static_cast<MinimapKind>(this->kinds[static_cast<size_t>(y) * static_cast<size_t>(this->width) + static_cast<size_t>(x)]);
    }
};
