/*!
 * @file named_attack_fx.h
 * @brief 攻撃の視覚効果の記録の置き場（属性を**名前**で積む版。変愚系以外のコア用）
 * @details
 * `attack_fx_recorder.h` は変愚系の `AttributeType` の番号で積む作りで、変愚と短愚だけが使う（このヘッダは触らない）。
 * 変愚系以外のコア（Angband・幻想蛮怒・Frox・Sil-Q・Umoria・Rogue 系）は属性の番号体系がそれぞれ違うので、
 * **各コアのアダプタが属性の名前（`fire`・`pois`・`old_sleep` など。小文字。表は
 * `presentation/bridge/attack_fx_attr_names.h`）まで直してから**ここへ積む。ここは画面へ運ぶ形
 * （`frame/attack_fx.h` の `ProjectionFx`・`MissileFx`）をそのまま持つだけで、原作の型を何も知らない。
 *
 * ## 使い方（コアのアダプタ側）
 * 1. 原作へ書き足した**観測の呼び出し**（`__attribute__((weak))` の既定の関数と `//AN:` のような目印をセットにした
 *    1 行。ゲームのルールは変えない）が、アダプタの強い定義を呼ぶ。
 * 2. アダプタは投射 1 回ごとに `add_projection()` を呼ぶ（形・属性の名前・半径・始点・着弾点・経路・範囲・旗を詰めた
 *    `ProjectionFx` をそのまま渡す。`seq` はここで振る）。
 * 3. 射撃・投擲は 1 歩ごとに記録する：1 発目の 1 歩目で `begin_missile()`、以降の歩ごとに `missile_step()`、
 *    相手に当たったら `missile_hit()`。**歩ごとは仮置き**で、1 発の終わり（当たったとき・次の `begin_missile()`・
 *    `end_missile()`、または動きの終わり）で確定する。
 * 4. フレームを組むとき `take(projections, missiles, settled)` を呼び、返ったものを `GameFrame::projections` /
 *    `missiles` へ移す。**汲んだら空になる**（`CombatFeedback::take_events` と同じ考え方）。
 *    `settled` はコアが「動きの終わり」＝命令待ちにいるかどうか。
 *
 * ## 「動きの終わりで確定」にした理由
 * フレームは入力待ちや `-more-` の最中にも組まれる（`capture` は 10ms ごとに回る）。飛んでいる最中の 1 発を
 * 途中で汲むと、1 発が経路の途中で割れて 2 本の矢になる。そこで、開いている 1 発は `settled` が真になるまで
 * （または `kMaxHeldTakes` 回の汲みを越えるまで）汲まずに持ち越す。投射（`add_projection`）は 1 回で完結するので
 * 仮置きは要らない。
 *
 * ## 変愚系との併用はしない
 * 通し番号 `seq` は両方の記録の置き場がそれぞれ 1 から振る。同じコアで両方を使うと画面側の重複除去が衝突する。
 *
 * ## ヘッダだけで完結させた理由
 * 新しい .cpp を足さないので、Windows の vcxproj・Linux・Android の組み立ての一覧は変えなくてよい
 * （`attack_fx_recorder.h` と同じ）。
 */
#pragma once

#include "frame/attack_fx.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace attack_fx {

//! 溜める件数と 1 件あたりのマス数の上限（溜まりすぎたら古いほうから捨てる）。
constexpr size_t kNamedMaxProjections = 64;
constexpr size_t kNamedMaxMissiles = 64;
constexpr size_t kNamedMaxCells = 512;
//! 開いている 1 発を、動きの終わりを待たずに確定するまでの汲みの回数（命令待ちが来ない場合の保険）。
constexpr int kMaxHeldTakes = 40;

class NamedRecorder {
public:
    static NamedRecorder &instance()
    {
        static NamedRecorder recorder;
        return recorder;
    }

    NamedRecorder(const NamedRecorder &) = delete;
    NamedRecorder &operator=(const NamedRecorder &) = delete;

    //! 投射 1 回（原作の `project()` 1 回）。`seq` はここで振る。
    void add_projection(ProjectionFx &&projection)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        projection.seq = ++this->seq_;
        if (this->projections_.size() >= kNamedMaxProjections) {
            this->projections_.erase(this->projections_.begin());
        }
        this->projections_.push_back(std::move(projection));
    }

    /*!
     * @brief 射撃・投擲 1 発を始める（開いている前の 1 発は閉じる）。
     * @param item_id 飛ばした物のコアの基本アイテム番号（`MapCellView::object_id` と同じ番号体系）。0 なら不明。
     * @param brand 属性付きの矢弾なら属性の名前。**識別済みのものだけ**を渡すこと（未識別を見せると原作より情報が増える）。
     */
    void begin_missile(MissileKind kind, AttackFxOrigin origin, uint16_t item_id, const std::string &brand, int src_y, int src_x)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->close_open_locked();
        MissileFx missile;
        missile.seq = ++this->seq_;
        missile.origin = origin;
        missile.kind = kind;
        missile.item_id = item_id;
        missile.brand = brand;
        missile.src_y = static_cast<int16_t>(src_y);
        missile.src_x = static_cast<int16_t>(src_x);
        this->open_ = std::move(missile);
        this->has_open_ = true;
        this->held_ = 0;
    }

    //! 開いている 1 発の 1 歩（始点の次のマスから止まったマスまで）。開いていなければ何もしない。
    void missile_step(int y, int x)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (!this->has_open_ || this->open_.path.size() >= kNamedMaxCells) {
            return;
        }
        FxCell cell;
        cell.y = static_cast<int16_t>(y);
        cell.x = static_cast<int16_t>(x);
        this->open_.path.push_back(cell);
    }

    /*!
     * @brief 開いている 1 発が相手に当たった。
     * @details **当たった時点でその 1 発は終わり**（確定する）。当たった直後に `-more-` などの問いが出ると、
     * 命令待ちが来るまで確定を待つ作りでは、矢の飛行が問いの後まで画面に届かない。貫通する矢がその先へ
     * 飛ぶ分は、次の `missile_step()`（新しい 1 発）として記録される。
     */
    void missile_hit()
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->has_open_) {
            this->open_.hit = true;
            this->close_open_locked();
        }
    }

    /*!
     * @brief 開いている 1 発が戻ってくる（ブーメランなど）。
     * @details 戻りの経路は着弾したマスから始点の隣の次のマスまで、最後に始点（撃った者）を足す
     * （`attack_fx::Recorder::throw_returns` と同じ）。
     */
    void missile_returns()
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (!this->has_open_) {
            return;
        }
        this->open_.kind = MissileKind::Boomerang;
        this->open_.return_path.clear();
        for (size_t i = this->open_.path.size(); i-- > 1;) {
            this->open_.return_path.push_back(this->open_.path[i]);
        }
        FxCell origin;
        origin.y = this->open_.src_y;
        origin.x = this->open_.src_x;
        this->open_.return_path.push_back(origin);
    }

    //! 開いている 1 発を閉じる（確定させる）。コアが 1 発の終わりを知っているときに呼ぶ。
    void end_missile()
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        this->close_open_locked();
    }

    //! 開いている 1 発があるか。
    bool has_open() const
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        return this->has_open_;
    }

    //! 汲めるものがあるか（`settled` が真なら開いている 1 発も数える）。フレームを組み直すかの判定に使う。
    bool has_ready(bool settled) const
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        return !this->projections_.empty() || !this->missiles_.empty() || (this->has_open_ && (settled || (this->held_ >= kMaxHeldTakes)));
    }

    /*!
     * @brief 溜まっている分を持ち出して空にする（フレームを組むたびに呼ぶ）。
     * @param settled コアが動きの終わり（命令待ち）にいるか。偽のあいだ、開いている 1 発は汲まずに持ち越す。
     */
    void take(std::vector<ProjectionFx> &projections, std::vector<MissileFx> &missiles, bool settled)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->has_open_) {
            if (settled || (++this->held_ >= kMaxHeldTakes)) {
                this->close_open_locked();
            }
        }
        projections.swap(this->projections_);
        missiles.swap(this->missiles_);
        this->projections_.clear();
        this->missiles_.clear();
    }

private:
    NamedRecorder() = default;

    void close_open_locked()
    {
        if (!this->has_open_) {
            return;
        }
        this->has_open_ = false;
        this->held_ = 0;
        if (this->open_.path.empty()) {
            return; // 飛んだ先が無い（画面側も描く経路が無い）
        }
        if (this->missiles_.size() >= kNamedMaxMissiles) {
            this->missiles_.erase(this->missiles_.begin());
        }
        this->missiles_.push_back(std::move(this->open_));
        this->open_ = MissileFx{};
    }

    mutable std::mutex mutex_;
    uint32_t seq_{ 0 };
    bool has_open_{ false };
    int held_{ 0 };
    MissileFx open_;
    std::vector<ProjectionFx> projections_;
    std::vector<MissileFx> missiles_;
};

} // namespace attack_fx
