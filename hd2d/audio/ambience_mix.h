#pragma once
/*!
 * @file ambience_mix.h
 * @brief 周囲の地形 → **ベッドと重ねる層**。
 *
 * ## なぜ層にしたのか
 * 場面ごとに 1 本ずつ作ると、地上だけで 10 場面 × 時刻 4 帯 ＝ 40 本になり、しかも
 * 「森だが隣が海」「草原だが川沿い」という**混じり**が出せない。
 * **ベッド 1 本 ＋ 重ねる層**にすると、素材 37 本で全部の組み合わせが出る（§8.8）。
 *
 * ## ここは純関数だけ
 * `ambience_table.h` と同じで、**音の装置を要らない**。だから `--ui-check` が
 * 机の上で「この周囲ならこの層がこの音量」を突ける。鳴らすのは `AudioEngine` と
 * `AmbienceDirector` の仕事。
 *
 * ## 切り替えの寸断を作らない（2026-08-21 に決めた）
 * > マスの移動毎に切り替えると寸断で不自然になるので、切り替わりはタイムラグを許容し
 * > フェードアウトとフェードインを完了させながら変化させていく。
 *
 * `AmbienceDirector` が 3 つの規則で守る（§8.4）。
 * 1. **入れ替えの最中は次の要求を受け付けない**（積んでおく）
 * 2. **同じ答えが一定時間続かないと動かない**（落ち着き待ち）
 * 3. 層は入れ替えず**音量だけ**を動かす（速さの制限は `AudioEngine` 側）
 */

#include <string>
#include <vector>

namespace hd2d::audio {

class AudioEngine;

/*!
 * @brief 時刻の帯（§8.3）。**夜の境目はコアの `is_daytime()` と同じ**にしてある。
 * @details `world.cpp` の日中判定は 1 日 10,000 ターンの前半で、`extract_date_time()` を
 * 通すと **06:00〜18:00** になる。ここをずらすと**画は昼なのに音は夜**という
 * 食い違いが出る。朝と夕は日中を 3 つに割ったもので、画には影響しない。
 */
enum class TimeBand {
    Dawn, //!< 06:00〜09:00
    Day, //!< 09:00〜15:00
    Dusk, //!< 15:00〜18:00
    Night, //!< 18:00〜06:00
};

//! 時刻（分）→ 帯。**負（時刻が分からない）は昼**（分からないものを夜にしない）。
TimeBand time_band(int day_minute);
//! 帯の名前（素材名に使う。`layer_life_night` の `night`）。
const char *time_band_key(TimeBand band);

/*!
 * @brief 周囲の地形の内訳。**フレームの `SurroundingsView` を写しただけ**（0..255）。
 * @details ここで持ち直しているのは、この TU に `frame/game_frame.h` を持ち込まないため。
 * 検査が手で作れる形にしておきたい（`AmbienceScene` と同じ考え方）。
 */
struct AmbienceSurroundings {
    int grass{ 0 };
    int tree{ 0 };
    int dirt{ 0 };
    int swamp{ 0 };
    int water{ 0 };
    int deep_water{ 0 };
    int lava{ 0 };
    int rock{ 0 };
    int glass{ 0 };
    int wall{ 0 };
    int radius{ 0 };
    int counted{ 0 };

    //! 数えた結果が入っているか。**偽なら層を使わない**（ベッド 1 本に落ちる）。
    bool valid() const { return (this->radius > 0) && (this->counted > 0); }
};

//! いちばん多い足元の材料。**ベッドを選ぶのに使う**（`ambience.jsonc` の `ground`）。
enum class GroundKind {
    Unknown = 0, //!< 数えていない
    Stone, //!< ただの床・岩盤（ダンジョンの既定）
    Grass,
    Forest, //!< 立ち木が多い
    Dirt,
    Swamp,
    Water, //!< 浅い水
    DeepWater,
    Lava,
    Rock, //!< 山・岩
    Glass,
};

/*!
 * @brief いちばん多い材料を選ぶ。
 * @details **どれも少なければ `Stone`**（洞窟の床は材料の名前が付かないため）。
 * 数えていなければ `Unknown`。
 */
GroundKind dominant_ground(const AmbienceSurroundings &sur);
//! 材料の名前（`ambience.jsonc` の `ground` に書く綴り）。
const char *ground_key(GroundKind ground);
//! 綴り → 材料（読めなければ `Unknown`）。
GroundKind ground_from_key(const std::string &key);

//! 層 1 本。**名前は素材の名前そのもの**（`layer_grass` → `layer_grass.wav`）。
struct AmbienceLayerGain {
    std::string name;
    float gain{ 0.f };
};

/*!
 * @brief 周囲と時刻から**重ねる層**を決める（純関数）。
 * @param outdoors 空の下か（地上・広域）。**生き物の層はここが真のときだけ**乗る
 * @return 音量が 0 より大きい層だけ。**順番は名前順で安定**（検査が読みやすい）
 */
std::vector<AmbienceLayerGain> decide_layers(const AmbienceSurroundings &sur, TimeBand band, bool outdoors);

/*!
 * @brief 切り替えの段取り役（§8.4）。**寸断を作らないための待ち**をここに閉じる。
 *
 * @details ベッドと層で扱いが違う。
 * * **ベッド**は入れ替わる（クロスフェード）。だから「落ち着き待ち」と
 *   「入れ替え中は受け付けない」の 2 つで守る
 * * **層**は入れ替わらない。音量が動くだけなので、目標を毎フレーム渡してよい
 *   （速さの制限は `AudioEngine::update` が持つ）
 */
class AmbienceDirector {
public:
    //! 落ち着き待ちの秒（同じ答えがこれだけ続いてから動く）と、入れ替えにかける秒。
    void configure(float dwell_seconds, float switch_seconds);

    /*!
     * @brief 毎フレーム呼ぶ。
     * @param wanted_bed いま鳴らしたいベッド（空なら「鳴らさない」）
     * @param wanted_layers いま鳴らしたい層とその音量
     * @param dir wav の置き場
     */
    void update(float dt_s, const std::string &wanted_bed,
        const std::vector<AmbienceLayerGain> &wanted_layers, AudioEngine &engine, const std::string &dir);

    //! 実際に採用しているベッド（要求とは別。待っている間は古いまま）。
    const std::string &bed() const { return this->current_bed_; }
    //! 積んである要求が待っている秒（検査用）。
    float waited_seconds() const { return this->stable_s_; }
    //! 入れ替えの最中か（検査用）。
    bool switching() const { return this->switch_left_ > 0.f; }

private:
    float dwell_s_{ 1.5f };
    float switch_s_{ 1.2f };
    std::string current_bed_; //!< 実際に鳴らしている
    std::string pending_bed_; //!< 積んである要求
    bool pending_valid_{ false };
    float stable_s_{ 0.f };
    float switch_left_{ 0.f };
};

} // namespace hd2d::audio
