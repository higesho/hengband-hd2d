/*!
 * @file attack_fx.h
 * @brief 攻撃の視覚効果（ボクセルの投射物と爆発）を画面へ運ぶ形
 *
 * ## 何を運ぶか
 * - `ProjectionFx`：魔法などの投射 1 回（原作の `project()` 1 回）。形・属性・経路・爆発の範囲。
 * - `MissileFx`：射撃・投擲 1 回。飛ばした物と経路。
 *
 * どちらも `combat_fx` と同じく**汲んだら消える**（コアが記録し、`capture` で汲んで空にする）。
 * `capture` は入力待ちの間も回るので、残す作りにすると同じ効果が何度も出る。
 *
 * ## 属性は名前で運ぶ
 * 属性の番号（`AttributeType`）はコアごとに違うので、**名前**（`fire`・`cold` など。小文字）で運ぶ。
 * 画面側は名前で見た目を引き、知らない名前は汎用の見た目に落ちる（`SoundEvent` と同じ考え方）。
 *
 * @note ここは**追加だけ**の変更なのでプロトコルの版は上げない（v1 §3.2）。
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

//! 誰が起こしたか。
enum class AttackFxOrigin : uint8_t {
    Player = 0,
    Monster = 1,
    Other = 2, //!< 罠・道具の暴発・地形など
};

//! 投射の形。
enum class ProjectionShape : uint8_t {
    Bolt = 0, //!< 玉が飛んで最初の相手で止まる（爆発しない）
    Beam = 1, //!< 経路の全部に効く線
    Ball = 2, //!< 玉が飛んで着弾で爆発（半径 `rad`）
    Breath = 3, //!< 撃った者を頂点にした扇形（玉は飛ばない）
    Jump = 4, //!< 経路を持たず、その場に直接起きる（爆発のルーン・流星群の 1 発など）
    Touch = 5, //!< 周囲・足元にだけ効く（地形を変える魔法など）
};

//! マス 1 つ（格子座標）。爆発の範囲のときは中心からの距離 `dist` も持つ。
struct FxCell {
    int16_t y{ 0 };
    int16_t x{ 0 };
    uint8_t dist{ 0 };
};

//! フラグ（ビット）。
enum ProjectionFxFlag : uint16_t {
    kProjHidden = 1u << 0, //!< 原作が画面に描かない投射（`PROJECT_HIDE`）。飛ばない魔法の効果に使う
    kProjPlayerBlind = 1u << 1, //!< プレイヤが盲目だった
    kProjReflected = 1u << 2, //!< 反射で跳ね返った弾
    kProjAllInSight = 1u << 3, //!< 視界内の全員にかかる魔法の 1 件（画面全体の効果にまとめる手がかり）
};

struct ProjectionFx {
    //! 通し番号（画面側の重複除去用。コアの起動から増え続ける）。
    uint32_t seq{ 0 };
    AttackFxOrigin origin{ AttackFxOrigin::Player };
    ProjectionShape shape{ ProjectionShape::Bolt };
    //! 属性の名前（小文字。`fire`・`cold`・`elec`・`missile`・`old_sleep` など）。
    std::string attr;
    //! ボール・ブレスの半径（マス）。
    uint8_t rad{ 0 };
    int16_t src_y{ 0 };
    int16_t src_x{ 0 };
    int16_t impact_y{ 0 };
    int16_t impact_x{ 0 };
    //! 始点の次のマスから着弾点までの経路（`dist` は使わない）。
    std::vector<FxCell> path;
    //! 爆発・ビーム・ブレスで効果の及んだマスと、中心からの距離（原作の `positions`）。
    std::vector<FxCell> area;
    uint16_t flags{ 0 };
};

//! 射撃・投擲の種類。
enum class MissileKind : uint8_t {
    Arrow = 0, //!< 弓の矢（少し弧を描いて飛ぶ）
    Bolt = 1, //!< クロスボウの矢（まっすぐ飛ぶ）
    Shot = 2, //!< スリングの弾
    Thrown = 3, //!< 投げた物（その物の板を飛ばす）
    Boomerang = 4, //!< 戻ってくる投擲
};

struct MissileFx {
    uint32_t seq{ 0 };
    AttackFxOrigin origin{ AttackFxOrigin::Player };
    MissileKind kind{ MissileKind::Arrow };
    //! 飛ばした物の基本アイテムの番号（コアの `bi_id`。`MapCellView::object_id` と同じ番号）。0 なら不明。
    uint16_t item_id{ 0 };
    //! 属性付きの矢弾（炎の矢など）なら属性の名前。無ければ空。
    std::string brand;
    int16_t src_y{ 0 };
    int16_t src_x{ 0 };
    //! 始点の次のマスから止まったマスまで。
    std::vector<FxCell> path;
    //! 戻ってくる投擲の戻りの経路（無ければ空）。
    std::vector<FxCell> return_path;
    //! 相手に当たったか。
    bool hit{ false };
};
