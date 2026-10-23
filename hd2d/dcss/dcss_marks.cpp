#include "dcss/dcss_marks.h"

#include <nlohmann/json.hpp>

namespace hd2d::dcss {

namespace {

using json = nlohmann::json;

RingTint make_tint(const DcssMark &mark, int r, int g, int b, float radius, float alpha)
{
    RingTint t;
    t.gx = mark.gx;
    t.gy = mark.gy;
    t.radius = radius;
    t.r = static_cast<float>(r) / 255.f;
    t.g = static_cast<float>(g) / 255.f;
    t.b = static_cast<float>(b) / 255.f;
    t.a = alpha;
    return t;
}

//! 輪の半径と濃さ（マス単位）。濃さは**半透明**（決めたこと D23「足元の円は半透明に」）。
//! 板は D24 で画面に正対してマスの奥へ 2 割ずれ、輪の手前の弧が板の陰に隠れたので、輪を大きくした（0.44 → 0.64。マスの外へ 0.14 はみ出す）。
//! 1.0・1.5 倍（0.44・0.66）を撮り比べて決めた（`tools/dcss/dc_look_check.py` の撮影）。召喚者の輪は元の差（+0.08）を保つ。
constexpr float kMainRadius = 0.64f;
constexpr float kSummonerRadius = 0.72f;
constexpr float kRingAlpha = 0.5f;

} // namespace

std::vector<RingTint> DcssMarks::rings_for(const DcssMark &mark)
{
    std::vector<RingTint> out;
    if (mark.player) {
        return out;
    }
    using namespace fgflag;
    //! 色は原作の枠の絵（`rltiles/dc-feat.txt` の halo_*・monster_threat_*）の主な色を画素から測った。
    //! 態度（友好・中立・平和）と強さは、原作の `tilepick.cc` で同時には立たない（態度が先）。
    const std::uint64_t att = mark.flags & kAttMask;
    if (att == kPet) {
        out.push_back(make_tint(mark, 113, 239, 134, kMainRadius, kRingAlpha)); // HALO_FRIENDLY
    } else if (att == kGoodNeutral) {
        out.push_back(make_tint(mark, 239, 197, 52, kMainRadius, kRingAlpha)); // HALO_GD_NEUTRAL
    } else if (att == kNeutral) {
        out.push_back(make_tint(mark, 205, 205, 205, kMainRadius, kRingAlpha)); // HALO_NEUTRAL
    }
    const std::uint64_t threat = mark.flags & kThreatMask;
    if (threat != 0) {
        const bool ghost = (mark.flags & kGhost) != 0;
        int r = 0, g = 0, b = 0;
        bool have = true;
        if (threat == kTrivial) {
            ghost ? (r = 77, g = 77, b = 77) : (r = 102, g = 102, b = 102);
        } else if (threat == kEasy) {
            ghost ? (r = 120, g = 149, b = 149) : (r = 204, g = 204, b = 204);
        } else if (threat == kTough) {
            ghost ? (r = 152, g = 140, b = 76) : (r = 255, g = 255, b = 0);
        } else if (threat == kNasty) {
            ghost ? (r = 170, g = 0, b = 30) : (r = 255, g = 0, b = 0);
        } else if (threat == kUnusual) {
            r = 204, g = 97, b = 213; // 原作は幽霊でも THREAT_UNUSUAL の 1 枚
        } else {
            have = false;
        }
        if (have) {
            out.push_back(make_tint(mark, r, g, b, kMainRadius, kRingAlpha));
        }
    }
    if (mark.summoner) {
        out.push_back(make_tint(mark, 53, 204, 238, kSummonerRadius, kRingAlpha)); // HALO_SUMMONER
    }
    return out;
}

bool DcssMarks::ingest_json(const std::string &payload)
{
    const json m = json::parse(payload, nullptr, false);
    if (m.is_discarded() || !m.is_object()) {
        return false;
    }
    std::vector<DcssMark> next;
    const auto list = m.find("m");
    if ((list != m.end()) && list->is_array()) {
        for (const json &row : *list) {
            if (!row.is_array() || (row.size() < 7)) {
                continue;
            }
            DcssMark mark;
            mark.gx = row[0].get<int>();
            mark.gy = row[1].get<int>();
            mark.flags = static_cast<std::uint64_t>(row[2].get<std::uint32_t>()) | (static_cast<std::uint64_t>(row[3].get<std::uint32_t>()) << 32);
            mark.summoner = row[5].get<int>() != 0;
            mark.player = row[6].get<int>() != 0;
            next.push_back(mark);
        }
    }
    std::vector<DcssCloudCell> clouds;
    const auto cl = m.find("c");
    if ((cl != m.end()) && cl->is_array()) {
        for (const json &row : *cl) {
            if (!row.is_array() || (row.size() < 4)) {
                continue;
            }
            clouds.push_back({ row[0].get<int>(), row[1].get<int>(), row[2].get<int>(), row[3].get<int>() != 0 });
        }
    }
    std::vector<DcssFxCell> fx_cells;
    const auto fx = m.find("f");
    if ((fx != m.end()) && fx->is_array()) {
        for (const json &row : *fx) {
            if (!row.is_array() || (row.size() < 3) || !row[2].is_array()) {
                continue;
            }
            DcssFxCell cell;
            cell.gx = row[0].get<int>();
            cell.gy = row[1].get<int>();
            for (const json &tile : row[2]) {
                if (tile.is_number_integer()) {
                    cell.tiles.push_back(tile.get<int>());
                }
            }
            cell.by_player = (row.size() < 4) || !row[3].is_number() || (row[3].get<int>() != 0);
            if (!cell.tiles.empty()) {
                fx_cells.push_back(std::move(cell));
            }
        }
    }
    std::vector<DcssFlashCell> flash_cells;
    const auto fl = m.find("fl");
    if ((fl != m.end()) && fl->is_array()) {
        for (const json &row : *fl) {
            if (!row.is_array() || (row.size() < 4)) {
                continue;
            }
            flash_cells.push_back({ row[0].get<int>(), row[1].get<int>(), row[2].get<int>(), row[3].get<int>() });
        }
    }
    const auto pos = m.find("p");
    if ((pos != m.end()) && pos->is_array() && (pos->size() >= 2) && (*pos)[0].is_number() && (*pos)[1].is_number()) {
        this->player_gx_ = (*pos)[0].get<int>();
        this->player_gy_ = (*pos)[1].get<int>();
    }
    this->marks_ = std::move(next);
    this->clouds_ = std::move(clouds);
    this->fx_cells_ = std::move(fx_cells);
    this->flash_cells_ = std::move(flash_cells);
    this->rings_.clear();
    for (const DcssMark &mark : this->marks_) {
        for (const RingTint &t : rings_for(mark)) {
            this->rings_.push_back(t);
        }
    }
    ++this->serial_;
    return true;
}

} // namespace hd2d::dcss
