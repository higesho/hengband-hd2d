/*!
 * @file nh_statwin.cpp
 * @brief `nh_statwin.h` の実装。
 */
#include "nethack/nh_statwin.h"

#include <algorithm>
#include <cstdlib>

namespace hd2d::nethack {

namespace {

//! `mhstatus.h` の `fieldorder1`・`fieldorder2`。
const int kOrder1[] = { kBlTitle, kBlStr, kBlDx, kBlCo, kBlIn, kBlWi, kBlCh, kBlAlign, kBlScore, -1 };
const int kOrder2[] = { kBlLevelDesc, kBlGold, kBlHp, kBlHpMax, kBlEne, kBlEneMax, kBlAc, kBlXp, kBlExp, kBlHd, kBlTime, kBlHunger, kBlCap, kBlCondition, -1 };

//! 先頭の数字の並びを整数にする（無ければ -1）。
int leading_number(const std::string &s)
{
    std::size_t i = 0;
    while ((i < s.size()) && !((s[i] >= '0') && (s[i] <= '9'))) {
        ++i;
    }
    if (i >= s.size()) {
        return -1;
    }
    return std::atoi(s.c_str() + i);
}

bool glued_to_previous(const std::string &text)
{
    return !text.empty() && ((text.front() == '(') || (text.front() == '/'));
}

} // namespace

const int *nh_status_order(int row)
{
    return (row == 0) ? kOrder1 : kOrder2;
}

NhStatusView nh_build_status(const NhModel &model)
{
    NhStatusView view;
    for (int row = 0; row < 2; ++row) {
        for (const int *p = nh_status_order(row); *p >= 0; ++p) {
            const int field = *p;
            if (field == kBlCondition) {
                for (const NhStatusCond &c : model.conditions()) {
                    view.rows[row].push_back(NhStatRun{ c.name, c.color, c.attr, true, kBlCondition });
                }
                continue;
            }
            const NhStatusField &value = model.status(field);
            if (!value.present) {
                continue;
            }
            std::string text = nh_status_display_text(field, value);
            if (text.empty()) {
                continue;
            }
            const bool glue = glued_to_previous(text) && !view.rows[row].empty();
            view.rows[row].push_back(NhStatRun{ std::move(text), value.color, value.attr, !glue, field });
        }
    }
    // 体力バー
    const NhStatusField &hp = model.status(kBlHp);
    if (hp.present) {
        int pct = hp.pct;
        if (pct < 0) {
            const int now = leading_number(hp.text);
            const int max = leading_number(model.status(kBlHpMax).text);
            if ((now >= 0) && (max > 0)) {
                pct = std::min(100, (now * 100) / max);
            }
        }
        view.bar_pct = std::clamp(pct, 0, 100);
        view.bar_color = (hp.color == kNoColor) ? 2 : hp.color; // 指定なしは緑
    }
    return view;
}

} // namespace hd2d::nethack
