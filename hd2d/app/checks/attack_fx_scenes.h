/*!
 * @file attack_fx_scenes.h
 * @brief 攻撃の視覚効果の検査で流す「場面」の定義（系統ごとのファイルに分けてある）。
 *
 * `--attack-fx-check` は、ここの場面を全部 CPU で流して（落ちない・数が上限を超えない・最後に消える）確かめ、
 * `--fx-family=系統名` でその系統の場面の連続画像を書き出す。1 属性だけ見るなら
 * `--fx-attr=pois --fx-shape=ball`（場面の表に無い組み合わせも流せる）。
 *
 * | 系統 | ファイル | 関数 |
 * |---|---|---|
 * | 液体 | `attack_fx_scenes_liquid.cpp` | `fx_scenes_liquid()` |
 * | 稲妻・光線 | `attack_fx_scenes_lightning.cpp` | `fx_scenes_lightning()` |
 * | その他のダメージの属性 | `attack_fx_scenes_elemental.cpp` | `fx_scenes_elemental()` |
 * | 呪い・心・状態変化・地形・特殊 | `attack_fx_scenes_spirit.cpp` | `fx_scenes_spirit()` |
 *
 * 見た目を作る人は、自分の系統のファイルに**場面を足す**だけでよい（`attack_fx_checks.cpp` は触らない）。
 */
#pragma once

#include "frame/attack_fx.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hd2d {

//! 検査の場面 1 つ。ゲームがフレームに載せる `ProjectionFx` 1 件に相当する。
struct FxCheckScene {
    //! 出力の名前（例 `pois_ball`）。連続画像は `…_frames/<部屋>_<名前>_<番号>.png`。
    const char *name;
    //! 属性の名前（小文字）。
    const char *attr;
    ProjectionShape shape;
    //! ボール・ブレスの半径（マス）。
    int rad;
    //! `ProjectionFxFlag` のビット（`kProjHidden`・`kProjAllInSight` など）。
    std::uint16_t flags;
};

const std::vector<FxCheckScene> &fx_scenes_liquid();
const std::vector<FxCheckScene> &fx_scenes_lightning();
const std::vector<FxCheckScene> &fx_scenes_elemental();
const std::vector<FxCheckScene> &fx_scenes_spirit();

//! 4 つの系統をつないだ場面の表。
std::vector<FxCheckScene> fx_scenes_all();
//! 系統名（`liquid`・`lightning`・`elemental`・`spirit`）の場面。知らない名前は空。
std::vector<FxCheckScene> fx_scenes_of_family(const std::string &family);

//! 場面の形の名前（`bolt`・`beam`・`ball`・`breath`・`jump`・`touch`・`hidden`・`allsight`）から。`hidden`・`allsight` は旗つきの Bolt。
bool fx_parse_shape(const std::string &name, ProjectionShape &shape, std::uint16_t &flags);

} // namespace hd2d
