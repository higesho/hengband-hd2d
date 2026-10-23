#include "dcss/dcss_clouds.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace hd2d::dcss {

namespace {

//! 1 マス・1 箱ごとに決まる乱数（0〜1）。同じ入力なら同じ値（フレームごとに雲がちらつかない）。
float noise01(int a, int b, int c, int d)
{
    std::uint32_t h = 2166136261u;
    for (const std::uint32_t v : { static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b), static_cast<std::uint32_t>(c), static_cast<std::uint32_t>(d) }) {
        h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
        h *= 16777619u;
    }
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return static_cast<float>(h & 0xFFFFu) / 65535.f;
}

float to_linear(std::uint8_t v)
{
    return std::pow(static_cast<float>(v) / 255.f, 2.2f);
}

int hex_value(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f')) {
        return 10 + (c - 'a');
    }
    if ((c >= 'A') && (c <= 'F')) {
        return 10 + (c - 'A');
    }
    return -1;
}

} // namespace

bool DcssClouds::load(const std::string &dir, std::string &err)
{
    this->patterns_.clear();
    std::ifstream in(dir + "/clouds.tsv", std::ios::binary);
    if (!in) {
        err = "clouds.tsv がありません: " + dir;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || (line[0] == '#')) {
            continue;
        }
        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back();
        }
        const std::size_t t1 = line.find('\t');
        const std::size_t t2 = (t1 == std::string::npos) ? std::string::npos : line.find('\t', t1 + 1);
        if (t2 == std::string::npos) {
            continue;
        }
        const int tile = std::atoi(line.substr(0, t1).c_str());
        DcssCloudPattern p;
        p.emissive = (line[t1 + 1] == '1');
        const std::string hex = line.substr(t2 + 1);
        if (hex.size() < static_cast<std::size_t>(DcssCloudPattern::kGrid * DcssCloudPattern::kGrid * 8)) {
            continue;
        }
        bool ok = true;
        for (std::size_t i = 0; i < p.block.size() && ok; ++i) {
            for (std::size_t c = 0; c < 4; ++c) {
                const int hi = hex_value(hex[(i * 8) + (c * 2)]);
                const int lo = hex_value(hex[(i * 8) + (c * 2) + 1]);
                if ((hi < 0) || (lo < 0)) {
                    ok = false;
                    break;
                }
                p.block[i][c] = static_cast<std::uint8_t>((hi * 16) + lo);
            }
        }
        if (ok) {
            this->patterns_[tile] = p;
        }
    }
    if (this->patterns_.empty()) {
        err = "clouds.tsv が空です";
        return false;
    }
    return true;
}

const DcssCloudPattern *DcssClouds::pattern(int tile) const
{
    const auto it = this->patterns_.find(tile);
    return (it == this->patterns_.end()) ? nullptr : &it->second;
}

std::size_t DcssClouds::append_voxels(std::vector<FxVoxel> &out, const std::vector<DcssCloudCell> &cells, float seconds) const
{
    constexpr std::size_t kBudget = 20000; //!< 1 フレームに足す箱の上限（広い雲で描画が止まらないように）
    const std::size_t before = out.size();
    constexpr int kGrid = DcssCloudPattern::kGrid;
    for (const DcssCloudCell &cell : cells) {
        if ((out.size() - before) >= kBudget) {
            break;
        }
        const DcssCloudPattern *pat = this->pattern(cell.tile);
        if (pat == nullptr) {
            continue;
        }
        const float ox = static_cast<float>(cell.gx);
        const float oy = static_cast<float>(cell.gy);
        if (!cell.opaque) {
            // 遮らない雲: 床を這う低く薄い層。区画ごとに薄い箱を 1 つ。
            for (int by = 0; by < kGrid; ++by) {
                for (int bx = 0; bx < kGrid; ++bx) {
                    const auto &blk = pat->block[static_cast<std::size_t>((by * kGrid) + bx)];
                    const float a = static_cast<float>(blk[3]) / 255.f;
                    if (a < 0.15f) {
                        continue;
                    }
                    const float n = noise01(cell.gx, cell.gy, bx, by);
                    FxVoxel v;
                    v.center = Vec3{ ox + ((static_cast<float>(bx) + 0.5f) / kGrid), oy + ((static_cast<float>(by) + 0.5f) / kGrid),
                        kThinLift + (kThinHeight * 0.5f) + (0.01f * std::sin((seconds * 1.1f) + (n * 6.28f))) };
                    v.size = Vec3{ kThinSide, kThinSide, kThinHeight };
                    v.r = to_linear(blk[0]);
                    v.g = to_linear(blk[1]);
                    v.b = to_linear(blk[2]);
                    v.alpha = std::clamp(a * 0.7f, 0.2f, 0.6f);
                    v.emissive = pat->emissive ? 0.6f : 0.f;
                    v.part = FxPart::Mist;
                    out.push_back(v);
                }
            }
            continue;
        }
        // 視界を遮る雲: 床から人の背くらいまで、4x4 の柱で濃く埋める。柱の高さは区画の不透明度で決める（縁は低く、中心は高い）。
        constexpr int kColumns = kGrid / 2;
        for (int cy = 0; cy < kColumns; ++cy) {
            for (int cx = 0; cx < kColumns; ++cx) {
                float sr = 0.f, sg = 0.f, sb = 0.f, sa = 0.f;
                for (int j = 0; j < 2; ++j) {
                    for (int i = 0; i < 2; ++i) {
                        const auto &blk = pat->block[static_cast<std::size_t>((((cy * 2) + j) * kGrid) + (cx * 2) + i)];
                        const float a = static_cast<float>(blk[3]) / 255.f;
                        sr += static_cast<float>(blk[0]) * a;
                        sg += static_cast<float>(blk[1]) * a;
                        sb += static_cast<float>(blk[2]) * a;
                        sa += a;
                    }
                }
                const float a = sa / 4.f;
                if (a < 0.18f) {
                    continue;
                }
                const auto r8 = static_cast<std::uint8_t>(std::clamp(sr / sa, 0.f, 255.f));
                const auto g8 = static_cast<std::uint8_t>(std::clamp(sg / sa, 0.f, 255.f));
                const auto b8 = static_cast<std::uint8_t>(std::clamp(sb / sa, 0.f, 255.f));
                const float density = std::min(1.f, a * 1.6f);
                const float height = kThickHeight * (0.5f + (0.5f * density));
                const int layers = std::max(2, static_cast<int>(std::ceil(height / kThickLayer)));
                const float layer_h = height / static_cast<float>(layers);
                for (int k = 0; k < layers; ++k) {
                    const float n1 = noise01(cell.gx, cell.gy, (cx * 8) + cy, k);
                    const float n2 = noise01(cell.gx + 7, cell.gy + 3, (cx * 8) + cy, k);
                    const float n3 = noise01(cell.gx + 13, cell.gy + 5, (cx * 8) + cy, k);
                    FxVoxel v;
                    v.center = Vec3{ ox + ((static_cast<float>(cx) + 0.5f) / kColumns) + ((n1 - 0.5f) * 0.03f) + (0.01f * std::sin((seconds * 0.6f) + (n3 * 6.28f))),
                        oy + ((static_cast<float>(cy) + 0.5f) / kColumns) + ((n2 - 0.5f) * 0.03f),
                        kThinLift + ((static_cast<float>(k) + 0.5f) * layer_h) + (0.012f * std::sin((seconds * 0.9f) + (n3 * 6.28f))) };
                    v.size = Vec3{ kThickSide + 0.01f, kThickSide + 0.01f, layer_h + 0.01f };
                    v.r = to_linear(r8);
                    v.g = to_linear(g8);
                    v.b = to_linear(b8);
                    // 柱 1 本を縦に重ねたときに、真上から怪物が透けて見える濃さ（原作の雲は怪物の上に不透明度 0.6 で重なる）。
                    v.alpha = std::clamp(0.20f * (0.7f + (0.6f * density)) * (0.85f + (0.3f * n1)), 0.08f, 0.3f);
                    v.emissive = pat->emissive ? 0.35f : 0.f;
                    v.part = FxPart::Mist;
                    out.push_back(v);
                }
            }
        }
    }
    //! 検査用: 環境変数 `HD2D_DCSS_CLOUD_TRACE` があれば、足した箱の数が変わるたびに標準エラーへ 1 行（遮る雲のマス数・遮らない雲のマス数・箱の数）。
    static const bool trace = (std::getenv("HD2D_DCSS_CLOUD_TRACE") != nullptr);
    if (trace) {
        static std::size_t last = ~static_cast<std::size_t>(0);
        const std::size_t added = out.size() - before;
        if (added != last) {
            last = added;
            std::size_t thick = 0;
            for (const DcssCloudCell &c : cells) {
                thick += c.opaque ? 1 : 0;
            }
            std::fprintf(stderr, "[dcss-clouds] cells=%zu opaque=%zu voxels=%zu patterns=%zu\n", cells.size(), thick, added, this->patterns_.size());
        }
    }
    return out.size() - before;
}

} // namespace hd2d::dcss
