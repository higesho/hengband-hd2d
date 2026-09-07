/*!
 * @file click_path.cpp
 * @brief `click_path.h` の実装。
 */
#include "ui/click_path.h"

#include "ui/ui_layout.h"

#include <algorithm>
#include <cstdlib>
#include <deque>

namespace hd2d {

bool minimap_walkable(const MinimapSnapshot &map, int gx, int gy)
{
    if (!map.valid()) {
        return false;
    }
    switch (map.at(gx, gy)) {
    case MinimapKind::Floor:
    case MinimapKind::Door:
    case MinimapKind::Stairs:
    case MinimapKind::Item:
    case MinimapKind::Player:
    /*
     * 歩ける地形の細別（FH-09）。**色を分けただけで役割は Floor のまま**——
     * 細別が来る前はどれも Floor で通れていたので、ここで塞ぐと
     * 「色を足したらクリック移動が効かなくなる」退行になる。
     * 通れるかの本当の判定はコアが持つ（違法な一歩はコアが断る）。
     */
    case MinimapKind::Water:
    case MinimapKind::Tree:
    case MinimapKind::Grass:
    case MinimapKind::Lava:
    case MinimapKind::Snow:
        return true;
    case MinimapKind::Monster:
        /*
         * **敵の上は通らない。**通れることにすると、道の途中に敵が居るだけで
         * クリック移動が攻撃コマンドに化ける。攻撃は利用者が明示的にやること。
         */
        return false;
    case MinimapKind::Unknown:
    case MinimapKind::Wall:
    //! 山は壁と同じ扱い（飛べる者は登れるが、クリック移動の経路には入れない）。
    case MinimapKind::Mountain:
    default:
        return false;
    }
}

void ClickPath::cancel()
{
    this->steps_.clear();
    this->issued_ = false;
}

bool ClickPath::begin(const MinimapSnapshot &map, int from_gx, int from_gy, int to_gx, int to_gy)
{
    if (!map.valid() || ((from_gx == to_gx) && (from_gy == to_gy))) {
        return false;
    }
    if (!minimap_walkable(map, to_gx, to_gy)) {
        return false; // 壁・未踏破。**行けない所を目的地にしない**
    }

    /*
     * 幅優先。8 方向（コアの移動と同じ）。フロアは高々 198×66 ＝ 13,068 マスなので、
     * 1 クリックにつき 1 回この規模を舐めるのは十分に安い。
     */
    const std::size_t count = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
    std::vector<int> came_from(count, -1);
    std::vector<bool> seen(count, false);
    const auto index_of = [&map](int x, int y) {
        return (static_cast<std::size_t>(y) * static_cast<std::size_t>(map.width)) + static_cast<std::size_t>(x);
    };
    if ((from_gx < 0) || (from_gy < 0) || (from_gx >= map.width) || (from_gy >= map.height)) {
        return false;
    }

    std::deque<std::pair<int, int>> queue;
    queue.emplace_back(from_gx, from_gy);
    seen[index_of(from_gx, from_gy)] = true;
    bool found = false;
    while (!queue.empty() && !found) {
        const auto [cx, cy] = queue.front();
        queue.pop_front();
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if ((dx == 0) && (dy == 0)) {
                    continue;
                }
                const int nx = cx + dx;
                const int ny = cy + dy;
                if ((nx < 0) || (ny < 0) || (nx >= map.width) || (ny >= map.height)) {
                    continue;
                }
                const std::size_t next = index_of(nx, ny);
                if (seen[next] || !minimap_walkable(map, nx, ny)) {
                    continue;
                }
                seen[next] = true;
                came_from[next] = static_cast<int>(index_of(cx, cy));
                if ((nx == to_gx) && (ny == to_gy)) {
                    found = true;
                }
                queue.emplace_back(nx, ny);
            }
        }
    }
    if (!found) {
        return false; // 届かない。**黙って途中まで歩かない**
    }

    std::vector<std::pair<int, int>> reversed;
    for (int at = static_cast<int>(index_of(to_gx, to_gy)); at >= 0; at = came_from[static_cast<std::size_t>(at)]) {
        reversed.emplace_back(at % map.width, at / map.width);
        if (at == static_cast<int>(index_of(from_gx, from_gy))) {
            break;
        }
    }
    if (reversed.size() < 2) {
        return false;
    }
    reversed.pop_back(); // 出発点は歩かない
    this->steps_.assign(reversed.rbegin(), reversed.rend());
    this->issued_ = false;
    return true;
}

void ClickPath::advance(const GameFrame &frame, presentation::InputEventsMessage &out)
{
    if (this->steps_.empty()) {
        return;
    }
    /*
     * **積むべきでない場面では捨てる。**メニュー・プロンプト・数値入力の最中に
     * 方向を積むと、選択やカーソルが勝手に動く（誤コマンド禁止）。
     */
    if (frame.menu_open || !frame.menu_term_lines.empty() || frame.numeric.active
        || !frame.prompt.choices.empty() || frame.text_input_active) {
        this->cancel();
        return;
    }

    if (this->issued_) {
        if ((frame.player_gx == this->expect_gx_) && (frame.player_gy == this->expect_gy_)) {
            this->steps_.erase(this->steps_.begin()); // 1 歩ぶん進んだ
            this->issued_ = false;
            if (this->steps_.empty()) {
                return;
            }
        } else {
            //! まだ動いていない（待つ）か、別の所に居る（壁バンプ・戦闘割込）。
            const int dx = std::abs(frame.player_gx - this->expect_gx_);
            const int dy = std::abs(frame.player_gy - this->expect_gy_);
            if ((dx > 1) || (dy > 1)) {
                this->cancel();
            }
            return;
        }
    }

    const auto [nx, ny] = this->steps_.front();
    const int dx = std::clamp(nx - frame.player_gx, -1, 1);
    const int dy = std::clamp(ny - frame.player_gy, -1, 1);
    if ((dx == 0) && (dy == 0)) {
        this->cancel(); // 経路と実座標が食い違った
        return;
    }
    presentation::InputEventWire wire;
    wire.e = "move";
    wire.dx = dx;
    wire.dy = dy;
    out.events.push_back(wire);
    this->issued_ = true;
    this->expect_gx_ = frame.player_gx + dx;
    this->expect_gy_ = frame.player_gy + dy;
}

} // namespace hd2d
