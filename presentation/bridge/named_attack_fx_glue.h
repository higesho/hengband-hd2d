/*!
 * @file named_attack_fx_glue.h
 * @brief 旧い C のコア（幻想蛮怒・Frox・Sil-Q）の魔法・矢弾の効果の記録を、共通の記録の置き場へ積む橋
 * @details
 * 各コアのアダプタ（C）は、原作に足した観測の呼び出し（`tools/targeting/attack_fx_overlay.py`）から呼ばれ、
 * 原作の型（`GF_xxx`・`PROJECT_xxx`・物の構造体）を読んで、**属性のコア名（`GF_` を除いた小文字）・形・範囲・経路・旗**を
 * 整数と文字列に直す。ここはその値を受けて、
 *   1. 属性のコア名を見た目の名前に直し（`attack_fx_core_attr_aliases.h` の `*_look_name`）、
 *   2. `attack_fx::NamedRecorder`（`named_attack_fx.h`）へ積む。
 * ヘッダだけで完結（新しい .cpp は無い。Windows の vcxproj・Linux・Android の一覧は変えない）。UTF-8 BOM 付き。
 *
 * 整数の並びは `presentation/frame/attack_fx.h` と同じ:
 *  - origin: 0=プレイヤ 1=モンスター 2=それ以外
 *  - shape: 0=ボルト 1=ビーム 2=ボール 3=ブレス 4=直接
 *  - kind: 0=矢 1=クロスボウの矢 2=スリングの弾 3=投擲
 *  - flags: 1=隠す 2=盲目 4=反射 8=視界内の全員
 *  - path は (y,x) の並び、area は (中心からの距離,y,x) の並び
 */
#pragma once

#include "bridge/attack_fx_core_attr_aliases.h"
#include "bridge/named_attack_fx.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

namespace attack_fx {
namespace glue {

//! 記録した件数の累計（フレームを組むかの判定に使う。汲んでも減らない）。
inline std::atomic<std::uint32_t> &total_counter()
{
    static std::atomic<std::uint32_t> counter{ 0 };
    return counter;
}

//! 見た目の名前を引く関数（`frox_look_name` など）。
using LookFn = const char *(*)(const char *);

//! 投射 1 回を積む。`core_attr` はコア名（`GF_` を除いた小文字）。空なら汎用の見た目。
inline void projection(LookFn look, int origin, int shape, const char *core_attr, int rad, int src_y, int src_x, int impact_y,
    int impact_x, int flags, const int *path, int path_n, const int *area, int area_n)
{
    ProjectionFx fx;
    fx.origin = static_cast<AttackFxOrigin>(origin);
    fx.shape = static_cast<ProjectionShape>(shape);
    if ((core_attr != nullptr) && (core_attr[0] != '\0')) {
        const char *name = (look != nullptr) ? look(core_attr) : core_attr;
        fx.attr = (name != nullptr) ? name : "";
    }
    fx.rad = static_cast<std::uint8_t>(std::max(0, std::min(rad, 255)));
    fx.src_y = static_cast<std::int16_t>(src_y);
    fx.src_x = static_cast<std::int16_t>(src_x);
    fx.impact_y = static_cast<std::int16_t>(impact_y);
    fx.impact_x = static_cast<std::int16_t>(impact_x);
    fx.flags = static_cast<std::uint16_t>(flags);
    for (int i = 0; (path != nullptr) && (i < path_n) && (fx.path.size() < kNamedMaxCells); ++i) {
        FxCell cell;
        cell.y = static_cast<std::int16_t>(path[i * 2]);
        cell.x = static_cast<std::int16_t>(path[i * 2 + 1]);
        fx.path.push_back(cell);
    }
    for (int i = 0; (area != nullptr) && (i < area_n) && (fx.area.size() < kNamedMaxCells); ++i) {
        FxCell cell;
        cell.dist = static_cast<std::uint8_t>(std::max(0, std::min(area[i * 3], 255)));
        cell.y = static_cast<std::int16_t>(area[i * 3 + 1]);
        cell.x = static_cast<std::int16_t>(area[i * 3 + 2]);
        fx.area.push_back(cell);
    }
    NamedRecorder::instance().add_projection(std::move(fx));
    total_counter().fetch_add(1);
}

/*!
 * @brief 射撃・投擲の 1 歩を積む。`first` が真か、開いている 1 発が無ければ新しい 1 発を始める。
 * @param item_id 飛ばした物の基本アイテムの番号（`MapCellView::object_id` と同じ。不明なら 0）
 * @param brand 識別済みの属性の名前（無ければ空。画面側の登録表の名前）
 */
inline void missile_step(int first, int kind, int item_id, const char *brand, int src_y, int src_x, int y, int x)
{
    auto &recorder = NamedRecorder::instance();
    if ((first != 0) || !recorder.has_open()) {
        recorder.begin_missile(static_cast<MissileKind>(kind), AttackFxOrigin::Player,
            static_cast<std::uint16_t>(item_id < 0 ? 0 : item_id), (brand != nullptr) ? std::string(brand) : std::string(), src_y,
            src_x);
    }
    recorder.missile_step(y, x);
    total_counter().fetch_add(1);
}

//! 開いている 1 発が相手に当たった。
inline void missile_hit()
{
    NamedRecorder::instance().missile_hit();
    total_counter().fetch_add(1);
}

//! 開いている 1 発が戻ってくる（ブーメラン）。
inline void missile_returns()
{
    NamedRecorder::instance().missile_returns();
    total_counter().fetch_add(1);
}

//! 溜まっている分を `GameFrame` へ汲む（汲んだら空になる）。`settled` はコアが命令待ちにいるか。
inline void take_into(GameFrame &frame, bool settled)
{
    NamedRecorder::instance().take(frame.projections, frame.missiles, settled);
}

//! 汲めるものがあるか（フレームを組み直すかの判定に使う）。
inline bool ready(bool settled)
{
    return NamedRecorder::instance().has_ready(settled);
}

} // namespace glue
} // namespace attack_fx
