/*!
 * @file fx_silq.cpp
 * @brief Sil-Q にあって変愚に無い属性の見た目（`sq_` で始まる名前。2 個）。
 *
 * 属性の名前（コア名 → 見た目の名前）と、既存の見た目へ寄せる属性は `presentation/bridge/attack_fx_core_attr_aliases.h`。
 * 原作は `silq/src/defines.h` の `GF_xxx`（27 個）。変愚と同名のもの・寄せるもの（`BOULDER`→`shards` など）はここへ来ない。
 *
 * | 属性 | 原作の説明 | 見た目 |
 * |---|---|---|
 * | `sq_earthquake` | 地震（冷気に弱い物を砕く。大地を揺らす） | 天井から岩屑が降り、床にひびが走って、土煙が立つ。画面が揺れる（派手に光らせない。Frox の `fc_quake` のような光るひびは使わない） |
 * | `sq_web` | 着弾点に蜘蛛の巣の罠を作る | 上から糸が降りて、床に白い巣（放射状の糸と同心の輪）が広がって残る |
 *
 * **原作より情報を増やさない**（効いたか・耐えたかで絵を変えない）。
 */
#include "render/fx_styles/fx_look_motifs.h"

#include <algorithm>
#include <cmath>

namespace hd2d {

namespace {

using namespace looks;

void look_earthquake(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.02f, 0.9f, 0.55f);
    // 天井から降る岩屑。
    const int n = L.mini ? 2 : 4;
    for (int k = 0; k < n; ++k) {
        const std::uint32_t id = static_cast<std::uint32_t>(k);
        const float born = (0.08f * static_cast<float>(k)) + (0.2f * fx_rand(L.seed, id, 9u));
        const float fall = 0.3f;
        const float age = te - born;
        const float ox = (fx_rand(L.seed, id, 1u) - 0.5f) * 0.7f * L.scale;
        const float oy = (fx_rand(L.seed, id, 2u) - 0.5f) * 0.7f * L.scale;
        if ((age >= 0.f) && (age < fall)) {
            const float u = age / fall;
            rock_cluster(out, Vec3{ L.foot.x + ox, L.foot.y + oy, fx_lerp(2.2f, 0.12f, u * u) }, 0.25f * L.scale, age * 8.f, L.e.base, L.e.tint, 1.f, 1.0f, L.seed + id);
        } else if (age >= fall) {
            const float rest = 1.f - fx_ramp(age, 0.7f, 1.1f);
            rock_cluster(out, Vec3{ L.foot.x + ox, L.foot.y + oy, 0.07f }, 0.2f * L.scale, static_cast<float>(k), L.e.base, L.e.tint, rest, 0.9f, L.seed + id);
            debris_burst(L, out, Vec3{ L.foot.x + ox, L.foot.y + oy, 0.1f }, 4, born + fall, 1.6f, 1.1f, 0.05f * L.scale, L.e.base, L.e.tint, 0.6f, 21u + id);
        }
    }
    // 床のひび（光らない灰色の筋と、縁の砂ぼこり）。
    const float grow = fx_ramp(te, 0.f, 0.4f);
    for (int k = 0; k < (L.mini ? 2 : 4); ++k) {
        const std::uint32_t id = static_cast<std::uint32_t>(k);
        const float a = 2.f * kPi * fx_rand(L.seed, id, 3u);
        const float len = (0.5f + (0.3f * fx_rand(L.seed, id, 4u))) * L.scale;
        const Vec3 p0{ L.foot.x, L.foot.y, 0.03f };
        const Vec3 p1 = p0 + Vec3{ std::cos(a) * len * 0.5f, std::sin(a) * len * 0.5f, 0.f };
        const float b = a + ((fx_rand(L.seed, id, 5u) - 0.5f) * 1.3f);
        const Vec3 p2 = p1 + Vec3{ std::cos(b) * len * 0.5f, std::sin(b) * len * 0.5f, 0.f };
        floor_dots(out, p0, p1, grow * 2.f, 0.07f, FxColour{ 0.04f, 0.035f, 0.03f }, 0.f, alpha);
        floor_dots(out, p1, p2, (grow * 2.f) - 1.f, 0.065f, FxColour{ 0.04f, 0.035f, 0.03f }, 0.f, alpha);
        floor_dots(out, p0 + Vec3{ 0.f, 0.f, 0.01f }, p1, grow * 2.f, 0.03f, FxColour{ 0.55f, 0.5f, 0.42f }, 0.5f, alpha * 0.7f);
    }
    // 土煙。
    FxSmoke dust;
    dust.origin = Vec3{ L.front.x, L.front.y, 0.05f };
    dust.radius = 0.28f * L.scale;
    dust.born = 0.05f;
    dust.duration = 0.45f;
    dust.life = 0.95f;
    dust.count = count_of(L, 9);
    dust.rise = 0.35f;
    dust.size = 0.2f * L.scale;
    dust.grow = 2.f;
    dust.colour = FxColour{ 0.62f, 0.56f, 0.46f };
    dust.colour_end = FxColour{ 0.42f, 0.38f, 0.32f };
    dust.alpha = 0.6f;
    dust.emissive = 0.5f;
    dust.seed = L.seed;
    (void)fx_emit_smoke(dust, te, out);
    if (!L.mini) {
        fx_add_shake(out, fx_shake_envelope(te, 0.f, 0.9f, 0.13f));
        FxEntityMod &mod = fx_mod_for(out, L.gx, L.gy);
        mod.tremble = std::max(mod.tremble, 0.035f * fx_envelope(te, 0.05f, 0.6f, 0.3f));
        mod.tremble_phase = te * 85.f;
    }
}

void all_earthquake(const FxStyleCtx &ctx, const Entry &e, FxFrame &out)
{
    fx_add_shake(out, fx_shake_envelope(ctx.t, 0.f, 0.9f, 0.13f));
    const float u = ctx.t / 0.9f;
    if ((u >= 0.f) && (u <= 1.25f)) {
        const float env = std::sin(kPi * fx_clamp01(u / 1.25f));
        fx_add_screen_wave(out, e.base, (u * 1.3f) - 0.15f, 0.22f, 0.34f * env, 0.f);
    }
}

void look_web(const Look &L, FxFrame &out)
{
    const float te = L.te;
    const float alpha = fx_envelope(te, 0.03f, 1.1f, 0.5f);
    const Vec3 c{ L.foot.x, L.foot.y, 0.03f };
    const float R = 0.46f * L.scale;
    const float spokes = fx_ramp(te, 0.1f, 0.4f);
    const float rings = fx_ramp(te, 0.25f, 0.65f);
    // 上から降りる糸。
    for (int i = 0; i < (L.mini ? 3 : 7); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float a = 2.f * kPi * fx_rand(L.seed, id, 1u);
        const float r = R * fx_rand(L.seed, id, 2u);
        const float drop = fx_ramp(te, 0.f, 0.22f);
        const Vec3 base{ L.foot.x + (std::cos(a) * r), L.foot.y + (std::sin(a) * r), 0.f };
        beam_bar(out, base + Vec3{ 0.f, 0.f, 1.2f - (1.15f * drop) }, base + Vec3{ 0.f, 0.f, 1.2f }, 0.015f, FxColour{ 0.9f, 0.92f, 0.98f }, 1.0f,
            alpha * (1.f - fx_ramp(te, 0.2f, 0.4f)), 1.f, FxPart::Shape);
    }
    // 床の巣：放射状の糸と八角形の輪。
    const int n = 8;
    for (int k = 0; k < n; ++k) {
        const float a = (kPi / 8.f) + (2.f * kPi * static_cast<float>(k) / static_cast<float>(n));
        floor_dots(out, c, c + Vec3{ std::cos(a) * R, std::sin(a) * R, 0.f }, spokes, 0.05f, FxColour{ 0.9f, 0.92f, 0.98f }, 1.2f, alpha);
    }
    for (int ring = 1; ring <= 3; ++ring) {
        const float rr = R * static_cast<float>(ring) / 3.f;
        const float vis = fx_clamp01((rings * 3.f) - static_cast<float>(ring - 1));
        for (int k = 0; k < n; ++k) {
            const float a0 = (kPi / 8.f) + (2.f * kPi * static_cast<float>(k) / static_cast<float>(n));
            const float a1 = a0 + (2.f * kPi / static_cast<float>(n));
            floor_dots(out, c + Vec3{ std::cos(a0) * rr, std::sin(a0) * rr, 0.f }, c + Vec3{ std::cos(a1) * rr, std::sin(a1) * rr, 0.f }, vis, 0.045f,
                FxColour{ 0.85f, 0.88f, 0.95f }, 1.0f, alpha * 0.9f);
        }
    }
    // 粘りつく露。
    for (int i = 0; i < count_of(L, 8); ++i) {
        const std::uint32_t id = static_cast<std::uint32_t>(i);
        const float a = 2.f * kPi * fx_rand(L.seed, id, 3u);
        const float rr = R * (0.2f + (0.75f * fx_rand(L.seed, id, 4u)));
        put(out, c + Vec3{ std::cos(a) * rr, std::sin(a) * rr, 0.03f }, 0.035f, FxColour{ 0.95f, 0.97f, 1.f }, 2.2f * (0.6f + (0.4f * std::sin((te * 6.f) + static_cast<float>(i)))), alpha * rings);
    }
    if (!L.mini) {
        add_light(out, Vec3{ L.foot.x, L.foot.y, 0.4f }, 2.6f, FxColour{ 0.8f, 0.85f, 1.f }, 0.7f * alpha);
    }
}

constexpr FxColour kWhite{ 1.f, 1.f, 1.f };

const Entry kEntries[] = {
    // attr, base, smoke, emissive, tint, tint2, look, seconds, level, flight, beam, beam_seconds, allsight, allsight_seconds, flags
    { "sq_earthquake", { 0.72f, 0.64f, 0.5f }, 0.5f, 0.8f, { 0.46f, 0.41f, 0.34f }, kWhite, look_earthquake, 1.7f, 0, nullptr, nullptr, 0.f, all_earthquake, 1.15f, kWholeArea },
    { "sq_web", { 0.85f, 0.88f, 0.95f }, 0.1f, 1.0f, { 0.95f, 0.97f, 1.f }, kWhite, look_web, 1.9f, 0, nullptr, nullptr, 0.f, nullptr, 0.f, kNone },
};

} // namespace

void fx_register_silq(FxStyleRegistry &registry)
{
    looks::register_entries(registry, kEntries, sizeof(kEntries) / sizeof(kEntries[0]));
}

} // namespace hd2d
