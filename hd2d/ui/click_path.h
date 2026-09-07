/*!
 * @file click_path.h
 * @brief クリックした所まで歩く（P8）。
 *
 * ## 何のためのものか
 * 隣のマスをクリックすれば 1 歩で済むが、離れた所をクリックしたときに何も起きないと
 * 「地図をクリックしても動かない UI」になる。`HengbandUi.exe` はこれを持っている
 * （Phase6 P6-B）ので、**同じことができる**ようにする（実装は引かない。必守制約 6）。
 *
 * ## 落とすと必ず壊れるもの
 * | # | 約束 | 落とすとどうなるか |
 * |---|---|---|
 * | 1 | **1 回に積むのは 1 歩だけ** | まとめて積むと、途中で戦闘が割り込んだときに残りが別の場面のコマンドとして走る |
 * | 2 | **積んだ 1 歩が実座標で確認できるまで次を積まない** | 壁にぶつかった・押し戻されたに気づけず、ずれたまま歩き続ける |
 * | 3 | **ずれたら捨てる**（壁バンプ・戦闘割込・別の画面） | 誤コマンドになる。設計書 必守制約 3 の親戚 |
 *
 * ## 経路の元
 * `GameFrame::minimap`（フロア全域・既知のマスだけ）。**未踏破は通れない**として扱う。
 * 見ていない所を通り抜ける経路を引くと、UI が知らないはずのことを知っていることになる。
 */
#pragma once

#include "frame/game_frame.h"
#include "frame/protocol_messages.h"

#include <utility>
#include <vector>

namespace hd2d {

class ClickPath {
public:
    /*!
     * @brief 目的地までの経路を引く。
     * @return 引けたら true（届かない・同じマスなら false で、状態は変わらない）。
     */
    bool begin(const MinimapSnapshot &map, int from_gx, int from_gy, int to_gx, int to_gy);
    void cancel();
    bool active() const { return !this->steps_.empty(); }
    //! 残りの歩数（画面に出す）。
    std::size_t remaining() const { return this->steps_.size(); }

    /*!
     * @brief 進める。**1 回に積むのは高々 1 歩。**
     * @details 積むべきでない場面（メニュー・プロンプト・数値入力）では捨てる。
     */
    void advance(const GameFrame &frame, presentation::InputEventsMessage &out);

private:
    //! 残りの通過点（世界座標。先頭が次に立つマス）。
    std::vector<std::pair<int, int>> steps_;
    //! 直前に積んだ 1 歩の行き先（実座標で確認するまで次を積まない）。
    bool issued_{ false };
    int expect_gx_{ 0 };
    int expect_gy_{ 0 };
};

//! そのマスは歩けるか（ミニマップの種別から。**未踏破は歩けない**）。
bool minimap_walkable(const MinimapSnapshot &map, int gx, int gy);

} // namespace hd2d
