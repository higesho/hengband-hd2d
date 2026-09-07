/*!
 * @file ambience_mix.cpp
 * @brief `ambience_mix.h` の実装。**閾値はここに 1 か所だけ置く。**
 *
 * ## 文字コード
 * この TU は **UTF-8（BOM 付き）**。`hd2d/` は `/execution-charset:utf-8` で組む。
 */
#include "audio/ambience_mix.h"

#include "audio/audio_engine.h"

#include <algorithm>
#include <array>

namespace hd2d::audio {

namespace {

/*!
 * @brief 割合（0..255）→ 音量（0..1）。`lo` 未満は 0、`hi` 以上は `ceiling`。
 * @details 直線で繋ぐ。**下に死んだ帯を作る**のが要点で、そうしないと
 * 「木が 1 マス見えただけで葉ずれが鳴る」——荒野はどの地形にも木が 1〜2/18 混ざるので、
 * 閾値が無いとどこでも森の音になる。
 */
float ramp(int v255, float lo, float hi, float ceiling)
{
    const float v = static_cast<float>(std::clamp(v255, 0, 255)) / 255.f;
    if (v <= lo) {
        return 0.f;
    }
    if (v >= hi) {
        return ceiling;
    }
    return ceiling * ((v - lo) / (hi - lo));
}

//! 層の上限。**ベッドを食わない**ための蓋（層が全部乗るとベッドが聞こえなくなる）。
constexpr float kLayerCeiling = 0.75f;

//! 生き物の層の上限。こちらはもう少し控えめ（鳥や虫は主役ではない）。
constexpr float kLifeCeiling = 0.6f;

//! 「その材料が主役」と言える下限（`dominant_ground` が使う）。
constexpr int kDominantFloor = 26; //!< 255 の約 10%

} // namespace

TimeBand time_band(int day_minute)
{
    if (day_minute < 0) {
        return TimeBand::Day; //!< 分からないものを夜にしない（夜の音が既定になると困る）
    }
    const int m = day_minute % (24 * 60);
    if (m < (6 * 60)) {
        return TimeBand::Night;
    }
    if (m < (9 * 60)) {
        return TimeBand::Dawn;
    }
    if (m < (15 * 60)) {
        return TimeBand::Day;
    }
    if (m < (18 * 60)) {
        return TimeBand::Dusk;
    }
    return TimeBand::Night;
}

const char *time_band_key(TimeBand band)
{
    switch (band) {
    case TimeBand::Dawn:
        return "dawn";
    case TimeBand::Day:
        return "day";
    case TimeBand::Dusk:
        return "dusk";
    case TimeBand::Night:
    default:
        return "night";
    }
}

GroundKind dominant_ground(const AmbienceSurroundings &sur)
{
    if (!sur.valid()) {
        return GroundKind::Unknown;
    }
    /*
     * **壁は数に入れない。**洞窟は壁が半分を超えるので、混ぜると常に壁が勝つ。
     * 壁は「閉塞」の層の材料であって、足元の材料ではない（§8.1）。
     */
    const std::array<std::pair<int, GroundKind>, 9> candidates{ {
        { sur.tree, GroundKind::Forest },
        { sur.deep_water, GroundKind::DeepWater },
        { sur.water, GroundKind::Water },
        { sur.swamp, GroundKind::Swamp },
        { sur.lava, GroundKind::Lava },
        { sur.glass, GroundKind::Glass },
        { sur.rock, GroundKind::Rock },
        { sur.grass, GroundKind::Grass },
        { sur.dirt, GroundKind::Dirt },
    } };
    int best = kDominantFloor;
    GroundKind kind = GroundKind::Stone;
    for (const auto &[value, candidate] : candidates) {
        if (value > best) {
            best = value;
            kind = candidate;
        }
    }
    return kind;
}

const char *ground_key(GroundKind ground)
{
    switch (ground) {
    case GroundKind::Stone:
        return "stone";
    case GroundKind::Grass:
        return "grass";
    case GroundKind::Forest:
        return "forest";
    case GroundKind::Dirt:
        return "dirt";
    case GroundKind::Swamp:
        return "swamp";
    case GroundKind::Water:
        return "water";
    case GroundKind::DeepWater:
        return "deep_water";
    case GroundKind::Lava:
        return "lava";
    case GroundKind::Rock:
        return "rock";
    case GroundKind::Glass:
        return "glass";
    case GroundKind::Unknown:
    default:
        return "";
    }
}

GroundKind ground_from_key(const std::string &key)
{
    static const std::array<std::pair<const char *, GroundKind>, 10> table{ {
        { "stone", GroundKind::Stone },
        { "grass", GroundKind::Grass },
        { "forest", GroundKind::Forest },
        { "dirt", GroundKind::Dirt },
        { "swamp", GroundKind::Swamp },
        { "water", GroundKind::Water },
        { "deep_water", GroundKind::DeepWater },
        { "lava", GroundKind::Lava },
        { "rock", GroundKind::Rock },
        { "glass", GroundKind::Glass },
    } };
    for (const auto &[name, kind] : table) {
        if (key == name) {
            return kind;
        }
    }
    return GroundKind::Unknown;
}

std::vector<AmbienceLayerGain> decide_layers(const AmbienceSurroundings &sur, TimeBand band, bool outdoors)
{
    std::vector<AmbienceLayerGain> out;
    if (!sur.valid()) {
        return out; //!< 数えていない＝層を使わない（ベッド 1 本に落ちる）
    }
    const auto add = [&out](const char *name, float gain) {
        if (gain > 0.f) {
            out.push_back(AmbienceLayerGain{ name, gain });
        }
    };
    /*
     * 閾値の並び。**下の帯（lo）が要点**で、そこを 0 にしないとどこでも全部鳴る。
     * 上の帯（hi）はそこで頭打ちになる割合。実際に遊んで詰めること。
     */
    add("layer_glass", ramp(sur.glass, 0.05f, 0.40f, kLayerCeiling));
    add("layer_grass", ramp(sur.grass, 0.05f, 0.45f, kLayerCeiling));
    add("layer_lava", ramp(sur.lava, 0.02f, 0.30f, kLayerCeiling));
    add("layer_leaves", ramp(sur.tree, 0.03f, 0.35f, kLayerCeiling));
    add("layer_rock", ramp(sur.rock, 0.08f, 0.50f, kLayerCeiling));
    add("layer_surf", ramp(sur.deep_water, 0.05f, 0.50f, kLayerCeiling));
    add("layer_swamp", ramp(sur.swamp, 0.03f, 0.35f, kLayerCeiling));
    add("layer_water", ramp(sur.water, 0.02f, 0.30f, kLayerCeiling));
    /*
     * 閉塞。**壁が多いほど反響が増えて風が減る**という層。
     * 下の帯を 0.35 と高くしてあるのは、部屋の中は壁がそれなりに見えるためで、
     * ここを下げると地上でも家の壁で鳴ってしまう。
     */
    add("layer_enclosed", ramp(sur.wall, 0.35f, 0.80f, kLayerCeiling));
    /*
     * 生き物。**空の下だけ。**洞窟で鳥や虫が鳴くと台無しになる。
     * 草か木のどちらかが在ればよい（多いほうを採る）。
     */
    if (outdoors) {
        const int green = std::max(sur.grass, sur.tree);
        const std::string name = std::string("layer_life_") + time_band_key(band);
        const float gain = ramp(green, 0.05f, 0.40f, kLifeCeiling);
        if (gain > 0.f) {
            out.push_back(AmbienceLayerGain{ name, gain });
        }
    }
    std::sort(out.begin(), out.end(),
        [](const AmbienceLayerGain &a, const AmbienceLayerGain &b) { return a.name < b.name; });
    return out;
}

void AmbienceDirector::configure(float dwell_seconds, float switch_seconds)
{
    this->dwell_s_ = std::max(0.f, dwell_seconds);
    this->switch_s_ = std::max(0.f, switch_seconds);
}

void AmbienceDirector::update(float dt_s, const std::string &wanted_bed,
    const std::vector<AmbienceLayerGain> &wanted_layers, AudioEngine &engine, const std::string &dir)
{
    //! ---- 1. 落ち着き待ち。**答えが変わったら数え直し**（§8.4 の規則 2） ----
    if (!this->pending_valid_ || (wanted_bed != this->pending_bed_)) {
        this->pending_bed_ = wanted_bed;
        this->pending_valid_ = true;
        this->stable_s_ = 0.f;
    } else {
        this->stable_s_ += dt_s;
    }

    //! ---- 2. 入れ替えの残り時間を進める（§8.4 の規則 1） ----
    if (this->switch_left_ > 0.f) {
        this->switch_left_ = std::max(0.f, this->switch_left_ - dt_s);
    }

    /*
     * ---- 3. 条件が揃ったときだけベッドを動かす ----
     *
     * **入れ替えの最中は動かさない。**ここを外すと、1 歩ごとに新しいベッドが
     * 上書きされて、どれも鳴り切らないまま次へ行く＝「寸断」になる。
     */
    const bool settled = (this->stable_s_ >= this->dwell_s_);
    const bool idle = (this->switch_left_ <= 0.f);
    if (settled && idle && (this->pending_bed_ != this->current_bed_)) {
        engine.play_bed(this->pending_bed_, dir);
        this->current_bed_ = this->pending_bed_;
        this->switch_left_ = this->switch_s_;
    }

    /*
     * ---- 4. 層は毎フレーム目標を渡す ----
     *
     * **待たせない。**層は入れ替わらず音量が動くだけなので、目標が 1 歩ごとに
     * 揺れても `AudioEngine` の速さの制限（`kLayerFadeSeconds`）で均される。
     */
    std::vector<std::string> keep;
    keep.reserve(wanted_layers.size());
    for (const AmbienceLayerGain &layer : wanted_layers) {
        engine.set_layer(layer.name, dir, layer.gain);
        keep.push_back(layer.name);
    }
    engine.fade_out_layers_except(keep);
}

} // namespace hd2d::audio
