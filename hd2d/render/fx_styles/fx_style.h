/*!
 * @file fx_style.h
 * @brief 属性ごとの攻撃の見た目（スタイル）の登録表。系統ごとのファイルが自分の属性を登録する。
 *
 * （見た目）と §7（実装の手引き）。
 *
 * ## 仕組み
 * - 属性の名前（`ProjectionFx::attr`。小文字。一覧は `presentation/bridge/attack_fx_attr_names.h`）から
 *   `FxStyle` を引く。**名前が 1 字でも違うと当たらない**（`inertial` と `inertia` の食い違いがあった）。
 *   登録に無い名前・綴りの違う名前が 0 であることは `--attack-fx-check` が数える。
 * - `FxStyle` は **段階ごとの関数**と、基準の色を持つ。関数が `nullptr` の段階は汎用の見た目
 *   （`fx_generic_*`。属性の色の玉と爆発）になる。**後から見た目を作る人は、自分の系統のファイルの中だけで
 *   関数を書き、`FxStyle` に結ぶ。**他の系統のファイルも `voxel_fx.cpp` も触らなくてよい。
 * - 関数は**状態を持たない**。同じ（撃ち方、経過秒）には同じ絵を返す（`voxel_fx.h` の方針）。乱数は
 *   `fx_rand(spec.seed, …)`。
 *
 * ## 系統とファイル
 * | 系統 | ファイル | 登録関数 |
 * |---|---|---|
 * | 液体（§5.1） | `fx_liquid.cpp` | `fx_register_liquid` |
 * | 稲妻・光線（§5.2） | `fx_lightning.cpp` | `fx_register_lightning` |
 * | その他のダメージの属性（§5.3） | `fx_elemental.cpp` | `fx_register_elemental` |
 * | 呪い・心・状態変化・地形・特殊（§5.4〜5.8） | `fx_spirit.cpp` | `fx_register_spirit` |
 * | Frox にあって変愚に無い属性（`fc_`。§8.3） | `fx_frox.cpp` | `fx_register_frox` |
 * | 幻想蛮怒にあって変愚に無い属性（`gb_`。§8.3） | `fx_gensoband.cpp` | `fx_register_gensoband` |
 * | Sil-Q にあって変愚に無い属性（`sq_`。§8.3） | `fx_silq.cpp` | `fx_register_silq` |
 */
#pragma once

#include "render/voxel_fx.h"

#include <deque>
#include <string>
#include <vector>

namespace hd2d {

//! 段階の関数。`ctx.spec`・`ctx.params`・`ctx.t`・`ctx.fly`・`ctx.te` を読み、`out` に**足す**。
using FxEmitFn = void (*)(const FxStyleCtx &ctx, FxFrame &out);
//! 段階の長さ（秒）。対応する `FxEmitFn` を呼ぶ長さを決める（短いと打ち切られ、長いと空のまま待つ）。
using FxSecondsFn = float (*)(const FxSpec &spec, const VoxelFxParams &params);
//! 属性の色の組を作り直す（汎用の玉と爆発の色が欲しいときの上書き）。`nullptr` なら基準の色から作る。
struct FxStyle;
using FxPaletteFn = FxPalette (*)(const FxStyle &style);

/*!
 * @brief 属性 1 つの見た目。
 * @details 段階と、それを呼ぶ形の対応:
 * | 段階 | 呼ばれる形 | `ctx.te` の意味 | 長さ |
 * |---|---|---|---|
 * | `flight` | ボルト・ボール（`Orb`）の玉が飛んでいる間（`t < fly`） | — | `fx_fly_seconds` |
 * | `impact` | 同じく着弾のあと | 着弾からの秒 | `impact_seconds`（既定は汎用の爆発の長さ） |
 * | `beam` | ビーム | 撃ってからの秒 | `beam_seconds` |
 * | `breath` | ブレス | 同上 | `breath_seconds` |
 * | `burst` | 直接（`Jump`） | 同上 | `burst_seconds` |
 * | `glow` | 飛ばない魔法・視界内の全員・足元だけ（`spec.glow_mode` で見分ける） | 同上 | `glow_seconds` |
 *
 * `spec.explode`（ボールか）、`spec.cells`（効果範囲のマスと、中心からの距離 `ring`）、`spec.route`（経路）、
 * `spec.centre_gx/gy`（着弾点または対象）、`spec.src_gx/gy`（撃った者）が使える。
 * 画面の揺れ・対象の見た目の変化・光の波は `FxFrame` の `shake`・`entity_mods`・`screen_waves` に足す
 * （`fx_parts.h`・`fx_entity_mod.h`）。
 */
struct FxStyle {
    //! 属性の名前（小文字）。
    const char *attr{ "" };
    //! 系統の名前（登録した関数が入れる）。
    const char *family{ "" };
    //! 基準の色と煙の濃さ・自己発光の倍率（汎用の玉と爆発の色になる）。
    FxColour base{ 1.f, 1.f, 1.f };
    float smoke{ 0.2f };
    float emissive{ 1.f };
    FxPaletteFn palette{ nullptr };

    FxEmitFn flight{ nullptr };
    FxEmitFn impact{ nullptr };
    FxSecondsFn impact_seconds{ nullptr };
    FxEmitFn beam{ nullptr };
    FxSecondsFn beam_seconds{ nullptr };
    FxEmitFn breath{ nullptr };
    FxSecondsFn breath_seconds{ nullptr };
    FxEmitFn burst{ nullptr };
    FxSecondsFn burst_seconds{ nullptr };
    FxEmitFn glow{ nullptr };
    FxSecondsFn glow_seconds{ nullptr };
};

//! 登録表。系統ごとの登録関数が `add()` で属性を足す。
class FxStyleRegistry {
public:
    //! 属性を足して返す（返した参照に関数を結ぶ）。同じ名前を 2 度足すと検査が落ちる。
    FxStyle &add(const char *attr, const FxColour &base, float smoke, float emissive);
    //! 名前（小文字）で引く。無ければ `nullptr`。
    FxStyle *find(const std::string &attr);
    const FxStyle *find(const std::string &attr) const;
    //! 以降に足す属性の系統の名前。
    void set_family(const char *family) { this->family_ = family; }
    //! 登録した順の一覧。
    const std::deque<FxStyle> &all() const { return this->styles_; }
    //! 同じ名前を 2 度足した数（検査用。0 のはず）。
    int duplicate_count() const { return this->duplicates_; }

private:
    std::deque<FxStyle> styles_; //!< 参照を保つため deque
    const char *family_{ "" };
    int duplicates_{ 0 };
};

/*!
 * @name 系統ごとの登録関数（各 `fx_*.cpp` が定義する）
 * @{
 */
void fx_register_liquid(FxStyleRegistry &registry);
void fx_register_lightning(FxStyleRegistry &registry);
void fx_register_elemental(FxStyleRegistry &registry);
void fx_register_spirit(FxStyleRegistry &registry);
//! 変愚に無い属性の見た目：Frox（`fc_` で始まる名前）と幻想蛮怒（`gb_` で始まる名前）。
void fx_register_frox(FxStyleRegistry &registry);
void fx_register_gensoband(FxStyleRegistry &registry);
//! Sil-Q にあって変愚に無い属性の見た目（`sq_` で始まる名前）。
void fx_register_silq(FxStyleRegistry &registry);
/*! @} */

//! 全系統を登録した表（初回の呼び出しで作る。以降は変わらない）。
const FxStyleRegistry &fx_style_registry();

//! 名前（大文字小文字は問わない）から引く。登録が無ければ `nullptr`。
const FxStyle *fx_find_style(const std::string &attr);

} // namespace hd2d
