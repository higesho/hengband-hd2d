/*!
 * @file attack_fx_recorder.h
 * @brief 攻撃の視覚効果の記録の置き場
 * @details
 * 原作へ組み立てのたびに書き足す**記録の呼び出し**（`tools/targeting/overlay.py`）が、
 * `native_targeting_api.h` の `hb_attack_fx_*` を通してここへ積む。画面側へ運ぶ形
 * （`frame/attack_fx.h`）への変換は `presentation_bridge.cpp` の `fill_attack_fx` が
 * `capture` のたびに行い、**汲んだら空になる**（`CombatFeedback::take_events` と同じ考え方）。
 *
 * ## 原作の状態を変えない
 * ここは受け取った数を積むだけで、原作の変数・乱数・画面には触れない。
 *
 * ## ヘッダだけで完結させた理由
 * 呼び出しの定義は全コアに入っている `native_targeting_runtime.cpp` に置き、積む先は
 * このヘッダの inline 関数内の静的変数にした。新しい .cpp を足さないので、
 * Windows の vcxproj・Linux・Android の組み立ての一覧は変えなくてよい。
 *
 * ## 属性と旗は番号のまま持つ
 * `AttributeType` や `PROJECT_*` は変愚系にしか無い型なので、ここ（全コア共通）は
 * 番号を持つだけにし、名前・形・旗への変換は変愚系だけが組む `presentation_bridge.cpp` で行う。
 */
#pragma once

#include "frame/attack_fx.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace attack_fx {

//! 溜める件数と 1 件あたりのマス数の上限（溜まりすぎたら古いほうから捨てる）。
constexpr size_t kMaxProjections = 64;
constexpr size_t kMaxMissiles = 64;
constexpr size_t kMaxCells = 512;

//! `project()` 1 回の記録（番号のまま）。
struct RawProjection {
    uint32_t seq{ 0 };
    int src_idx{ 0 }; //!< 0 = プレイヤ、正 = モンスター、負 = それ以外
    int src_y{ 0 };
    int src_x{ 0 };
    int impact_y{ 0 };
    int impact_x{ 0 };
    int attr_id{ 0 }; //!< `AttributeType` の番号
    int rad{ 0 };
    uint32_t flags{ 0 }; //!< 原作の `PROJECT_*`（`project()` が加工した後の値）
    bool blind{ false };
    bool breath{ false }; //!< 経路が取れて、ブレスとして広がったか
    std::vector<FxCell> path; //!< 始点の次のマスから着弾点まで
    std::vector<FxCell> area; //!< 効果の及んだマスと中心からの距離
};

//! 射撃・投擲 1 回の記録（番号のまま）。
struct RawMissile {
    uint32_t seq{ 0 };
    bool thrown{ false }; //!< 投擲か（偽なら弓などの射撃）
    bool boomerang{ false }; //!< 投擲のうち、戻ってくる物
    int item_id{ 0 };
    int tval{ -1 }; //!< 射撃の矢弾の `ItemKindType` の番号
    int brand_id{ -1 }; //!< 属性付きの矢弾の `AttributeType` の番号。無ければ -1
    int src_y{ 0 };
    int src_x{ 0 };
    std::vector<FxCell> path;
    std::vector<FxCell> return_path;
    bool hit{ false };
};

class Recorder {
public:
    static Recorder &instance()
    {
        static Recorder recorder;
        return recorder;
    }

    Recorder(const Recorder &) = delete;
    Recorder &operator=(const Recorder &) = delete;

    void add_projection(RawProjection &&projection)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        projection.seq = ++this->seq_;
        if (this->projections_.size() >= kMaxProjections) {
            this->projections_.erase(this->projections_.begin());
        }
        this->projections_.push_back(std::move(projection));
    }

    //! 射撃の 1 歩（`cur_dis == 1` で新しい 1 発を始める）。
    void shot_step(int cur_dis, int x, int y, int prev_y, int prev_x, int item_id, int tval, int brand_id)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (cur_dis <= 1 || !this->open_) {
            this->begin_missile(false, false, item_id, prev_y, prev_x);
            this->missiles_.back().tval = tval;
            this->missiles_.back().brand_id = brand_id;
        }
        this->push_cell(this->missiles_.back().path, y, x);
    }

    //! 投擲の 1 歩（`cur_dis == 1` で新しい 1 発を始める）。
    void throw_step(int cur_dis, int x, int y, int prev_y, int prev_x, int item_id, bool returns)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (cur_dis <= 1 || !this->open_) {
            this->begin_missile(true, returns, item_id, prev_y, prev_x);
        }
        this->push_cell(this->missiles_.back().path, y, x);
    }

    //! 直前に始めた 1 発が相手に当たった。
    void mark_hit()
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (this->open_) {
            this->missiles_.back().hit = true;
        }
    }

    /*!
     * @brief 直前に始めた投擲が戻ってくる。
     * @details 戻りの経路は原作が描く順（着弾したマスから、始点の隣の次のマスまで）に、
     * 最後に始点（プレイヤ）を足す。
     */
    void throw_returns()
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        if (!this->open_ || !this->missiles_.back().thrown) {
            return;
        }
        auto &missile = this->missiles_.back();
        missile.boomerang = true;
        missile.return_path.clear();
        for (size_t i = missile.path.size(); i-- > 1;) {
            missile.return_path.push_back(missile.path[i]);
        }
        FxCell origin;
        origin.y = static_cast<int16_t>(missile.src_y);
        origin.x = static_cast<int16_t>(missile.src_x);
        missile.return_path.push_back(origin);
    }

    //! 溜まっている分を持ち出して空にする（`capture` が呼ぶ）。
    void take(std::vector<RawProjection> &projections, std::vector<RawMissile> &missiles)
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        projections.swap(this->projections_);
        missiles.swap(this->missiles_);
        this->projections_.clear();
        this->missiles_.clear();
        this->open_ = false;
    }

private:
    Recorder() = default;

    void begin_missile(bool thrown, bool boomerang, int item_id, int src_y, int src_x)
    {
        if (this->missiles_.size() >= kMaxMissiles) {
            this->missiles_.erase(this->missiles_.begin());
        }
        RawMissile missile;
        missile.seq = ++this->seq_;
        missile.thrown = thrown;
        missile.boomerang = boomerang;
        missile.item_id = item_id;
        missile.src_y = src_y;
        missile.src_x = src_x;
        this->missiles_.push_back(std::move(missile));
        this->open_ = true;
    }

    static void push_cell(std::vector<FxCell> &cells, int y, int x)
    {
        if (cells.size() >= kMaxCells) {
            return;
        }
        FxCell cell;
        cell.y = static_cast<int16_t>(y);
        cell.x = static_cast<int16_t>(x);
        cells.push_back(cell);
    }

    std::mutex mutex_;
    uint32_t seq_{ 0 };
    //! 最後の 1 件が、まだ飛んでいる（`mark_hit` などを受け付ける）か。
    bool open_{ false };
    std::vector<RawProjection> projections_;
    std::vector<RawMissile> missiles_;
};

} // namespace attack_fx
