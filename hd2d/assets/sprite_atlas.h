/*!
 * @file sprite_atlas.h
 * @brief 既存タイル（`tilework/sfc/*.png`）を 32px へ落としてアトラスへ積む（P4 ①④）。
 *
 * 段取りは P4。
 *
 * ## なぜビルボードなのか（§11）
 * 「HD-2D」は「**HD の 3D 背景 × 2D のドット絵キャラ**」の意味であり、キャラをポリゴン化
 * すると参考画像の質感から遠ざかる。R 1,330 / K 367 の既存資産がそのまま使えるので、
 * **素材の量の問題の大半がここで消える**。
 *
 * ## 640px → 32px（§11 の課題そのもの）
 * > 640px のマスタをそのまま 32px へ落としても「ドット絵」にはならず「小さい絵」になる。
 * > 減色して階段状の輪郭を作る後処理か、32px で描き起こすかが要る。
 *
 * ここでやっているのは前者で、順に
 * 1. **α で重みを付けた**箱平均（透明画素の色を混ぜない。混ぜると輪郭が暗く濁る）
 * 2. **α をしきい値で 0/255 に振る** — これが「階段状の輪郭」を作る本体
 * 3. 各成分を段に丸める（減色）
 *
 * 2 が要点である。半透明の縁を残すと、拡大したとき「ぼけた小さい絵」に見える。
 *
 * ## 遅延読み込み
 * 目録には 1,700 枚以上ある。**画面に出た索引だけ**焼く。1 枚あたり 32×32×4 = 4KB。
 */
#pragma once

#include "render/gl_core.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace hd2d {

//! アトラス上の 1 枚の場所（テクセル）。
struct SpriteRect {
    int x{ 0 };
    int y{ 0 };
    int w{ 0 };
    int h{ 0 };
    bool valid{ false };
};

class SpriteAtlas {
public:
    //! 1 枚の辺（px）。設計書 §5「1 マス 32 ボクセル」とビルボードの粒度を揃える。
    static constexpr int kSpritePx = 32;
    //! アトラスの辺。32px なら 64×64 = 4,096 枚入る（R 1,330 ＋ K 367 ＋ P で足りる）。
    static constexpr int kAtlasSide = 2048;

    bool init(std::string &err);
    void shutdown();

    /*!
     * @brief 索引の絵を引く（無ければその場で読んで焼く）。
     * @param path PNG の絶対パス。空なら失敗。
     * @return 場所。読めなければ `valid = false`（**落ちない**。呼び出し側は描かない）。
     */
    const SpriteRect &acquire(std::uint16_t tile_index, const std::string &path);

    gl::GLuint texture() const { return this->texture_; }
    int side() const { return kAtlasSide; }
    int loaded() const { return this->loaded_; }
    int failed() const { return this->failed_; }

private:
    bool blit(const std::string &path, int at_x, int at_y);

    gl::GLuint texture_{ 0 };
    int pen_x_{ 0 };
    int pen_y_{ 0 };
    int loaded_{ 0 };
    int failed_{ 0 };
    bool full_warned_{ false };
    std::unordered_map<std::uint16_t, SpriteRect> rects_;
    SpriteRect invalid_;
};

} // namespace hd2d
