/*!
 * @file nh_select.h
 * @brief NetHack のキャラクタ作成の窓の状態と操作（職業・種族・属性・性別の選びと、合わない組み合わせの扱い）。描画にも通信にも触れない。
 *
 * ## 何のためのものか
 * 原作の Windows の GUI 版 `win/win32/mhdlg.c`（`IDD_PLAYER_SELECTOR`。`plselAdjustSelections`・
 * `plselDrawItem`）の選び方を写す。コアから届く `select`（§3.1）の `roles`・`races`・`genders`・`aligns`・`ok`（有効な組み合わせの表）・
 * `name`・`pick` を持つ。
 *
 * ## 原作と同じ規則
 * - 今の職業に合わない種族は一覧の中で**赤い字**（選べない）。今の職業と種族に合わない属性・性別は**灰色**（無効）。
 * - 職業を選び直すと、種族・性別・属性を、合う物に自動で直す（今の選びが合うなら変えない。合わなければ一覧の先頭の合う物）。
 * - `ok` は `[[職業, 種族, 性別, 属性], …]`（原作の `ok_role`・`ok_race`・`ok_gend`・`ok_align` がすべて真になる組。番号は各一覧の `i`）。
 *   表が空のときは、すべての組み合わせを有効とする。
 * - `pick` は先に決まっている選び（-1 は未定）。未定の所は、決まっている所と矛盾しない先頭の組から埋める。
 */
#pragma once

#include <array>
#include <string>
#include <vector>

namespace hd2d::nethack {

struct NhSelOption {
    int index{ 0 }; //!< 原作の番号（返事の `role` などにそのまま入れる）
    std::string name;
    std::string name_f; //!< 女性のときの職業名（無ければ空）
    std::string adj;    //!< 種族の形容詞（原作の `adj`）
};

//! 窓の中のどの部品にキーボードの焦点があるか。
enum class NhSelFocus { Roles, Races, Align, Gender, Play, Random, Quit };

class NhSelect {
public:
    //! `select` の知らせ（JSON 1 通の字）を取り込む。壊れている・`select` でないときは偽。
    bool load(const std::string &payload);

    int request() const { return this->request_; }
    //! `pick` が職業・種族・性別・属性のすべてを決めていて、その組が有効か。真のとき原作の GUI 版は窓を出さずに始める
    //! （設定ファイルで全部決まっているとき。`mhdlg.c` の `plselRandomize`）。
    bool pick_complete() const { return this->pick_complete_; }
    const std::vector<NhSelOption> &roles() const { return this->roles_; }
    const std::vector<NhSelOption> &races() const { return this->races_; }
    const std::vector<NhSelOption> &genders() const { return this->genders_; }
    const std::vector<NhSelOption> &aligns() const { return this->aligns_; }
    const std::string &name() const { return this->name_; }

    //! いまの選び（各一覧の `index`）。
    int role() const { return this->sel_[0]; }
    int race() const { return this->sel_[1]; }
    int gender() const { return this->sel_[2]; }
    int align() const { return this->sel_[3]; }

    //! 今の職業に合う種族か（合わなければ赤字で選べない）。
    bool race_ok(int race) const;
    //! 今の職業と種族に合う性別・属性か（合わなければ灰色で選べない）。
    bool gender_ok(int gender) const;
    bool align_ok(int align) const;

    //! 一覧の項目を選ぶ（合わない種族・性別・属性は何もしない）。選べたら真。
    bool choose_role(int role);
    bool choose_race(int race);
    bool choose_gender(int gender);
    bool choose_align(int align);

    //! 職業の表示名（女性が選ばれていて `name_f` があればそれ）。
    std::string role_label(const NhSelOption &role) const;

    NhSelFocus focus() const { return this->focus_; }
    void set_focus(NhSelFocus focus) { this->focus_ = focus; }
    //! Tab（`step` が 1）・Shift+Tab（-1）で焦点を進める。
    void move_focus(int step);
    //! 矢印の上下（`step` が 1 で下）。焦点のある一覧・ラジオボタンの次の選べる項目へ。
    void move_choice(int step);

    //! 開始・おまかせ・やめるの返事の JSON の字。
    std::string play_json() const;
    std::string random_json() const;
    std::string quit_json() const;

private:
    bool has_combo(int role, int race, int gender, int align) const;
    void adjust_after_role();
    void adjust_after_race();
    void adjust_align();
    int first_race_for_role(int role) const;

    int request_{ 0 };
    std::vector<NhSelOption> roles_;
    std::vector<NhSelOption> races_;
    std::vector<NhSelOption> genders_;
    std::vector<NhSelOption> aligns_;
    std::vector<std::array<int, 4>> combos_;
    std::string name_;
    bool pick_complete_{ false };
    std::array<int, 4> sel_{ -1, -1, -1, -1 };
    NhSelFocus focus_{ NhSelFocus::Roles };
};

} // namespace hd2d::nethack
