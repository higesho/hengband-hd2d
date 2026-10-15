#pragma once
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace hd2d {
struct MenuTab {
    std::string id;
    std::string label;
    std::vector<std::string> items;
};

// Shared, renderer-independent tab selection and per-tab position memory.
class MenuTabs {
public:
    void reset(std::vector<MenuTab> tabs)
    {
        const bool had_previous = !tabs_.empty();
        const std::string previous = tabs_.empty() ? std::string() : tabs_[index_].id;
        tabs_ = std::move(tabs);
        index_ = 0;
        for (size_t i = 0; i < tabs_.size(); ++i) {
            if (had_previous && tabs_[i].id == previous) index_ = static_cast<int>(i);
        }
        for (auto it = positions_.begin(); it != positions_.end();) {
            if (std::none_of(tabs_.begin(), tabs_.end(), [&](const auto &tab) { return tab.id == it->first; })) it = positions_.erase(it);
            else ++it;
        }
    }
    const std::vector<MenuTab> &tabs() const { return tabs_; }
    int index() const { return index_; }
    void select(int index) { index_ = std::clamp(index, 0, std::max(0, static_cast<int>(tabs_.size()) - 1)); }
    void step(int direction)
    {
        if (!tabs_.empty()) select((index_ + (direction < 0 ? static_cast<int>(tabs_.size()) - 1 : 1)) % static_cast<int>(tabs_.size()));
    }
    int cursor() const
    {
        if (tabs_.empty()) return 0;
        const auto &tab = tabs_[index_];
        const auto found = positions_.find(tab.id);
        if (found == positions_.end()) return 0;
        auto item = std::find(tab.items.begin(), tab.items.end(), found->second.item);
        return item == tab.items.end() ? std::clamp(found->second.index,0,std::max(0,int(tab.items.size())-1)) : static_cast<int>(item - tab.items.begin());
    }
    int scroll() const
    {
        if (tabs_.empty()) return 0;
        const auto found = positions_.find(tabs_[index_].id);
        return found == positions_.end() ? 0 : std::max(0, found->second.scroll);
    }
    void remember(int cursor, int scroll)
    {
        if (tabs_.empty()) return;
        const auto &tab = tabs_[index_];
        if (cursor >= 0 && cursor < static_cast<int>(tab.items.size())) positions_[tab.id] = {tab.items[cursor], std::max(0, scroll),cursor};
    }
    void clear_positions()
    {
        positions_.clear();
        index_ = 0;
    }
private:
    struct Position { std::string item; int scroll{},index{}; };
    std::vector<MenuTab> tabs_;
    std::unordered_map<std::string, Position> positions_;
    int index_{};
};
}
