#include "dcss/dcss_fx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace hd2d::dcss {

namespace {

int g_failed = 0;
int g_total = 0;

void check(const char *name, bool ok, const std::string &detail = std::string())
{
    ++g_total;
    if (!ok) {
        ++g_failed;
    }
    std::fprintf(stderr, "  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " — ", detail.c_str());
}

std::string join(const std::vector<double> &v)
{
    std::string out;
    for (const double d : v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), " %.3f", d);
        out += buf;
    }
    return out;
}

//! 出たボクセルが載っているマスの数（床の座標の整数部）。
std::set<std::pair<int, int>> cells_of(const std::vector<FxVoxel> &voxels)
{
    std::set<std::pair<int, int>> out;
    for (const FxVoxel &v : voxels) {
        out.insert({ static_cast<int>(std::floor(v.center.x)), static_cast<int>(std::floor(v.center.y)) });
    }
    return out;
}

DcssFxCell cell(int x, int y, int tile, bool by_player = true)
{
    DcssFxCell c;
    c.gx = x;
    c.gy = y;
    c.tiles = { tile };
    c.by_player = by_player;
    return c;
}

} // namespace

int run_dcss_fx_check()
{
    std::fprintf(stderr, "[dcss-fx-check] 攻撃の演出（原作のタイルの絵・飛ぶ道筋の順・薄れ・閃光・予算）\n");
    DcssFx fx;
    std::string err;
    const bool loaded = fx.load("assets/ui/dcss/atlas", err);
    check("材料（tiles.tsv・fx_tiles.tsv・main.png）を読める", loaded, err);
    if (!loaded) {
        return 1;
    }
    constexpr int kFlame = 4850; // TILE_BOLT_FLAME_1
    constexpr int kIcicle = 4861; // TILE_BOLT_ICICLE（向きのある魔法の絵）
    constexpr int kDart = 3846; // TILE_MI_DART4（実物）
    constexpr int kYellow = 4846; // TILE_BOLT_DEFAULT_YELLOW（既定色の弾。立てる塊）
    constexpr int kExplosion = 4869; // TILE_BOLT_ICEBLAST
    {
        const DcssFxSprite &s = fx.sprite(kFlame);
        check("火の玉（TILE_BOLT_FLAME）は丸い塊（B）で、ボクセルになる", s.valid && (s.kind == DcssFxKind::Billboard) && (s.full.size() > 100) && (s.full.size() <= 1024),
            std::to_string(s.full.size()) + " 画素");
        check("粗い版は 2x2 を 1 画素にしたもので、全体より少ない", !s.coarse.empty() && (s.coarse.size() < s.full.size()), std::to_string(s.coarse.size()));
        check("光の色が火の色（赤が最大・成分は 1 以下）", (s.light_r >= s.light_g) && (s.light_r >= s.light_b) && (s.light_r <= 1.0001f), "");
        const DcssFxSprite &i = fx.sprite(kIcicle);
        check("氷柱（TILE_BOLT_ICICLE）は向きのある魔法の絵（D）", i.valid && (i.kind == DcssFxKind::Directional), "");
        const DcssFxSprite &d = fx.sprite(kDart);
        check("ダーツ（TILE_MI_DART）は実物（M）", d.valid && (d.kind == DcssFxKind::Missile), "");
        const DcssFxSprite &none = fx.sprite(1);
        check("main の範囲にない番号は絵を作らない", !none.valid, "");
    }

    // ---- 飛ぶ道筋の順 ----
    {
        const auto vertical = std::vector<std::pair<int, int>>{ { 10, 10 }, { 10, 11 }, { 10, 12 }, { 10, 13 }, { 10, 14 } };
        auto d = DcssFx::flight_delays(vertical, true, 10, 15);
        check("縦の列（プレイヤーの行動）: プレイヤーに近い端から 0.05 秒ずつ", (d[4] == 0.0) && (std::fabs(d[3] - 0.05) < 1e-9) && (std::fabs(d[0] - 0.2) < 1e-9), join(d));
        d = DcssFx::flight_delays(vertical, false, 10, 15);
        check("縦の列（怪物の行動）: プレイヤーから遠い端から", (d[0] == 0.0) && (std::fabs(d[4] - 0.2) < 1e-9), join(d));
        const auto diagonal = std::vector<std::pair<int, int>>{ { 3, 3 }, { 4, 4 }, { 5, 5 }, { 6, 6 } };
        d = DcssFx::flight_delays(diagonal, true, 2, 2);
        check("斜めの列: プレイヤーの側から順", (d[0] == 0.0) && (d[1] < d[2]) && (d[2] < d[3]), join(d));
        const auto bent = std::vector<std::pair<int, int>>{ { 0, 0 }, { 1, 0 }, { 2, 1 }, { 3, 1 }, { 4, 2 } }; // 原作の道筋は 1 マスずつ縦横にずれる
        d = DcssFx::flight_delays(bent, true, -1, -1);
        check("ずれた直線の列も列として扱う（プレイヤーが不明なら小さい番号の端から）", (d[0] == 0.0) && (d[4] > d[3]) && (d[3] > d[2]), join(d));
        std::vector<std::pair<int, int>> ring;
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                if (x != 0 || y != 0) {
                    ring.emplace_back(20 + x, 20 + y);
                }
            }
        }
        d = DcssFx::flight_delays(ring, true, 20, 30);
        check("爆発の輪（広がり）は一度に出す（遅れが全部 0）", std::all_of(d.begin(), d.end(), [](double v) { return v == 0.0; }), join(d));
        std::vector<std::pair<int, int>> square;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                square.emplace_back(x, y);
            }
        }
        d = DcssFx::flight_delays(square, true, 0, 5);
        check("3x3 の塊も列ではない（全部 0）", std::all_of(d.begin(), d.end(), [](double v) { return v == 0.0; }), join(d));
        std::vector<std::pair<int, int>> longline;
        for (int i = 0; i < 20; ++i) {
            longline.emplace_back(5, i);
        }
        d = DcssFx::flight_delays(longline, true, 5, -1);
        check("長い列でも全体は 0.45 秒までに着く", *std::max_element(d.begin(), d.end()) <= DcssFx::kFlightSeconds + 1e-9, join({ *std::max_element(d.begin(), d.end()) }));
        check("1 マスだけなら遅れは無い", DcssFx::flight_delays({ { 1, 1 } }, true, 0, 0) == std::vector<double>{ 0.0 }, "");
    }

    // ---- 時間の流れ（連鎖の稲妻のように一度に届いた列）----
    {
        DcssFx f;
        std::string e2;
        (void)f.load("assets/ui/dcss/atlas", e2);
        DcssFxView view;
        view.right = Vec3{ 1.f, 0.f, 0.f };
        view.up = Vec3{ 0.f, -0.5f, 0.8660254f }; // 見下ろす向き
        const std::vector<DcssFxCell> line = { cell(10, 14, kFlame), cell(10, 13, kFlame), cell(10, 12, kFlame), cell(10, 11, kFlame) };
        f.update(line, {}, 10, 15, 100.0);
        FxFrame out;
        (void)f.append(out, 100.0, view);
        check("届いた直後は先頭の 1 マスだけ点く", cells_of(out.voxels).size() == 1 && cells_of(out.voxels).count({ 10, 14 }) == 1, std::to_string(cells_of(out.voxels).size()) + " マス");
        out = FxFrame{};
        (void)f.append(out, 100.06, view);
        check("0.06 秒後は 2 マス（飛ぶ道筋どおりに伸びる）", cells_of(out.voxels).size() == 2, std::to_string(cells_of(out.voxels).size()));
        out = FxFrame{};
        (void)f.append(out, 100.2, view);
        check("0.2 秒後は 4 マス全部が点いている", cells_of(out.voxels).size() == 4, std::to_string(cells_of(out.voxels).size()));
        check("光る絵には点光源が足される（枠の数まで）", !out.lights.empty() && (out.lights.size() <= 4), std::to_string(out.lights.size()));
        // コアが絵を消した（原作は 30 ミリ秒で消す）: 消えてからも薄れながら残る。
        f.update({}, {}, 10, 15, 100.03);
        out = FxFrame{};
        (void)f.append(out, 100.2, view);
        check("コアが絵を消しても、短く残る（点いたばかりのマスが消えない）", cells_of(out.voxels).size() >= 3, std::to_string(cells_of(out.voxels).size()));
        float alpha_early = 0.f;
        for (const FxVoxel &v : out.voxels) {
            alpha_early = std::max(alpha_early, v.alpha);
        }
        out = FxFrame{};
        (void)f.append(out, 100.45, view);
        float alpha_late = 0.f;
        for (const FxVoxel &v : out.voxels) {
            alpha_late = std::max(alpha_late, v.alpha);
        }
        check("残っている間は薄れていく（不透明度が下がる）", alpha_late < alpha_early, std::to_string(alpha_early) + " -> " + std::to_string(alpha_late));
        out = FxFrame{};
        (void)f.append(out, 102.0, view);
        check("十分たつと全部消え、覚えも捨てる", out.voxels.empty() && (f.tracked_count() == 0), std::to_string(f.tracked_count()));
    }

    // ---- 向きと置き方 ----
    {
        DcssFx f;
        std::string e2;
        (void)f.load("assets/ui/dcss/atlas", e2);
        DcssFxView view;
        view.right = Vec3{ 0.f, 1.f, 0.f }; // カメラを 90 度回した
        view.up = Vec3{ -0.5f, 0.f, 0.8660254f };
        f.update({ cell(5, 5, kDart), cell(7, 7, kYellow) }, {}, 0, 0, 10.0);
        FxFrame out;
        (void)f.append(out, 10.3, view);
        bool flat_ok = true;
        bool stand_ok = true;
        int flat_n = 0;
        int stand_n = 0;
        for (const FxVoxel &v : out.voxels) {
            if (std::floor(v.center.x) == 5.f && std::floor(v.center.y) == 5.f) {
                ++flat_n;
                flat_ok = flat_ok && (std::fabs(v.center.z - DcssFx::kFlatHeight) < 1e-4f);
            } else {
                ++stand_n;
                stand_ok = stand_ok && (v.center.z > 0.f);
            }
        }
        check("実物の矢・弾は地図の面に寝かせる（高さが一定。カメラを回しても）", flat_n > 10 && flat_ok, std::to_string(flat_n));
        check("丸い塊はカメラへ向けて立てる（高さがばらける）", stand_n > 100, std::to_string(stand_n));
        float zmin = 10.f;
        float zmax = -10.f;
        for (const FxVoxel &v : out.voxels) {
            if (std::floor(v.center.x) == 7.f) {
                zmin = std::min(zmin, v.center.z);
                zmax = std::max(zmax, v.center.z);
            }
        }
        check("立てた絵は床より下へ出ない", zmin >= 0.f, std::to_string(zmin));
    }

    // ---- 予算（広い爆発）----
    {
        DcssFx f;
        std::string e2;
        (void)f.load("assets/ui/dcss/atlas", e2);
        DcssFxView view;
        view.right = Vec3{ 1.f, 0.f, 0.f };
        view.up = Vec3{ 0.f, -0.5f, 0.8660254f };
        std::vector<DcssFxCell> big;
        for (int y = 0; y < 9; ++y) {
            for (int x = 0; x < 9; ++x) {
                big.push_back(cell(30 + x, 30 + y, kExplosion));
            }
        }
        f.update(big, {}, 30, 30, 50.0);
        FxFrame out;
        (void)f.append(out, 50.2, view);
        check("半径 4 の爆発（81 マス）でもボクセルは予算以内", out.voxels.size() <= DcssFx::kVoxelBudget + 1500, std::to_string(out.voxels.size()));
        check("予算を超える見込みのときは粗い版で作って 81 マスとも出す", cells_of(out.voxels).size() >= 70, std::to_string(cells_of(out.voxels).size()));
    }

    // ---- 閃光 ----
    {
        DcssFx f;
        std::string e2;
        (void)f.load("assets/ui/dcss/atlas", e2);
        DcssFxView view;
        std::vector<DcssFlashCell> flash;
        for (int i = 0; i < 40; ++i) {
            flash.push_back({ 10 + (i % 8), 10 + (i / 8), 12, 0 }); // LIGHTRED・既定の濃さ
        }
        f.update({}, flash, 10, 10, 5.0);
        FxFrame out;
        (void)f.append(out, 5.05, view);
        check("閃光は見えているマスごとに色をかぶせる（40 マス）", out.voxels.size() == 40, std::to_string(out.voxels.size()));
        bool tint_ok = !out.voxels.empty();
        for (const FxVoxel &v : out.voxels) {
            tint_ok = tint_ok && (v.alpha > 0.05f) && (v.alpha <= 0.35f) && (v.r > v.b);
        }
        check("色は原作の表の LIGHTRED（赤が強い）で、濃さは 0.35 以下の半透明", tint_ok, "");
        f.update({}, {}, 10, 10, 5.05);
        out = FxFrame{};
        (void)f.append(out, 6.0, view);
        check("閃光は消えると薄れて 0 になる", out.voxels.empty() && (f.flash_tracked_count() == 0), std::to_string(f.flash_tracked_count()));
        int r = 0, g = 0, b = 0, a = 0;
        DcssFx::flash_colour(1, r, g, b, a);
        check("閃光の色の表（原作 view_data.js）: BLUE = (0,0,128,100)", (r == 0) && (g == 0) && (b == 128) && (a == 100), "");
    }
    std::fprintf(stderr, "[dcss-fx-check] %d/%d 項目が通った\n", g_total - g_failed, g_total);
    return g_failed == 0 ? 0 : 1;
}

} // namespace hd2d::dcss
