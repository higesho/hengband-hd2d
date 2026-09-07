/*!
 * @file combat_fx.h
 * @brief 戦闘の見せ場を画面へ運ぶ形
 *
 * コアの `CombatEvent`（`src/core/combat-feedback.h`）を、**コアの型を持たない形**へ直したもの。
 * 画面側（`hd2d/`）はコアに触れないので（必守制約 6）、`AttributeType` をそのまま渡せない。
 *
 * ## 属性は「束」にして渡す
 * `AttributeType` は 80 種類以上ある。全部に色を振ると表の保守が破綻するうえ、
 * 遊んでいて区別がつかない。**見て分かる粒度**へ落とす仕事は Bridge が引き受け、
 * 画面側は束ごとの見た目だけを決める。
 */
#pragma once

#include <cstdint>

//! 何が起きたか。
enum class CombatFxKind : uint8_t {
    HitPlayer = 0, //!< 自分が受けた（全面のフラッシュと揺れ）
    HitMonster = 1, //!< 相手に当てた（そのマスで弾ける）
    Bolt = 2, //!< 飛道（`src` から `dst` へ走る）
};

//! 属性の束。**色を決めるのは画面側**で、ここでは種類だけを言う。
enum class CombatFxElement : uint8_t {
    Physical = 0, //!< 素手・武器・矢・轟音・破片（既定の落とし先）
    Fire = 1,
    Cold = 2,
    Elec = 3,
    Acid = 4,
    Poison = 5,
    Dark = 6, //!< 暗黒・地獄・深淵・虚無
    Light = 7, //!< 閃光・光の剣
    Chaos = 8, //!< カオス・因果混乱・混乱・時間逆転
    Psy = 9, //!< 精神攻撃
    /*
     * ---- ここから下は**幻想蛮怒だけが送る**----
     *
     * 変愚は 1 度も立てない。**変愚でも埋めるべきもの、と読まないこと**
     * ——幻想蛮怒が新設した耐性の軸で、変愚に同じ軸が無いから足してある。
     *
     * 足しても古い相手は壊れない（同 §6.4「B を選んだとき、変愚は壊れるか」）:
     * 線の上は生の数字で復号は範囲を見ず、色を引く側は `default` で白へ落ちる。
     */
    //! 時空。幻想蛮怒の `resist_time` が守る 4 つ（`GF_TIME` / `GF_NEXUS` / `GF_DISTORTION` / `GF_GRAVITY`）。
    //! **変愚では同じ攻撃が混沌や物理のまま**で、これは取りこぼしではない——
    //! 変愚には因果混乱耐性が在り、幻想蛮怒はそれを廃して時空へ畳んだ（`change.txt`）。
    Spacetime = 10,
    //! 狂気。幻想蛮怒の `resist_insanity` が守る `GF_COSMIC_HORROR` ただ 1 つ。
    //! 変愚に同じ軸は無い。
    Insanity = 11,
};

struct CombatFxEvent {
    CombatFxKind kind{ CombatFxKind::HitPlayer };
    CombatFxElement element{ CombatFxElement::Physical };
    //! 起きたマス（`Bolt` は着弾側）。
    int16_t y{ 0 };
    int16_t x{ 0 };
    //! `Bolt` の撃ったマス。ほかは `y`/`x` と同じ。
    int16_t src_y{ 0 };
    int16_t src_x{ 0 };
    //! 強さ 0.0〜1.0。受けたぶんの割合（かすり傷で真っ赤にしないため）。
    float intensity{ 0.f };
};
