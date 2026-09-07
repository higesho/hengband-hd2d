/*!
 * @file floor_meaning.cpp
 * @brief `floor_meaning.h` の実装。
 */
#include "world/floor_meaning.h"

#include "frame/minimap_snapshot.h"

#include <algorithm>
#include <cstdio>

namespace hd2d {

namespace {

//! 壁の厚みがこれ以上なら岩盤（掘りっぱなしの不定形な塊）。
constexpr int kBedrockDepth = 3;

//! ミニマップの種別 → 「開けているか」。
bool is_open_kind(std::uint8_t kind)
{
    switch (static_cast<MinimapKind>(kind)) {
    case MinimapKind::Floor:
    case MinimapKind::Door:
    case MinimapKind::Stairs:
    case MinimapKind::Item:
    case MinimapKind::Monster:
    case MinimapKind::Player:
        // Item / Monster / Player は**床の上に重ねて書かれた印**なので、地形としては開けている
        // （`MinimapKind` は上書きの優先順位で、地形の種別を潰してしまう）。
        return true;
    case MinimapKind::Water:
    case MinimapKind::Tree:
    case MinimapKind::Grass:
    case MinimapKind::Lava:
    case MinimapKind::Snow:
        // 歩ける地形の細別（FH-09）。色を分けただけで、開けている意味は Floor のまま。
        // ここで閉じると Frox の町（草が大半）が「壁の塊」と読まれて敷地の判定が崩れる。
        return true;
    case MinimapKind::Wall:
    case MinimapKind::Mountain:
    case MinimapKind::Unknown:
    default:
        return false;
    }
}

//! ミニマップの種別 → 「壁か」。**山も壁である**（役割の上では区別しない）。
bool is_wall_kind(MinimapKind kind)
{
    return (kind == MinimapKind::Wall) || (kind == MinimapKind::Mountain);
}

} // namespace

CellRole FloorMeaning::role_at(int x, int y) const
{
    if ((x < 0) || (y < 0) || (x >= this->width) || (y >= this->height)) {
        return CellRole::Unknown;
    }
    return static_cast<CellRole>(this->roles[(static_cast<std::size_t>(y) * static_cast<std::size_t>(this->width)) + static_cast<std::size_t>(x)]);
}

std::uint8_t FloorMeaning::mark_at(int x, int y) const
{
    if ((x < 0) || (y < 0) || (x >= this->width) || (y >= this->height)) {
        return 0;
    }
    return this->marks[(static_cast<std::size_t>(y) * static_cast<std::size_t>(this->width)) + static_cast<std::size_t>(x)];
}

bool FloorMeaning::mountain_at(int x, int y) const
{
    if ((x < 0) || (y < 0) || (x >= this->width) || (y >= this->height)) {
        return false;
    }
    const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(this->width)) + static_cast<std::size_t>(x);
    return (index < this->mountains.size()) && (this->mountains[index] != 0u);
}

bool same_floor(const FloorIdentity &a, const FloorIdentity &b)
{
    return (a.dungeon_id == b.dungeon_id) && (a.dun_level == b.dun_level)
        && (a.generated_turn == b.generated_turn) && (a.kind == b.kind);
}

bool rebuild_floor_meaning(const GameFrame &frame, FloorMeaning &out)
{
    const MinimapSnapshot &minimap = frame.minimap;
    if (!minimap.valid()) {
        return false;
    }

    /*
     * 既知の数と**中身の指紋**を一緒に取る（1 走査）。
     *
     * 指紋が要る理由: 掘削・岩溶解は**既知のまま**壁を床に書き換えるので、既知の数だけを
     * 見ていると作り直されず、掘った壁が描かれ続ける（2026-08-11 に気づいた）。
     *
     * 実体の印（Monster / Item / Player）は**床へ均して**から混ぜる。役割の読みでは
     * どれも「開けている」（`is_open_kind`）なので、均しても答えは変わらない。
     * 均さないと、モンスターが 1 歩あるくたびに指紋が変わって毎ターン作り直しになる
     * ——「毎フレーム作り直すのは安くない」（ヘッダの注記）ための門番が門番でなくなる。
     */
    int known = 0;
    std::uint64_t hash = 14695981039346656037ull; //!< FNV-1a 64
    for (const std::uint8_t kind : minimap.kinds) {
        if (static_cast<MinimapKind>(kind) != MinimapKind::Unknown) {
            ++known;
        }
        std::uint8_t canon = kind;
        switch (static_cast<MinimapKind>(kind)) {
        case MinimapKind::Monster:
        case MinimapKind::Item:
        case MinimapKind::Player:
            canon = static_cast<std::uint8_t>(MinimapKind::Floor);
            break;
        default:
            break;
        }
        hash ^= canon;
        hash *= 1099511628211ull;
    }
    // 作り直すのは「別のフロアになった」「既知が増えた」「既知のまま中身が変わった」とき。
    const bool same = same_floor(out.identity, frame.floor) && (out.width == minimap.width)
        && (out.height == minimap.height);
    if (same && (known == out.known_cells) && (hash == out.content_hash)) {
        return false;
    }

    const int w = minimap.width;
    const int h = minimap.height;
    const auto count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    /*
     * **階段は覚えておく**（2026-08-11）。ミニマップの種別は**そのフレームの見え**なので、
     * 階段のマスにモンスターが立つと `MinimapKind::Monster` に化けて `Stairs` が消える
     * （`presentation_bridge.cpp` の突き合わせは `Item` からは守っているが `Monster` からは
     * 守っていない）。コアは階段を `CAVE_MARK` で覚えているので、**消える方が嘘**である。
     *
     * 消えると困るのは町の読み方で、**階段を抱えた壁の塊は家ではなく岩山**という判断が
     * ひっくり返る（`town_plan.cpp` の `hugs_stairs`。2026-08-11 に気づいた:
     * 「イークの洞穴で洞窟から地上に戻った際に岩山が修正前の家になった」）。
     *
     * 同じフロアのあいだだけ引き継ぐ。壁になったマスは引き継がない——階段が壁に化けるのは
     * 「見間違えていた」ときで、そのときは新しい目のほうが正しい。
     */
    std::vector<std::uint8_t> was_stairs;
    if (same && (out.roles.size() == count)) {
        was_stairs.assign(count, 0u);
        for (std::size_t i = 0; i < count; ++i) {
            was_stairs[i] = (static_cast<CellRole>(out.roles[i]) == CellRole::Stairs) ? 1u : 0u;
        }
    }
    /*
     * **山も覚えておく**（2026-08-12。階段と同じ理由）。山のマスに飛ぶ敵が載ると
     * `MinimapKind::Monster` に化けて山が消え、町の読み方が「岩山か敷地か」で
     * ひっくり返る（`town_plan.cpp` の `crag_mass`）。山は動かないので、
     * 同じフロアのあいだは**一度立った印を落とさない**。
     */
    std::vector<std::uint8_t> was_mountain;
    if (same && (out.mountains.size() == count)) {
        was_mountain = out.mountains;
    }
    out.width = w;
    out.height = h;
    out.identity = frame.floor;
    out.known_cells = known;
    out.content_hash = hash;
    out.roles.assign(count, static_cast<std::uint8_t>(CellRole::Unknown));
    out.marks.assign(count, 0);
    out.wall_depth.assign(count, 0);
    out.mountains.assign(count, 0);
    out.room_cells = 0;
    out.corridor_cells = 0;
    out.structural_cells = 0;
    out.bedrock_cells = 0;
    out.mountain_cells = 0;

    auto at = [w](int x, int y) { return (static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x); };
    auto kind_at = [&minimap, &at, w, h](int x, int y) -> MinimapKind {
        if ((x < 0) || (y < 0) || (x >= w) || (y >= h)) {
            return MinimapKind::Unknown;
        }
        return static_cast<MinimapKind>(minimap.kinds[at(x, y)]);
    };
    auto open_at = [&kind_at](int x, int y) { return is_open_kind(static_cast<std::uint8_t>(kind_at(x, y))); };
    auto wall_at = [&kind_at](int x, int y) { return is_wall_kind(kind_at(x, y)); };

    /*
     * (1) 壁の厚み — 「開けている場所」からのチェビシェフ距離。2 走査で出る。
     *
     * 未探知（`Unknown`）は**開けている扱いにしない**。未探知を開けている扱いにすると、
     * 探索が進んでいない階では壁が全部「厚み 1 = 構造壁」に落ちて岩盤が出ない。
     * 逆に壁扱いにすると、フロアの外周の向こう側が無限に厚い岩盤になるが、そこは
     * どのみち描かないので害が無い。
     */
    constexpr std::uint8_t kFar = 255;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            out.wall_depth[at(x, y)] = open_at(x, y) ? 0 : kFar;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (out.wall_depth[at(x, y)] == 0) {
                continue;
            }
            int best = kFar;
            for (int dy = -1; dy <= 0; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dy == 0) && (dx >= 0)) {
                        continue;
                    }
                    if ((x + dx < 0) || (x + dx >= w) || (y + dy < 0)) {
                        continue;
                    }
                    best = std::min(best, static_cast<int>(out.wall_depth[at(x + dx, y + dy)]) + 1);
                }
            }
            out.wall_depth[at(x, y)] = static_cast<std::uint8_t>(std::min(best, static_cast<int>(kFar)));
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            if (out.wall_depth[at(x, y)] == 0) {
                continue;
            }
            int best = out.wall_depth[at(x, y)];
            for (int dy = 0; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dy == 0) && (dx <= 0)) {
                        continue;
                    }
                    if ((x + dx < 0) || (x + dx >= w) || (y + dy >= h)) {
                        continue;
                    }
                    best = std::min(best, static_cast<int>(out.wall_depth[at(x + dx, y + dy)]) + 1);
                }
            }
            out.wall_depth[at(x, y)] = static_cast<std::uint8_t>(best);
        }
    }

    /*
     * (2) 役割を決める。
     *
     * **部屋か通路かは「2×2 の開けた塊に入っているか」で見る。**§9.1 は
     * 「床の連結成分の幅（1 マス幅の連なり＝通路）」と言っており、幅 1 であることは
     * 「どの 2×2 も埋まらない」と同値である。連結成分を数えるより安く、同じことが言える。
     */
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const MinimapKind kind = kind_at(x, y);
            if (kind == MinimapKind::Unknown) {
                continue;
            }
            const std::size_t index = at(x, y);

            if (is_wall_kind(kind)) {
                //! 山は**役割を変えない**（壁として描く）。印だけ別に立てる。
                if (kind == MinimapKind::Mountain) {
                    out.mountains[index] = 1u;
                }
                const bool bedrock = out.wall_depth[index] >= kBedrockDepth;
                out.roles[index] = static_cast<std::uint8_t>(bedrock ? CellRole::Bedrock : CellRole::StructuralWall);
                if (bedrock) {
                    ++out.bedrock_cells;
                } else {
                    ++out.structural_cells;
                }
                continue;
            }
            if (kind == MinimapKind::Door) {
                out.roles[index] = static_cast<std::uint8_t>(CellRole::Doorway);
                continue;
            }
            if (kind == MinimapKind::Stairs) {
                out.roles[index] = static_cast<std::uint8_t>(CellRole::Stairs);
                continue;
            }

            bool wide = false;
            for (int oy = -1; (oy <= 0) && !wide; ++oy) {
                for (int ox = -1; (ox <= 0) && !wide; ++ox) {
                    wide = open_at(x + ox, y + oy) && open_at(x + ox + 1, y + oy)
                        && open_at(x + ox, y + oy + 1) && open_at(x + ox + 1, y + oy + 1);
                }
            }
            out.roles[index] = static_cast<std::uint8_t>(wide ? CellRole::RoomFloor : CellRole::CorridorFloor);
            if (wide) {
                ++out.room_cells;
            } else {
                ++out.corridor_cells;
            }
        }
    }

    /*
     * (2b) **覚えていた階段を戻す**（上の `was_stairs` の注記）。
     * 壁・扉になったマスは戻さない（見間違えを直したのはそちらが正しい）。
     */
    for (std::size_t i = 0; i < was_stairs.size(); ++i) {
        if (was_stairs[i] == 0u) {
            continue;
        }
        const auto now = static_cast<CellRole>(out.roles[i]);
        if ((now == CellRole::StructuralWall) || (now == CellRole::Bedrock) || (now == CellRole::Doorway)) {
            continue;
        }
        out.roles[i] = static_cast<std::uint8_t>(CellRole::Stairs);
    }

    /*
     * (2c) **覚えていた山を戻す**（上の `was_mountain` の注記）。こちらは階段と違って
     * 役割を書き換えないので、条件も要らない——山だったマスは山である。
     */
    for (std::size_t i = 0; i < was_mountain.size(); ++i) {
        out.mountains[i] |= was_mountain[i];
    }
    for (const std::uint8_t bit : out.mountains) {
        out.mountain_cells += (bit != 0u) ? 1 : 0;
    }

    // (3) 印（壁際・行き止まり・部屋の縁）。
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t index = at(x, y);
            const auto role = static_cast<CellRole>(out.roles[index]);
            if ((role != CellRole::RoomFloor) && (role != CellRole::CorridorFloor)) {
                continue;
            }
            int open_neighbours = 0;
            bool touches_wall = false;
            const int dx[4] = { 1, -1, 0, 0 };
            const int dy[4] = { 0, 0, 1, -1 };
            for (int i = 0; i < 4; ++i) {
                if (open_at(x + dx[i], y + dy[i])) {
                    ++open_neighbours;
                }
                if (wall_at(x + dx[i], y + dy[i])) {
                    touches_wall = true;
                }
            }
            std::uint8_t mark = 0;
            if (touches_wall) {
                mark |= MARK_WALL_SIDE;
                if (role == CellRole::RoomFloor) {
                    mark |= MARK_ROOM_EDGE;
                }
            }
            if (open_neighbours <= 1) {
                mark |= MARK_DEAD_END;
            }
            out.marks[index] = mark;
        }
    }
    return true;
}

std::string floor_meaning_summary(const FloorMeaning &meaning)
{
    char buf[256]{};
    std::snprintf(buf, sizeof(buf),
        "floor id=%d/%d gen=%llu kind=%d  %dx%d  room=%d corridor=%d wall=%d bedrock=%d mountain=%d known=%d",
        meaning.identity.dungeon_id, meaning.identity.dun_level,
        static_cast<unsigned long long>(meaning.identity.generated_turn), meaning.identity.kind,
        meaning.width, meaning.height, meaning.room_cells, meaning.corridor_cells,
        meaning.structural_cells, meaning.bedrock_cells, meaning.mountain_cells, meaning.known_cells);
    return buf;
}

} // namespace hd2d
