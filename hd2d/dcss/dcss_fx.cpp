#include "dcss/dcss_fx.h"

#include "render/fx_styles/fx_parts.h"
#include "render/lighting.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace hd2d::dcss {

namespace {

constexpr int kCanvas = 32; //!< 原作のタイルの 1 辺（画素）

float to_linear(float v)
{
    return std::pow(std::clamp(v, 0.f, 1.f), 2.2f);
}

float clamp01(float v)
{
    return std::clamp(v, 0.f, 1.f);
}

std::uint64_t pack(int gx, int gy, std::uint32_t low)
{
    return (static_cast<std::uint64_t>(gx & 0xFFFF) << 48) | (static_cast<std::uint64_t>(gy & 0xFFFF) << 32) | low;
}

int key_gx(std::uint64_t key)
{
    return static_cast<int>((key >> 48) & 0xFFFF);
}

int key_gy(std::uint64_t key)
{
    return static_cast<int>((key >> 32) & 0xFFFF);
}

std::uint32_t key_low(std::uint64_t key)
{
    return static_cast<std::uint32_t>(key & 0xFFFFFFFFu);
}

//! 検査用: 環境変数 `HD2D_DCSS_FX_TRACE` があれば、足したボクセルの数が変わるたびに標準エラーへ 1 行。
bool trace_enabled()
{
    static const bool on = (std::getenv("HD2D_DCSS_FX_TRACE") != nullptr);
    return on;
}

struct Visible {
    int gx{ 0 };
    int gy{ 0 };
    int tile{ 0 };
    float alpha{ 0.f }; //!< 薄れの倍率（0〜1）
    float bright{ 1.f };
    double first{ 0.0 };
    bool alive{ false };
};

//! 同じ絵が止まったままでも薄れる倍率（`kLifeSeconds` を過ぎたら `kFadeSeconds` かけて 0 へ）と、消えてからの倍率（`ghost` 秒たった）。
float fade_for(double age, bool alive, double ghost, double life, double fade, double ghost_span)
{
    float f = 1.f;
    if (age > life) {
        f = static_cast<float>(std::max(0.0, 1.0 - ((age - life) / fade)));
    }
    if (!alive) {
        f *= static_cast<float>(std::max(0.0, 1.0 - (ghost / ghost_span)));
    }
    return f;
}

} // namespace

double DcssFx::seconds_now()
{
    using clock = std::chrono::steady_clock;
    // 検査用: 環境変数 `HD2D_DCSS_FX_SLOW`（1 以上 100 以下。既定 1）で、演出の時間（点く間隔・薄れ・残る時間）を何倍かに延ばす（飛んでいる途中の絵を撮るため）。
    static const double slow = [] {
        const char *text = std::getenv("HD2D_DCSS_FX_SLOW");
        const double value = (text != nullptr) ? std::atof(text) : 1.0;
        return (value >= 1.0 && value <= 100.0) ? value : 1.0;
    }();
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count() / slow;
}

void DcssFx::flash_colour(int colour, int &r, int &g, int &b, int &a)
{
    //! 原作 `webserver/game_data/static/view_data.js` の `flash_colours`（`tilereg-dgn.cc` と同じ）。
    static constexpr int kTable[16][4] = {
        { 0, 0, 0, 0 }, // BLACK（透明）
        { 0, 0, 128, 100 }, // BLUE
        { 0, 128, 0, 100 }, // GREEN
        { 0, 128, 128, 100 }, // CYAN
        { 128, 0, 0, 100 }, // RED
        { 150, 0, 150, 100 }, // MAGENTA
        { 165, 91, 0, 100 }, // BROWN
        { 50, 50, 50, 150 }, // LIGHTGRAY
        { 0, 0, 0, 150 }, // DARKGRAY
        { 64, 64, 255, 100 }, // LIGHTBLUE
        { 64, 255, 64, 100 }, // LIGHTGREEN
        { 0, 255, 255, 100 }, // LIGHTCYAN
        { 255, 64, 64, 100 }, // LIGHTRED
        { 255, 64, 255, 100 }, // LIGHTMAGENTA
        { 150, 150, 0, 100 }, // YELLOW
        { 255, 255, 255, 100 }, // WHITE
    };
    const int i = std::clamp(colour, 0, 15);
    r = kTable[i][0];
    g = kTable[i][1];
    b = kTable[i][2];
    a = kTable[i][3];
}

bool DcssFx::load(const std::string &dir, std::string &err)
{
    this->loaded_ = false;
    this->rows_.clear();
    this->kinds_.clear();
    this->sprites_.clear();
    {
        std::ifstream in(dir + "/tiles.tsv", std::ios::binary);
        if (!in) {
            err = "tiles.tsv がありません: " + dir;
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || (line[0] == '#') || (line.rfind("const ", 0) == 0)) {
                continue;
            }
            if (line.back() == '\r') {
                line.pop_back();
            }
            std::istringstream ss(line);
            int index = 0;
            std::string page;
            Row r;
            if (!(ss >> index >> page >> r.w >> r.h >> r.ox >> r.oy >> r.sx >> r.sy >> r.ex >> r.ey)) {
                continue;
            }
            if (page == "m") {
                this->rows_[index] = r;
            }
        }
    }
    {
        std::ifstream in(dir + "/fx_tiles.tsv", std::ios::binary);
        if (!in) {
            err = "fx_tiles.tsv がありません: " + dir;
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || (line[0] == '#')) {
                continue;
            }
            if (line.back() == '\r') {
                line.pop_back();
            }
            std::istringstream ss(line);
            int index = 0;
            std::string kind;
            if (!(ss >> index >> kind) || kind.empty()) {
                continue;
            }
            switch (kind[0]) {
            case 'M':
                this->kinds_[index] = DcssFxKind::Missile;
                break;
            case 'D':
                this->kinds_[index] = DcssFxKind::Directional;
                break;
            case 'B':
                this->kinds_[index] = DcssFxKind::Billboard;
                break;
            default:
                break;
            }
        }
    }
    if (this->rows_.empty() || this->kinds_.empty()) {
        err = "tiles.tsv / fx_tiles.tsv が空です";
        return false;
    }
    if (!fx_load_png(dir + "/main.png", this->page_) || (this->page_.width <= 0)) {
        err = "main.png を読めません: " + dir;
        return false;
    }
    this->loaded_ = true;
    return true;
}

bool DcssFx::build_sprite(int tile, DcssFxSprite &out) const
{
    const auto row = this->rows_.find(tile);
    if (row == this->rows_.end()) {
        return false;
    }
    const Row &r = row->second;
    const int w = r.ex - r.sx;
    const int h = r.ey - r.sy;
    if ((w <= 0) || (h <= 0) || (r.ex > this->page_.width) || (r.ey > this->page_.height)) {
        return false;
    }
    // 32 ドット四方に置く（原作 `draw_tile` と同じ位置。`gen_dcss_clouds.py` と同じ式）。
    std::vector<std::uint8_t> canvas(static_cast<std::size_t>(kCanvas) * kCanvas * 4u, 0);
    const int x0 = static_cast<int>(static_cast<float>(r.ox) + 16.f - (static_cast<float>(r.w) * 0.5f));
    const int y0 = r.oy + (kCanvas - r.h);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const int dx = x0 + i;
            const int dy = y0 + j;
            if ((dx < 0) || (dx >= kCanvas) || (dy < 0) || (dy >= kCanvas)) {
                continue;
            }
            const std::size_t s = ((static_cast<std::size_t>(r.sy + j) * static_cast<std::size_t>(this->page_.width)) + static_cast<std::size_t>(r.sx + i)) * 4u;
            const std::size_t d = ((static_cast<std::size_t>(dy) * kCanvas) + static_cast<std::size_t>(dx)) * 4u;
            for (std::size_t c = 0; c < 4; ++c) {
                canvas[d + c] = this->page_.rgba[s + c];
            }
        }
    }
    out = DcssFxSprite{};
    out.valid = true;
    const auto kind = this->kinds_.find(tile);
    out.kind = (kind == this->kinds_.end()) ? DcssFxKind::Item : kind->second;

    constexpr std::uint8_t kMinAlpha = 50;
    const auto pixel_at = [&](int x, int y, float &pr, float &pg, float &pb, float &pa) {
        const std::size_t d = ((static_cast<std::size_t>(y) * kCanvas) + static_cast<std::size_t>(x)) * 4u;
        pr = static_cast<float>(canvas[d]) / 255.f;
        pg = static_cast<float>(canvas[d + 1]) / 255.f;
        pb = static_cast<float>(canvas[d + 2]) / 255.f;
        pa = static_cast<float>(canvas[d + 3]) / 255.f;
    };
    float lr = 0.f, lg = 0.f, lb = 0.f, lw = 0.f;
    for (int y = 0; y < kCanvas; ++y) {
        for (int x = 0; x < kCanvas; ++x) {
            const std::size_t d = ((static_cast<std::size_t>(y) * kCanvas) + static_cast<std::size_t>(x)) * 4u;
            if (canvas[d + 3] < kMinAlpha) {
                continue;
            }
            float pr, pg, pb, pa;
            pixel_at(x, y, pr, pg, pb, pa);
            DcssFxPixel p;
            p.u = ((static_cast<float>(x) + 0.5f) / kCanvas) - 0.5f;
            p.v = ((static_cast<float>(y) + 0.5f) / kCanvas) - 0.5f;
            p.r = to_linear(pr);
            p.g = to_linear(pg);
            p.b = to_linear(pb);
            p.a = pa;
            out.full.push_back(p);
            const float peak = std::max({ p.r, p.g, p.b });
            const float weight = pa * peak * peak;
            lr += p.r * weight;
            lg += p.g * weight;
            lb += p.b * weight;
            lw += weight;
        }
    }
    // 2x2 を 1 画素にした粗い版（広い爆発の予算の節約用）。α で重みを付けた平均の色、α の平均。
    for (int by = 0; by < kCanvas / 2; ++by) {
        for (int bx = 0; bx < kCanvas / 2; ++bx) {
            float sr = 0.f, sg = 0.f, sb = 0.f, sa = 0.f;
            for (int j = 0; j < 2; ++j) {
                for (int i = 0; i < 2; ++i) {
                    float pr, pg, pb, pa;
                    pixel_at((bx * 2) + i, (by * 2) + j, pr, pg, pb, pa);
                    sr += pr * pa;
                    sg += pg * pa;
                    sb += pb * pa;
                    sa += pa;
                }
            }
            const float a = sa / 4.f;
            if (a < 0.3f) {
                continue;
            }
            DcssFxPixel p;
            p.u = ((static_cast<float>(bx) + 0.5f) / (kCanvas / 2)) - 0.5f;
            p.v = ((static_cast<float>(by) + 0.5f) / (kCanvas / 2)) - 0.5f;
            p.r = to_linear(sr / sa);
            p.g = to_linear(sg / sa);
            p.b = to_linear(sb / sa);
            p.a = a;
            out.coarse.push_back(p);
        }
    }
    if (lw > 1e-4f) {
        lr /= lw;
        lg /= lw;
        lb /= lw;
        const float peak = std::max({ lr, lg, lb, 1e-4f });
        out.light_r = lr / peak;
        out.light_g = lg / peak;
        out.light_b = lb / peak;
    }
    return !out.full.empty();
}

const DcssFxSprite &DcssFx::sprite(int tile)
{
    const auto found = this->sprites_.find(tile);
    if (found != this->sprites_.end()) {
        return found->second;
    }
    DcssFxSprite s;
    if (!this->build_sprite(tile, s)) {
        s = DcssFxSprite{};
    }
    return this->sprites_.emplace(tile, std::move(s)).first->second;
}

std::vector<double> DcssFx::flight_delays(const std::vector<std::pair<int, int>> &cells, bool origin_is_player, int player_gx, int player_gy)
{
    const std::size_t n = cells.size();
    std::vector<double> out(n, 0.0);
    if (n < 2) {
        return out;
    }
    // いちばん離れた 2 マス（列の両端の候補）。
    std::size_t ia = 0;
    std::size_t ib = 1;
    double best = -1.0;
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            const double dx = static_cast<double>(cells[j].first - cells[i].first);
            const double dy = static_cast<double>(cells[j].second - cells[i].second);
            const double d = (dx * dx) + (dy * dy);
            if (d > best) {
                best = d;
                ia = i;
                ib = j;
            }
        }
    }
    const double ax = cells[ia].first;
    const double ay = cells[ia].second;
    const double vx = static_cast<double>(cells[ib].first) - ax;
    const double vy = static_cast<double>(cells[ib].second) - ay;
    const double len = std::sqrt((vx * vx) + (vy * vy));
    const int cheb = std::max(std::abs(cells[ib].first - cells[ia].first), std::abs(cells[ib].second - cells[ia].second));
    if ((len < 0.5) || (static_cast<int>(n) > (2 * cheb) + 2)) {
        return out; // 広がり（爆発の輪など）は一度に出す
    }
    // 直線の列か（全部のマスが両端を結ぶ線から 1.2 マス以内）。
    std::vector<double> along(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        const double px = static_cast<double>(cells[i].first) - ax;
        const double py = static_cast<double>(cells[i].second) - ay;
        const double perpendicular = std::fabs((vx * py) - (vy * px)) / len;
        if (perpendicular > 1.2) {
            return out;
        }
        along[i] = ((px * vx) + (py * vy)) / len;
    }
    // 撃ち手の側の端から順に。プレイヤーの行動なら、プレイヤーに近い端から。怪物・味方の行動なら、プレイヤーから遠い端から。
    bool a_first = true;
    if (player_gx >= 0) {
        const double da = std::hypot(ax - player_gx, ay - player_gy);
        const double db = std::hypot(static_cast<double>(cells[ib].first) - player_gx, static_cast<double>(cells[ib].second) - player_gy);
        a_first = origin_is_player ? (da <= db) : (da >= db);
    }
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](std::size_t l, std::size_t r) { return a_first ? (along[l] < along[r]) : (along[l] > along[r]); });
    const double step = std::min(kStepSeconds, kFlightSeconds / static_cast<double>(n - 1));
    for (std::size_t rank = 0; rank < n; ++rank) {
        out[order[rank]] = static_cast<double>(rank) * step;
    }
    return out;
}

void DcssFx::update(const std::vector<DcssFxCell> &cells, const std::vector<DcssFlashCell> &flashes, int player_gx, int player_gy, double now)
{
    for (auto &e : this->tracked_) {
        e.second.alive = false;
    }
    // 今回初めて届いたマス（位置ごと）。一度に届いた直線の列は、撃ち手の側から順に点ける。
    std::vector<std::pair<int, int>> fresh;
    bool fresh_by_player = true;
    for (const DcssFxCell &cell : cells) {
        bool is_new = false;
        for (const int tile : cell.tiles) {
            if (this->tracked_.find(pack(cell.gx, cell.gy, static_cast<std::uint32_t>(tile))) == this->tracked_.end()) {
                is_new = true;
            }
        }
        if (is_new) {
            fresh.emplace_back(cell.gx, cell.gy);
            fresh_by_player = cell.by_player;
        }
    }
    const std::vector<double> delays = flight_delays(fresh, fresh_by_player, player_gx, player_gy);
    if (trace_enabled() && (fresh.size() >= 2)) {
        // 検査用: 一度に届いたマスの並びと、点く遅れ（秒）。直線の列なら遅れが 0 から増える。
        std::string text;
        for (std::size_t i = 0; i < fresh.size(); ++i) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), " (%d,%d)+%.3f", fresh[i].first, fresh[i].second, delays[i]);
            text += buf;
        }
        std::fprintf(stderr, "[dcss-fx] batch n=%zu by_player=%d player=(%d,%d):%s\n", fresh.size(), fresh_by_player ? 1 : 0, player_gx, player_gy, text.c_str());
    }
    for (const DcssFxCell &cell : cells) {
        double delay = 0.0;
        for (std::size_t i = 0; i < fresh.size(); ++i) {
            if ((fresh[i].first == cell.gx) && (fresh[i].second == cell.gy)) {
                delay = delays[i];
                break;
            }
        }
        for (const int tile : cell.tiles) {
            const std::uint64_t key = pack(cell.gx, cell.gy, static_cast<std::uint32_t>(tile));
            auto it = this->tracked_.find(key);
            if (it == this->tracked_.end()) {
                it = this->tracked_.emplace(key, Tracked{ now, now + delay, now, true }).first;
            }
            it->second.alive = true;
            it->second.last = now;
        }
    }
    for (auto &e : this->flash_tracked_) {
        e.second.alive = false;
    }
    for (const DcssFlashCell &cell : flashes) {
        const std::uint64_t key = pack(cell.gx, cell.gy, (static_cast<std::uint32_t>(cell.colour & 0xFF) << 16) | static_cast<std::uint32_t>(cell.alpha & 0xFFFF));
        auto it = this->flash_tracked_.find(key);
        if (it == this->flash_tracked_.end()) {
            it = this->flash_tracked_.emplace(key, Tracked{ now, now, now, true }).first;
        }
        it->second.alive = true;
        it->second.last = now;
    }
}

std::size_t DcssFx::append(FxFrame &out, double now, const DcssFxView &view)
{
    if (!this->loaded_ || !this->active()) {
        return 0;
    }
    const std::size_t before = out.voxels.size();

    // ---- 見える物を集める（消えた物・薄れ終わった物はここで捨てる）----
    std::vector<Visible> visible;
    for (auto it = this->tracked_.begin(); it != this->tracked_.end();) {
        const Tracked &t = it->second;
        const double age = now - t.reveal;
        const double ghost = now - std::max(t.last, t.reveal);
        if (!t.alive && (ghost > kGhostSeconds)) {
            it = this->tracked_.erase(it); // 止まったまま残る物（届き続けている物）は、薄れ終えても覚える（届き直しで光り直さない）
            continue;
        }
        if (age < 0.0) {
            ++it; // まだ点かない（飛ぶ道筋で、弾がまだ来ていないマス）
            continue;
        }
        Visible v;
        v.gx = key_gx(it->first);
        v.gy = key_gy(it->first);
        v.tile = static_cast<int>(key_low(it->first));
        v.alpha = fade_for(age, t.alive, ghost, kLifeSeconds, kFadeSeconds, kGhostSeconds);
        v.bright = kHeadBright + ((kTailBright - kHeadBright) * clamp01(static_cast<float>(age / kTrailSeconds)));
        v.first = t.reveal;
        v.alive = t.alive;
        if (v.alpha > 0.02f) {
            visible.push_back(v);
        }
        ++it;
    }

    // 立てる絵は、全体が予算を超える見込みのとき粗い版で作る。
    std::size_t estimate = 0;
    for (const Visible &v : visible) {
        const DcssFxSprite &s = this->sprite(v.tile);
        if (s.valid && ((s.kind == DcssFxKind::Billboard) || (s.kind == DcssFxKind::Item))) {
            estimate += s.full.size();
        } else if (s.valid) {
            estimate += s.full.size();
        }
    }
    const bool coarse_stand = estimate > kVoxelBudget;

    const Vec3 forward = cross(view.right, view.up);
    float toward_x = -forward.x;
    float toward_y = -forward.y;
    const float toward_len = std::sqrt((toward_x * toward_x) + (toward_y * toward_y));
    if (toward_len > 1e-3f) {
        toward_x /= toward_len;
        toward_y /= toward_len;
    } else {
        toward_x = toward_y = 0.f;
    }

    struct LightCandidate {
        float x, y, r, g, b, w;
        double first;
    };
    std::vector<LightCandidate> lights;

    for (const Visible &v : visible) {
        if ((out.voxels.size() - before) >= kVoxelBudget) {
            break;
        }
        const DcssFxSprite &s = this->sprite(v.tile);
        if (!s.valid) {
            continue;
        }
        const bool flat = (s.kind == DcssFxKind::Missile) || (s.kind == DcssFxKind::Directional);
        const bool glow = (s.kind == DcssFxKind::Directional) || (s.kind == DcssFxKind::Billboard);
        const bool coarse = !flat && coarse_stand;
        const std::vector<DcssFxPixel> &pixels = coarse ? s.coarse : s.full;
        const float plate = (s.kind == DcssFxKind::Missile) ? (kPlateSize * kMissileScale) : kPlateSize;
        const float pixel = plate / static_cast<float>(coarse ? kCanvas / 2 : kCanvas);
        const float cx = static_cast<float>(v.gx) + 0.5f + (flat ? 0.f : (toward_x * kStandToward));
        const float cy = static_cast<float>(v.gy) + 0.5f + (flat ? 0.f : (toward_y * kStandToward));
        const float cz = flat ? kFlatHeight : kStandHeight;
        const float gain = glow ? 1.6f : ((s.kind == DcssFxKind::Missile) ? 0.7f : 0.f);
        for (const DcssFxPixel &p : pixels) {
            FxVoxel vox;
            if (flat) {
                vox.center = Vec3{ cx + (p.u * plate), cy + (p.v * plate), cz };
                vox.size = Vec3{ pixel, pixel, pixel };
            } else {
                vox.center = Vec3{ cx, cy, cz } + (view.right * (p.u * plate)) + (view.up * (-p.v * plate));
                const float side = pixel * 1.3f;
                vox.size = Vec3{ side, side, side };
            }
            vox.r = p.r;
            vox.g = p.g;
            vox.b = p.b;
            const float a = p.a * v.alpha;
            vox.alpha = (a >= 0.97f) ? 1.f : a;
            const float lum = std::max({ p.r, p.g, p.b });
            vox.emissive = gain * (0.4f + (0.9f * lum)) * v.bright;
            vox.part = FxPart::Sprite;
            if (vox.alpha > 0.03f) {
                out.voxels.push_back(vox);
            }
        }
        if (glow || (s.kind == DcssFxKind::Missile)) {
            lights.push_back({ static_cast<float>(v.gx) + 0.5f, static_cast<float>(v.gy) + 0.5f, s.light_r, s.light_g, s.light_b, v.alpha * (glow ? 1.f : 0.4f), v.first });
        }
    }

    // ---- 点光源（枠は少ない）: 弾の先頭（いちばん新しく現れた物）と、全体の重心 ----
    if (!lights.empty()) {
        const LightCandidate *head = &lights.front();
        float wx = 0.f, wy = 0.f, wr = 0.f, wg = 0.f, wb = 0.f, wsum = 0.f;
        float spread = 0.f;
        for (const LightCandidate &c : lights) {
            if (c.first > head->first) {
                head = &c;
            }
            wx += c.x * c.w;
            wy += c.y * c.w;
            wr += c.r * c.w;
            wg += c.g * c.w;
            wb += c.b * c.w;
            wsum += c.w;
        }
        if (wsum > 1e-4f) {
            wx /= wsum;
            wy /= wsum;
            for (const LightCandidate &c : lights) {
                spread = std::max(spread, std::hypot(c.x - wx, c.y - wy));
            }
            PointLight centre;
            centre.x = wx;
            centre.y = wy;
            centre.z = 0.8f;
            centre.radius = std::min(8.f, 3.2f + spread);
            centre.r = wr / wsum;
            centre.g = wg / wsum;
            centre.b = wb / wsum;
            centre.intensity = 2.4f * std::min(1.f, wsum / 2.f);
            out.lights.push_back(centre);
            if (lights.size() > 1) {
                PointLight tip;
                tip.x = head->x;
                tip.y = head->y;
                tip.z = 0.7f;
                tip.radius = 3.5f;
                tip.r = head->r;
                tip.g = head->g;
                tip.b = head->b;
                tip.intensity = 2.0f * head->w;
                out.lights.push_back(tip);
            }
        }
    }

    // ---- 閃光（原作は見えているマスの矩形に色をかぶせる）----
    std::size_t flashes_added = 0;
    constexpr double kFlashLife = 0.6;
    constexpr double kFlashGhost = 0.25;
    for (auto it = this->flash_tracked_.begin(); it != this->flash_tracked_.end();) {
        const Tracked &t = it->second;
        const double age = now - t.first;
        const double ghost = now - t.last;
        if (!t.alive && (ghost > kFlashGhost)) {
            it = this->flash_tracked_.erase(it);
            continue;
        }
        const std::uint32_t low = key_low(it->first);
        int r = 0, g = 0, b = 0, a = 0;
        flash_colour(static_cast<int>((low >> 16) & 0xFF), r, g, b, a);
        const int given = static_cast<int>(low & 0xFFFF);
        if (given > 0) {
            a = std::min(given, 255);
        }
        const float f = fade_for(age, t.alive, ghost, kFlashLife, kFadeSeconds, kFlashGhost);
        const float alpha = std::min(0.35f, (static_cast<float>(a) / 255.f) * 0.8f) * f;
        if ((alpha > 0.01f) && (flashes_added < kFlashBudget) && (a > 0)) {
            FxVoxel vox;
            vox.center = Vec3{ static_cast<float>(key_gx(it->first)) + 0.5f, static_cast<float>(key_gy(it->first)) + 0.5f, 0.55f };
            vox.size = Vec3{ 1.02f, 1.02f, 1.1f };
            vox.r = to_linear(static_cast<float>(r) / 255.f);
            vox.g = to_linear(static_cast<float>(g) / 255.f);
            vox.b = to_linear(static_cast<float>(b) / 255.f);
            vox.alpha = alpha;
            vox.emissive = 0.5f;
            vox.part = FxPart::Wave;
            out.voxels.push_back(vox);
            ++flashes_added;
        }
        ++it;
    }

    const std::size_t added = out.voxels.size() - before;
    if (trace_enabled()) {
        static std::size_t last = ~static_cast<std::size_t>(0);
        if (added != last) {
            last = added;
            std::fprintf(stderr, "[dcss-fx] tracked=%zu visible=%zu voxels=%zu flashes=%zu lights=%zu coarse=%d\n", this->tracked_.size(), visible.size(), added,
                flashes_added, out.lights.size(), coarse_stand ? 1 : 0);
        }
    }
    return added;
}

} // namespace hd2d::dcss
