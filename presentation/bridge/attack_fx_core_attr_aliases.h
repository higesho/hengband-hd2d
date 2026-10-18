/*!
 * @file attack_fx_core_attr_aliases.h
 * @brief 変愚以外のコア（Frox・幻想蛮怒・Sil-Q・TOband）の属性を、画面側の「攻撃の見た目」の名前に直す表
 * @details
 * 変愚の属性の名前の一覧（`attack_fx_attr_names.h`）に無い属性が、Frox・幻想蛮怒・Sil-Q・TOband にはある。
 * 画面側（`hd2d/render/fx_styles/`）は、それらに **専用の見た目** を持つ（Frox は `fc_` 、幻想蛮怒は `gb_`、Sil-Q は `sq_` を頭に付けた名前）か、
 * すでにある見た目へ **寄せる**（決めたこと 2026-10-11「案どおり寄せる」）。
 * コアのアダプタは、原作の属性（Frox の `GF_xxx`・幻想蛮怒の `GF_xxx`）を小文字にした名前（`GF_` を除く。例 `storm`・`tsukumo_storm`）を
 * ここで引いて、`NamedRecorder::add_projection` に渡す属性の名前にする。
 *
 * ## 引き方（コア名 → 見た目の名前）
 * 1. 専用の見た目の表（`kFroxOwnLooks` / `kGensobandOwnLooks`）にあれば、その見た目（`fc_xxx` / `gb_xxx`）
 * 2. 寄せる表（`kFroxAliases` / `kGensobandAliases`）にあれば、寄せ先の見た目（変愚と共通の名前）
 * 3. どちらにも無ければ、コア名がそのまま変愚の名前と同じ（`fire`・`pois`・`old_sleep` など）
 *
 * 関数 `frox_look_name` / `gensoband_look_name` / `silq_look_name` / `toband_look_name` がこの順で引く（TOband は専用の見た目が無いので寄せる表だけ）。どの表にも無い名前は、3. のとおりそのまま返す。
 * 変愚と同名かどうかは、呼ぶ側（またはアダプタの検査）が `attack_fx_attr_names.h` で確かめる。
 * 原作の属性の全数が、専用・寄せる・同名のどれかへつながっていることは `tools/attack_fx_core_names_check.py` が数える
 * （Frox は 135 個＝専用 23・寄せる 20・同名 92。幻想蛮怒は 170 個＝専用 41・寄せる 36・同名 93。Sil-Q は 27 個＝専用 2・寄せる 11・同名 14。TOband は 103 個＝専用 0・寄せる 30・同名 73）。
 *
 * 画面側の検査（`--attack-fx-check`）は、この表の見た目の名前がすべて登録表にあること、寄せ先もすべて登録表にあることを確かめる。
 * 依存はこのヘッダだけ（ゲームの型を何も使わない）。UTF-8 BOM 付き。
 */
#pragma once

#include <cstddef>
#include <cstring>

namespace attack_fx {

//! コア名（`GF_` を除いた小文字）と、見た目の名前の対。
struct CoreAttrAlias {
    const char *core_name;
    const char *look_name;
};

/*!
 * @name Frox（`frox/src/gf.h` の `GF_xxx`）
 * @{
 */
//! 専用の見た目を持つ属性（23 個）。見た目の名前は `fc_` + コア名。
inline constexpr CoreAttrAlias kFroxOwnLooks[] = {
    { "storm", "fc_storm" },
    { "rock", "fc_rock" },
    { "amnesia", "fc_amnesia" },
    { "blind", "fc_blind" },
    { "paralysis", "fc_paralysis" },
    { "antimagic", "fc_antimagic" },
    { "unholy_word", "fc_unholy_word" },
    { "unlife", "fc_unlife" },
    { "web", "fc_web" },
    { "isolation", "fc_isolation" },
    { "quake", "fc_quake" },
    { "death_touch", "fc_death_touch" },
    { "steal", "fc_steal" },
    { "chicken", "fc_chicken" },
    { "bomb", "fc_bomb" },
    { "air", "fc_air" },
    { "gain_exp", "fc_gain_exp" },
    { "eldritch", "fc_eldritch" },
    { "eldritch_stun", "fc_eldritch_stun" },
    { "eldritch_drain", "fc_eldritch_drain" },
    { "eldritch_dispel", "fc_eldritch_dispel" },
    { "eldritch_confuse", "fc_eldritch_confuse" },
    { "eldritch_howl", "fc_eldritch_howl" },
};

//! 既存の見た目へ寄せる属性（20 個）。
inline constexpr CoreAttrAlias kFroxAliases[] = {
    { "arrow", "missile" },
    { "psi_ego_whip", "psi" },
    { "psi_storm", "psi" },
    { "psi_brain_smash", "brain_smash" },
    { "subjugation", "domination" },
    { "old_drain", "hypodynamia" },
    { "draining_touch", "hypodynamia" },
    { "make_glyph", "make_rune_protection" },
    { "control_pact_monster", "control_demon" },
    { "control_living", "charm_living" },
    { "charm_ring_bearer", "charm" },
    { "pharaohs_curse", "cause_4" },
    { "slow", "old_slow" },
    { "baby_slow", "old_slow" },
    { "mana_clash", "mana" },
    { "blood", "blood_curse" },
    { "remove_obstacle", "kill_wall" },
    { "entomb", "stone_wall" },
    { "inert", "inertial" },
    { "water2", "water_flow" },
};
/*! @} */

/*!
 * @name Sil-Q（`silq/src/defines.h` の `GF_xxx`。27 個）
 * @{
 */
//! 専用の見た目を持つ属性（2 個）。見た目の名前は `sq_` + コア名。
inline constexpr CoreAttrAlias kSilqOwnLooks[] = {
    { "earthquake", "sq_earthquake" },
    { "web", "sq_web" },
};

//! 既存の見た目へ寄せる属性（11 個）。ほかの 14 個は変愚と同名（acid・elec・fire・cold・pois・dark・dark_weak・sound・confusion・kill_wall・kill_door・kill_trap・away_all・identify）。
inline constexpr CoreAttrAlias kSilqAliases[] = {
    { "nothing", "none" },
    { "hurt", "attack" },
    { "arrow", "missile" },
    { "boulder", "shards" },
    { "light", "lite" },
    { "slow", "old_slow" },
    { "speed", "old_speed" },
    { "sleep", "old_sleep" },
    { "fear", "turn_all" },
    { "heal", "old_heal" },
    { "lock_door", "jam_door" },
};
/*! @} */

/*!
 * @name 幻想蛮怒（`gensoband/src/defines.h` の `GF_xxx`）
 * @{
 */
//! 専用の見た目を持つ属性（41 個）。見た目の名前は `gb_` + コア名。
inline constexpr CoreAttrAlias kGensobandOwnLooks[] = {
    { "tsukumo_storm", "gb_tsukumo_storm" },
    { "yukari_storm", "gb_yukari_storm" },
    { "kinkakuji", "gb_kinkakuji" },
    { "tornado", "gb_tornado" },
    { "windcutter", "gb_windcutter" },
    { "banki_beam", "gb_banki_beam" },
    { "banki_beam2", "gb_banki_beam2" },
    { "seiran_beam", "gb_seiran_beam" },
    { "rainbow", "gb_rainbow" },
    { "lunatic_torch", "gb_lunatic_torch" },
    { "redeye", "gb_redeye" },
    { "kyuttoshitedoka_n", "gb_kyuttoshitedoka_n" },
    { "ryuuri", "gb_ryuuri" },
    { "slow_twine", "gb_slow_twine" },
    { "possession", "gb_possession" },
    { "soulsteal", "gb_soulsteal" },
    { "nightmare", "gb_nightmare" },
    { "train", "gb_train" },
    { "northern", "gb_northern" },
    { "n_e_p", "gb_n_e_p" },
    { "houtou", "gb_houtou" },
    { "satori", "gb_satori" },
    { "kokoro", "gb_kokoro" },
    { "moses", "gb_moses" },
    { "redmagic", "gb_redmagic" },
    { "gungnir", "gb_gungnir" },
    { "throw_plate", "gb_throw_plate" },
    { "blazingstar", "gb_blazingstar" },
    { "soulsculpture", "gb_soulsculpture" },
    { "yaku", "gb_yaku" },
    { "special_shot", "gb_special_shot" },
    { "youmu", "gb_youmu" },
    { "brain_finger_print", "gb_brain_finger_print" },
    { "kanameishi", "gb_kanameishi" },
    { "steam", "gb_steam" },
    { "super_ego", "gb_super_ego" },
    { "dig_oil", "gb_dig_oil" },
    { "alcohol", "gb_alcohol" },
    { "pit_fall", "gb_pit_fall" },
    { "berserk", "gb_berserk" },
    { "activ_trap", "gb_activ_trap" },
};

//! 既存の見た目へ寄せる属性。
inline constexpr CoreAttrAlias kGensobandAliases[] = {
    { "arrow", "missile" },
    { "make_blizzard", "ice" },
    { "make_storm", "water" },
    { "inact", "inertial" },
    { "make_acid_puddle", "acid" },
    { "make_poison_puddle", "pois" },
    { "dec_atk", "old_slow" },
    { "dec_def", "old_slow" },
    { "dec_mag", "old_slow" },
    { "dec_all", "old_slow" },
    { "no_move", "stasis" },
    { "stasis_dragon", "stasis" },
    { "stasis_living", "stasis" },
    { "old_drain", "hypodynamia" },
    { "make_glyph", "make_rune_protection" },
    { "distortion", "gravity" },
    { "cosmic_horror", "chaos" },
    { "punish_1", "cause_1" },
    { "punish_2", "cause_2" },
    { "punish_3", "cause_3" },
    { "punish_4", "cause_4" },
    { "pollute", "nuke" },
    { "charm_paladin", "charm" },
    { "charm_c", "charm" },
    { "charm_j", "charm" },
    { "charm_b", "charm" },
    { "control_living", "charm_living" },
    { "control_fish", "control_animal" },
    { "disp_kwai", "disp_all" },
    { "disp_bugs", "disp_all" },
    { "holy_water", "holy_fire" },
    { "timed_shard", "shards" },
    { "purify", "disp_evil" },
    { "wall_to_door", "make_door" },
    { "make_flower", "make_tree" },
    { "make_spider_nest", "make_wall" },
};
/*! @} */

/*!
 * @name TOband（`toband/src/defines.h` の `GF_xxx`。103 個）
 * @{
 */
/*!
 * 専用の見た目は無い。変愚と同名の 73 個以外は、既存の見た目へ寄せる（決めたこと 2026-10-17「新しい見た目は作らない」）。
 * 寄せ先は意味の近いもの（原作の `project_m()` の効果と、同じコアの呼び出し側の用途で決めた）。`cave_temp` は範囲を数えるだけの内部の印で
 * 絵を出さない（アダプタが記録しない）が、全数の突き合わせのために `none` へつなぐ。
 */
inline constexpr CoreAttrAlias kTobandAliases[] = {
    { "stone", "stasis" },
    { "old_stone", "stasis" },
    { "special_stone", "stasis" },
    { "inertia", "inertial" },
    { "physical", "missile" },
    { "blunt", "force" },
    { "edged", "engetsu" },
    { "volcanic_bomb", "meteor" },
    { "godly_spear", "psy_spear" },
    { "pure_fire", "fire" },
    { "pure_aqua", "water" },
    { "pure_earth", "shards" },
    { "pure_wind", "force" },
    { "make_glyph", "make_rune_protection" },
    { "old_drain", "hypodynamia" },
    { "new_drain", "hypodynamia" },
    { "dual_drain", "hypodynamia" },
    { "drain_soul", "hypodynamia" },
    { "new_slow", "old_slow" },
    { "silent", "stun" },
    { "control_beast", "control_animal" },
    { "control_dragon", "domination" },
    { "genocide_undead", "genocide" },
    { "strike_nova", "fire" },
    { "word_of_pain", "wounds" },
    { "shining", "lite" },
    { "erase_elem", "disenchant" },
    { "cave_temp", "none" },
    { "brainstorm", "mind_blast" },
    { "umbra", "dark" },
};
/*! @} */

namespace detail {

template <std::size_t N>
inline const char *find_look(const CoreAttrAlias (&table)[N], const char *core_name)
{
    for (const CoreAttrAlias &entry : table) {
        if (std::strcmp(entry.core_name, core_name) == 0) {
            return entry.look_name;
        }
    }
    return nullptr;
}

} // namespace detail

//! Frox のコア名（`GF_` を除いた小文字）→ 見た目の名前。専用 → 寄せ先 → そのまま、の順。
inline const char *frox_look_name(const char *core_name)
{
    if (const char *own = detail::find_look(kFroxOwnLooks, core_name)) {
        return own;
    }
    if (const char *alias = detail::find_look(kFroxAliases, core_name)) {
        return alias;
    }
    return core_name;
}

//! 幻想蛮怒のコア名（`GF_` を除いた小文字）→ 見た目の名前。専用 → 寄せ先 → そのまま、の順。
inline const char *gensoband_look_name(const char *core_name)
{
    if (const char *own = detail::find_look(kGensobandOwnLooks, core_name)) {
        return own;
    }
    if (const char *alias = detail::find_look(kGensobandAliases, core_name)) {
        return alias;
    }
    return core_name;
}

//! Sil-Q のコア名（`GF_` を除いた小文字）→ 見た目の名前。専用 → 寄せ先 → そのまま、の順。
inline const char *silq_look_name(const char *core_name)
{
    if (const char *own = detail::find_look(kSilqOwnLooks, core_name)) {
        return own;
    }
    if (const char *alias = detail::find_look(kSilqAliases, core_name)) {
        return alias;
    }
    return core_name;
}

//! TOband のコア名（`GF_` を除いた小文字）→ 見た目の名前。寄せ先 → そのまま、の順（専用の見た目は無い）。
inline const char *toband_look_name(const char *core_name)
{
    if (const char *alias = detail::find_look(kTobandAliases, core_name)) {
        return alias;
    }
    return core_name;
}

} // namespace attack_fx
