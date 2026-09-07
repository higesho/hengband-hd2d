#include "ui/combat_fx_view.h"

#include "render/math3d.h"
#include "ui/ui_paint.h"

#include <algorithm>
#include <cmath>

namespace hd2d {

FxColor CombatFxView::color_of(CombatFxElement element)
{
    /*
     * 2026-08-12 に決めた「ダメージの属性により色を変える。物理は白フラッシュ、
     * 炎は赤フラッシュ等」。**束の数は 10 まで**（設計書 §8）——全部の `AttributeType` に
     * 色を振ると保守が破綻するうえ、遊んでいて区別がつかない。
     */
    switch (element) {
    case CombatFxElement::Fire:
        return { 1.00f, 0.28f, 0.12f };
    case CombatFxElement::Cold:
        return { 0.55f, 0.85f, 1.00f };
    case CombatFxElement::Elec:
        return { 1.00f, 0.92f, 0.35f };
    case CombatFxElement::Acid:
        return { 0.45f, 0.95f, 0.35f };
    case CombatFxElement::Poison:
        return { 0.70f, 0.35f, 0.95f };
    case CombatFxElement::Dark:
        return { 0.35f, 0.16f, 0.50f };
    case CombatFxElement::Light:
        return { 1.00f, 1.00f, 0.85f };
    case CombatFxElement::Chaos:
        return { 0.95f, 0.45f, 0.95f };
    case CombatFxElement::Psy:
        return { 1.00f, 0.60f, 0.80f };
    /*
     * ---- 幻想蛮怒だけの 2 束----
     * どちらも**コア自身の宣言**（`gensoband/lib/pref/spell-xx.prf` の `Z:` 行）から採った。
     */
    //! 時空。コアの宣言は `d`（黒）。**黒はフラッシュに使えない**ので、いちばん暗い青へ寄せる。
    //! 闇（0.35,0.16,0.50）と紛れないよう、**紫ではなく青の側**に置いてある。
    case CombatFxElement::Spacetime:
        return { 0.20f, 0.33f, 0.60f };
    //! 狂気。コアの宣言は `RD`（明るい赤と暗い灰の交互）。火（1.00,0.28,0.12）より
    //! **暗く、橙に寄せない**——並べたときに火と区別が付かなくなるため。
    case CombatFxElement::Insanity:
        return { 0.72f, 0.20f, 0.22f };
    case CombatFxElement::Physical:
    default:
        return { 1.00f, 1.00f, 1.00f };
    }
}

uint32_t CombatFxView::life_ms_of(CombatFxKind kind)
{
    switch (kind) {
    case CombatFxKind::HitMonster:
        return SPARK_MS;
    case CombatFxKind::Bolt:
        return BOLT_MS;
    case CombatFxKind::HitPlayer:
    default:
        return FLASH_MS;
    }
}

void CombatFxView::push(const std::vector<CombatFxEvent> &events, uint32_t now)
{
    this->now_ms = now;
    for (const auto &event : events) {
        if (this->actives.size() >= MAX_ACTIVES) {
            this->actives.erase(this->actives.begin());
        }
        this->actives.push_back(Active{ event, now });
    }
}

void CombatFxView::update(uint32_t now)
{
    this->now_ms = now;
    const auto expired = [now](const Active &active) {
        // 単調時計の巻き戻りに耐える（差を符号付きで見る）。
        const auto elapsed = static_cast<int32_t>(now - active.start_ms);
        return (elapsed < 0) || (elapsed >= static_cast<int32_t>(life_ms_of(active.event.kind)));
    };
    this->actives.erase(std::remove_if(this->actives.begin(), this->actives.end(), expired), this->actives.end());
}

float CombatFxView::progress_of(const Active &active) const
{
    const auto life = static_cast<float>(life_ms_of(active.event.kind));
    const auto elapsed = static_cast<float>(static_cast<int32_t>(this->now_ms - active.start_ms));
    return std::clamp(elapsed / std::max(1.f, life), 0.f, 1.f);
}

bool CombatFxView::flash(FxColor &color, float &alpha) const
{
    auto found = false;
    auto best = 0.f;
    for (const auto &active : this->actives) {
        if (active.event.kind != CombatFxKind::HitPlayer) {
            continue;
        }

        /*
         * 強さは「減った HP の割合」。**そのまま濃さにすると、かすり傷でも画面が染まる。**
         * 下限を少し持たせつつ、上限は 0.55 で止める（真っ赤で前が見えないと理不尽になる）。
         */
        const auto weight = 0.18f + 0.37f * std::clamp(active.event.intensity * 2.2f, 0.f, 1.f);
        const auto fade = 1.f - this->progress_of(active);
        const auto value = weight * fade * fade; // 立ち上がりを強く、引きを速く
        if (value <= best) {
            continue;
        }

        best = value;
        color = color_of(active.event.element);
        found = true;
    }

    alpha = best;
    return found && (best > 0.001f);
}

void CombatFxView::shake_offset(float &dx, float &dz) const
{
    dx = 0.f;
    dz = 0.f;
    for (const auto &active : this->actives) {
        if (active.event.kind != CombatFxKind::HitPlayer) {
            continue;
        }

        const auto progress = this->progress_of(active);
        if (progress >= 1.f) {
            continue;
        }

        /*
         * 減衰する横揺れ。**乱数を使わない**——毎フレーム引くと、フレーム落ちしたときに
         * 揺れ幅が跳ねて見苦しい。位相を時間から作れば、何 fps でも同じ揺れになる。
         */
        const auto amplitude = 0.30f * std::clamp(active.event.intensity * 2.0f, 0.15f, 1.f) * (1.f - progress);
        const auto phase = progress * 26.f;
        dx += amplitude * std::sin(phase);
        dz += amplitude * 0.6f * std::sin(phase * 1.7f);
    }
}

std::vector<FxSpark> CombatFxView::sparks() const
{
    std::vector<FxSpark> out;
    for (const auto &active : this->actives) {
        if (active.event.kind != CombatFxKind::HitMonster) {
            continue;
        }

        FxSpark spark;
        spark.cell_y = static_cast<float>(active.event.y);
        spark.cell_x = static_cast<float>(active.event.x);
        spark.color = color_of(active.event.element);
        spark.progress = this->progress_of(active);
        spark.intensity = std::clamp(active.event.intensity * 2.5f, 0.25f, 1.f);
        out.push_back(spark);
    }

    return out;
}

std::vector<FxBolt> CombatFxView::bolts() const
{
    std::vector<FxBolt> out;
    for (const auto &active : this->actives) {
        if (active.event.kind != CombatFxKind::Bolt) {
            continue;
        }

        FxBolt bolt;
        bolt.from_y = static_cast<float>(active.event.src_y);
        bolt.from_x = static_cast<float>(active.event.src_x);
        bolt.to_y = static_cast<float>(active.event.y);
        bolt.to_x = static_cast<float>(active.event.x);
        bolt.color = color_of(active.event.element);
        bolt.progress = this->progress_of(active);
        out.push_back(bolt);
    }

    return out;
}

namespace {

/*!
 * @brief マスの中心を画面の画素へ落とす
 * @return 画面の手前にあれば true（後ろ・真横は false。**描いてはいけない**）
 * @details 行列は列優先（`math3d.h` の `operator*` がそう組んである）。
 * 落とし先は `scene` の中の座標＝**窓の座標**（`scene.x/y` を足した後）。
 */
bool project_cell_at_height(const Camera &camera, const RectPx &scene, float cell_x, float cell_y, float height,
    float &out_x, float &out_y)
{
    if (scene.empty()) {
        return false;
    }

    const Mat4 vp = camera.view_projection();
    const float world[4] = { cell_x + 0.5f, cell_y + 0.5f, height, 1.f };
    float clip[4] = { 0.f, 0.f, 0.f, 0.f };
    for (int row = 0; row < 4; ++row) {
        float sum = 0.f;
        for (int k = 0; k < 4; ++k) {
            sum += vp.m[(k * 4) + row] * world[k];
        }
        clip[row] = sum;
    }

    if (clip[3] <= 0.0001f) {
        return false; // カメラの後ろ。落とすと画面の反対側へ飛ぶ
    }

    const float ndc_x = clip[0] / clip[3];
    const float ndc_y = clip[1] / clip[3];
    out_x = static_cast<float>(scene.x) + ((ndc_x * 0.5f) + 0.5f) * static_cast<float>(scene.w);
    // GL の NDC は上が +1。画面の座標は下が大きいので反転する。
    out_y = static_cast<float>(scene.y) + ((-ndc_y * 0.5f) + 0.5f) * static_cast<float>(scene.h);
    return true;
}

//! 地面（高さ 0）のマスの中心。
bool project_cell(const Camera &camera, const RectPx &scene, float cell_x, float cell_y, float &out_x, float &out_y)
{
    return project_cell_at_height(camera, scene, cell_x, cell_y, 0.f, out_x, out_y);
}

/*!
 * @brief そのマスが画面で何画素あるか（＝モンスターの絵の大きさ）
 * @return 半径にあたる画素。落とせなければ 0
 * @details **画素で決め打ちにしてはいけない。** 見下ろしと一人称では 1 マスの見かけが
 * 何倍も違い、決め打ちだと一人称で豆粒になる（2026-08-12 に気づいた
 * 「一人称視点だとおそらく小さすぎて気付けない。モンスタータイルいっぱいで表現すべき」）。
 * 隣のマスと、1 マスぶん上の点との距離を測って、大きい方を採る——奥行きのある向きだと
 * 横幅が潰れるので、縦（高さ 1 マス）も見ないと平たくなる。
 */
float cell_half_px(const Camera &camera, const RectPx &scene, float cell_x, float cell_y)
{
    float cx = 0.f;
    float cy = 0.f;
    if (!project_cell(camera, scene, cell_x, cell_y, cx, cy)) {
        return 0.f;
    }

    auto span = 0.f;
    float nx = 0.f;
    float ny = 0.f;
    if (project_cell(camera, scene, cell_x + 1.f, cell_y, nx, ny)) {
        span = std::max(span, std::hypot(nx - cx, ny - cy));
    }
    if (project_cell(camera, scene, cell_x, cell_y + 1.f, nx, ny)) {
        span = std::max(span, std::hypot(nx - cx, ny - cy));
    }
    if (project_cell_at_height(camera, scene, cell_x, cell_y, 1.f, nx, ny)) {
        span = std::max(span, std::hypot(nx - cx, ny - cy));
    }

    // 1 マスぶんの見かけの「半径」。両端で見失わないよう最低限は確保する。
    return std::clamp(span * 0.5f, 6.f, 400.f);
}

//! 中心と大きさから矩形を作る（点の演出用）。
RectPx centered_rect(float cx, float cy, float half)
{
    RectPx r;
    r.x = static_cast<int>(std::lround(cx - half));
    r.y = static_cast<int>(std::lround(cy - half));
    r.w = std::max(1, static_cast<int>(std::lround(half * 2.f)));
    r.h = r.w;
    return r;
}

} // namespace

void draw_combat_fx(UiPaint &paint, const CombatFxView &fx, const Camera &camera,
    const RectPx &scene, bool flash_enabled)
{
    if (scene.empty()) {
        return;
    }

    /*
     * 飛道。撃ったマスから着弾側へ、頭が走る。**線を全部引かない**——
     * 引くと弾が「もう当たっている」ように見えて、避けられたのかどうかが読めない。
     * 頭とその少し後ろだけを出して、進んだぶんだけ見せる。
     */
    for (const auto &bolt : fx.bolts()) {
        const auto head = std::clamp(bolt.progress, 0.f, 1.f);
        const auto tail = std::max(0.f, head - 0.35f);
        for (int i = 0; i < 5; ++i) {
            const auto t = tail + ((head - tail) * (static_cast<float>(i) / 4.f));
            const auto cell_x = bolt.from_x + ((bolt.to_x - bolt.from_x) * t);
            const auto cell_y = bolt.from_y + ((bolt.to_y - bolt.from_y) * t);
            float sx = 0.f;
            float sy = 0.f;
            if (!project_cell(camera, scene, cell_x, cell_y, sx, sy)) {
                continue;
            }

            // 後ろほど細く薄く（尾を引いて見える）。太さもマスの見かけに合わせる。
            const auto weight = static_cast<float>(i + 1) / 5.f;
            const auto tile_half = cell_half_px(camera, scene, cell_x, cell_y);
            const auto half = tile_half * (0.15f + (0.35f * weight));
            const PaintColor color{ bolt.color.r, bolt.color.g, bolt.color.b, 0.45f * weight };
            paint.rect(centered_rect(sx, sy, half), color);
        }
    }

    /*
     * 命中。当てたマスで弾ける。**広がりながら薄くなる**（同じ大きさで消えると
     * 「点滅した」だけに見えて、当たったのか光っただけなのか分からない）。
     */
    for (const auto &spark : fx.sparks()) {
        float sx = 0.f;
        float sy = 0.f;
        if (!project_cell(camera, scene, spark.cell_x, spark.cell_y, sx, sy)) {
            continue;
        }

        /*
         * 大きさは**そのマスの見かけ**に合わせる（2026-08-12 に決めた
         * 「モンスタータイルいっぱいで表現すべき」）。画素で決め打つと一人称で豆粒になる。
         * 濃さは薄め——**相手が見えなくなっては本末転倒**（同「半透明がいい」）。
         */
        const auto tile_half = cell_half_px(camera, scene, spark.cell_x, spark.cell_y);
        if (tile_half <= 0.f) {
            continue;
        }

        const auto fade = 1.f - spark.progress;
        // マスいっぱいから少しだけ広がって消える。強い一撃ほど大きい。
        const auto half = tile_half * (0.85f + (0.35f * spark.progress)) * (0.8f + (0.4f * spark.intensity));
        const PaintColor color{ spark.color.r, spark.color.g, spark.color.b, 0.30f * fade };
        paint.rect(centered_rect(sx, sy, half), color);
        // 芯。薄い外側だけだと当たった瞬間が読めないので、中央にもう一段だけ重ねる。
        const PaintColor core{ spark.color.r, spark.color.g, spark.color.b, 0.22f * fade * fade };
        paint.rect(centered_rect(sx, sy, half * 0.5f), core);
    }

    // 被弾。地図の矩形いっぱいに敷く。**一番上**（点や線より前）に出す。
    if (flash_enabled) {
        FxColor color;
        float alpha = 0.f;
        if (fx.flash(color, alpha)) {
            paint.rect(scene, PaintColor{ color.r, color.g, color.b, alpha });
        }
    }
}

} // namespace hd2d
