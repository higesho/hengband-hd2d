#pragma once
/*!
 * @file ambience_table.h
 * @brief 場面 → 環境音のベッド。**純粋な表引きだけ。**
 *
 * ## なぜコアを触らずに済むか
 * 場面を選ぶのに要るものは**もうフレームで届いている**（`FloorIdentity` と
 * `LightingState`）。だからこの表は画面側だけで完結し、**どのコアでも同じように効く**
 * （コア名の分岐は書かない。設計 §1 制約 1）。
 *
 * | 届いているもの | ここでの名前 |
 * |---|---|
 * | `FloorIdentity.kind`（1=地上 / 2=ダンジョン / 3=クエスト / 4=闘技場） | `floor_kind` |
 * | `.dungeon_id` / `.dun_level` | `dungeon_id` / `depth` |
 * | `.town_id` / `.wild_mode` | `town_id` / `wild` |
 * | `LightingState.day_minute` | `night`（夜か） |
 *
 * ## 規則は上から順に見て、**最初に当たったもの**を採る
 * `dungeon_styles.jsonc` と同じ流儀。**当たらなければ「鳴らさない」**に落ちる——
 * 素材が 1 つも無い状態でも器だけ先に組めるようにするため。
 *
 * ```jsonc
 * { "rules": [
 *     { "when": { "town": 1 },                 "bed": "town_day" },
 *     { "when": { "floor": 2, "depth_min": 1 }, "bed": "cave" }
 * ] }
 * ```
 *
 * @note **音量も曲名もここでは決めない。**鳴らすのは `AudioEngine` の仕事で、
 * ここは「この場面はどのベッドか」だけを答える。分けてあるのは、表の検査に
 * 音の装置を要らなくするため（`--ui-check` は音を鳴らさずに表だけ突ける）。
 */

#include <optional>
#include <string>
#include <vector>

namespace hd2d::audio {

//! いまどこに居るか。**フレームから写すだけ**（ここで判断はしない）。
struct AmbienceScene {
    int floor_kind{ 0 }; //!< `FloorKind`（0=不明 1=地上 2=ダンジョン 3=クエスト 4=闘技場）
    int dungeon_id{ 0 };
    int depth{ 0 }; //!< `dun_level`
    int town_id{ 0 }; //!< 町の中でなければ 0
    bool wild{ false }; //!< 広域マップ
    int day_minute{ -1 }; //!< 0..1439。負なら「時刻が分からない」
    /*!
     * @brief 足元の材料（`GroundKind` を `int` にしたもの。0 = 数えていない）。
     * @details `ambience_mix.h` の `dominant_ground()` が周囲の内訳から決める。
     * **ここが `ambience.jsonc` の `ground` と突き合わされる**（§8.1）。
     * `int` にしてあるのは、この表に `ambience_mix.h` を持ち込まないため
     * （表は「音の装置も地形の知識も要らない純関数」であり続けたい）。
     */
    int ground{ 0 };
    //! 時刻の帯（`TimeBand` を `int` にしたもの）。負なら「問わない」。
    int time_band{ -1 };
};

/*!
 * @brief 規則 1 本。**書いていない条件は問わない。**
 * @details 条件がすべて当たった規則の `bed` を採る。`bed` が空文字なら
 * **「この場面では鳴らさない」**という明示（既定へ落ちるのとは違う——
 * 後ろにもっと広い規則が並んでいても、そこで止まる）。
 */
struct AmbienceRule {
    std::optional<int> floor_kind;
    std::optional<int> dungeon_id;
    std::optional<int> town_id;
    std::optional<int> depth_min;
    std::optional<int> depth_max;
    std::optional<bool> wild;
    std::optional<bool> night;
    //! 足元の材料（`ground_from_key()` で綴りから引いた値）。
    std::optional<int> ground;
    //! 時刻の帯（`dawn` / `day` / `dusk` / `night`）。**`night`（真偽）とは別の行**。
    std::optional<int> time_band;
    std::string bed;
};

/*!
 * @brief 夜か（`day_minute` から）。**6:00 未満と 18:00 以降を夜**とする。
 * @details 境目を 1 か所にするための関数。時刻が分からない（負）ときは昼とみなす
 * ——分からないものを夜にすると、コアが時刻を送ってこない場面で夜の音になる。
 */
bool is_night(int day_minute);

class AmbienceTable {
public:
    /*!
     * @brief `assets/audio/ambience.jsonc` を読む。
     * @return 読めたら true。**無いのは失敗ではない**（環境音が鳴らないだけ）
     * @param log 何本読んだか／なぜ読めなかったかを入れる（画面には出さない。stderr 用）
     */
    bool load(const std::string &dir, std::string *log = nullptr);
    //! この場面のベッドの名前。**当たらなければ空**（＝鳴らさない）。
    std::string bed_for(const AmbienceScene &scene) const;
    //! 読めた規則の本数（検査が「表が空でない」を見るのに使う）。
    std::size_t rule_count() const { return this->rules_.size(); }
    //! 表を直に入れる（検査用。ファイルを置かずに引きを確かめられるようにする）。
    void set_rules(std::vector<AmbienceRule> rules) { this->rules_ = std::move(rules); }

private:
    std::vector<AmbienceRule> rules_;
};

} // namespace hd2d::audio
