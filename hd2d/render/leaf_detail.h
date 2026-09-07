/*!
 * @file leaf_detail.h
 * @brief **木の葉**——葉の面に葉の形を描く（2026-08-23 に決めた「木の葉を詳細な葉にしたい」）。
 *
 * ## なぜ形を彫らないのか
 * 樹冠は**塊ごとに 1 色の直方体**で作ってある（`tools/voxel/gen_prefabs.py` の `tree()`）。
 * 球で葉を置いた初版は 1 本 65,000 三角形まで膨れ、8,409 本の町では成立しなかった
 * **彫る道は既に閉じている**ので、
 * 面の中に描く（`render/surface_wear.h` と同じ手。`fract(v_uv)` が 1 マスの面の中の位置）。
 *
 * ## 3 つの層
 * | 層 | 何を引くか | どこで効くか |
 * |---|---|---|
 * | 粒 | 面アトラスの位置（1 ボクセルに 1 つ） | **遠く**。樹冠が一様な緑の塊でなくなる |
 * | 塊の縁 | 1 マスの面の縁までの距離 | 中くらい。葉の固まりが分かれて見える |
 * | 葉の形 | 面を `cells²` に割って 1 枚ずつ楕円と中肋 | **近く**（一人称・寄った見下ろし） |
 *
 * ## 遠くでは形を描かない（**ここが肝**）
 * 見下ろしの町は 1 マス 65 画素・1 ボクセル 2 画素しかない。そこへ葉を 2×2 で描くと
 * **1 画素を割ってちらつく**。`fwidth(v_uv)`（1 画素が何ボクセルぶんか）で
 * **細かい層から順に消す**。消えても粒は残るので、遠くでも「のっぺりした緑」には戻らない。
 *
 * ## 掛ける相手
 * 材質が `MaterialClass::Leaf` の面だけ（`render/surface_wear.h`）。**草と苔は別**である
 * ——苔に葉脈が生えると嘘になるので、材質表で `Plant` と分けてある。
 */
#pragma once

#include "render/gl_core.h"

#include <string>

namespace hd2d {

/*!
 * @brief 木の葉の描き方。**1 フレーム 1 個。**
 * @details 実物を見て詰める値なので、**どれも「仮」**（記憶 `hengband-visual-review-ask-the-metric`）。
 */
struct LeafParams {
    //! 全体の強さ（0 = 描かない。**シェーダが早く抜ける**）。
    float amount{ 0.f };
    /*!
     * @brief 1 マスの面の一辺に葉を何枚並べるか（**既定 1 ＝ 面あたり 1 枚**）。
     * @details 初版は 2（4 枚）だったが、**網戸に見えた**——1 枚が小さすぎて形が読めず、
     * マスの境の暗い線だけが規則正しく残る。上げるほど葉が小さくなり、
     * **ちらつきの始まる距離も近くなる**（`fwidth` の判定に掛かる）。
     */
    float cells{ 1.f };
    //! 葉と葉の間の暗さ（0.30 ＝ 30% 落とす）。塊の縁にはこの半分を掛ける。
    float gap{ 0.30f };
    //! 中肋（葉の芯の筋）の明るさ。
    float vein{ 0.22f };
    //! ボクセル 1 個ごとの明暗と色みのばらつき。**遠くで効くのはここだけ。**
    float scatter{ 0.14f };
};

/*!
 * @brief 木の葉の式（GLSL）。**ボクセルのフラグメントシェーダが連結して使う。**
 * @details `#version` 行は含まない。`kSurfaceWearGlsl` の後ろに置くこと
 * （材質の番号を共有しているため、並びを一定にしておくと info log の行が読みやすい）。
 */
extern const char *const kLeafDetailGlsl;

//! 木の葉のユニフォームの場所。プログラムごとにリンク後 1 回だけ引く。
struct LeafUniforms {
    gl::GLint amount{ -1 };
    gl::GLint cells{ -1 };
    gl::GLint gap{ -1 };
    gl::GLint vein{ -1 };
    gl::GLint scatter{ -1 };

    void locate(gl::GLuint program);
};

//! 木の葉を送る。**`glUseProgram` 済みであること。**
void upload_leaf(const LeafUniforms &loc, const LeafParams &leaf);

/*!
 * @brief `--leaf=` の綴りから読む（`off` / `on` / 0.0〜2.0）。
 * @details 読み方は汚し（`parse_wear_amount`）と同じ。**口を分けてある**のは、
 * 片方だけ綴りを増やしたくなったときに相手を巻き込まないため。
 */
bool parse_leaf_amount(const std::string &spec, float &out);

} // namespace hd2d
