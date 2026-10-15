/*!
 * @file an_lore_ja.c
 * @brief モンスターの思い出（`mon-lore.c` の `lore_append_*`）の日本語版。
 *
 * @details 原作は英語の句を `textblock_append()` で 1 つずつ継いで文を組む（"This" → 形容 → 名詞 →
 * " is normally found " → "at depths of " …）。句の並びが英語の語順なので、書式のカタログ（フック #2）で
 * 句ごとに引いても日本語の文にならない。そこで日本語のときだけ、節ごとに日本語の語順で組み直す。
 *
 * - 呼ぶのは `mon-lore.c` の各 `lore_append_*()` の頭（`prepare_sources.py` の置換。`//AN:` 印）。
 *   日本語層が寝ていれば 0 を返し、原作の道へ戻る（英語は 1 ビットも変えない。制約 1）。
 * - **字はこのファイルに書かない**（制約 11）。英語の鍵を `an_tr_tag('L', …)` で引き、訳は
 *   `messages.ja.txt` の「モンスターの思い出」の節（札 `L:`）にある。引けなければ英語の鍵が出る（制約 5）。
 * - 色は原作と同じ所に同じ色を付ける（数・深さ・呪文・打撃の効果）。
 * - 距離（出現の深さ・気付く距離）は `an_u_ft()` でメートルに換算する（§19.3）。
 * - 呪文の説明（`monster_spell.txt` の `lore:`）・打撃の型と効果（`blow_methods.txt`・`blow_effects.txt`
 *   の `desc:`）・説明文は lib-ja の訳（P3）がそのまま来る。
 */

#include "angband.h"

#include "effects.h"
#include "game-world.h"
#include "init.h"
#include "mon-attack.h"
#include "mon-blows.h"
#include "mon-lore.h"
#include "mon-make.h"
#include "mon-spell.h"
#include "mon-util.h"
#include "obj-gear.h"
#include "player-attack.h"
#include "player-calcs.h"
#include "effects-info.h"
#include "obj-properties.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player.h"
#include "project.h"
#include "ui-output.h"
#include "z-textblock.h"

#include "an_lang_c.h"

#include <string.h>

/* mon-lore.c の色の関数（置換で static を外した。§20）。 */
extern int spell_color(struct player *p, const struct monster_race *race, int spell_index);
extern int blow_color(struct player *p, int blow_idx);

/*! 思い出の句を引く（札 L:）。 */
#define LT(s) an_tr_tag('L', (s))

/*! 性別の代名詞（主語）。 */
static const char *an_lore_pronoun(const struct monster_race *race)
{
    if (rf_has(race->flags, RF_FEMALE)) {
        return LT("She");
    }
    if (rf_has(race->flags, RF_MALE)) {
        return LT("He");
    }
    return LT("It");
}

/*!
 * @brief 旗の句を読点で並べる（`lore_append_clause()` の日本語版）。
 * @param start 頭に付ける字（書式。`%s` に代名詞が入る）。NULL なら付けない。
 * @param end 末尾に付ける字。
 */
static void an_lore_clause(textblock *tb, bitflag *f, uint8_t attr, const char *pronoun, const char *start,
    const char *end)
{
    int flag;
    bool first = true;

    if (!rf_count(f)) {
        return;
    }
    if (start) {
        textblock_append(tb, start, pronoun);
    }
    for (flag = rf_next(f, FLAG_START); flag; flag = rf_next(f, flag + 1)) {
        if (!first) {
            textblock_append(tb, "%s", LT(", "));
        }
        textblock_append_c(tb, attr, "%s", LT(describe_race_flag(flag)));
        first = false;
    }
    textblock_append(tb, "%s", end);
}

/*! 呪文の一覧（`lore_append_spell_clause()` の日本語版）。 */
static void an_lore_spell_clause(textblock *tb, bitflag *f, bool know_hp, const struct monster_race *race)
{
    int spell;
    bool first = true;

    for (spell = rsf_next(f, FLAG_START); spell; spell = rsf_next(f, spell + 1)) {
        int color = spell_color(player, race, spell);
        int damage = mon_spell_lore_damage(spell, race, know_hp);

        if (!first) {
            textblock_append(tb, "%s", LT(", "));
        }
        textblock_append_c(tb, color, "%s", mon_spell_lore_description(spell, race));
        if (damage > 0) {
            textblock_append_c(tb, color, "(%d)", damage);
        }
        first = false;
    }
}

/*! 呪文の頻度（「N回に1回」「約N回に1回」）。 */
static void an_lore_freq(textblock *tb, bool known, bool guessed, int freq)
{
    if (!freq) {
        return;
    }
    if (known) {
        textblock_append(tb, "%s", LT(" ("));
        textblock_append_c(tb, COLOUR_L_GREEN, "%d", 100 / freq);
        textblock_append(tb, "%s", LT(" times in "));
        textblock_append_c(tb, COLOUR_L_GREEN, "1");
        textblock_append(tb, "%s", LT(" time)"));
    } else if (guessed) {
        int approx = MAX(((freq + 9) / 10) * 10, 1);
        textblock_append(tb, "%s", LT(" (about "));
        textblock_append_c(tb, COLOUR_L_GREEN, "%d", 100 / approx);
        textblock_append(tb, "%s", LT(" times in "));
        textblock_append_c(tb, COLOUR_L_GREEN, "1");
        textblock_append(tb, "%s", LT(" time)"));
    }
}

static void an_lore_kills(textblock *tb, const struct monster_race *race, const struct monster_lore *lore,
    const bitflag *known_flags)
{
    const char *p = an_lore_pronoun(race);
    bool out = true;

    if (rf_has(known_flags, RF_UNIQUE)) {
        bool dead = (race->max_num == 0);
        if (lore->deaths) {
            textblock_append(tb, LT("%s has slain %d of your ancestors"), p, lore->deaths);
            if (dead) {
                textblock_append(tb, "%s", LT(", but you have taken revenge!  "));
            } else {
                textblock_append(tb, "%s", LT(", who remain unavenged.  "));
            }
        } else if (dead) {
            textblock_append(tb, "%s", LT("You have slain this foe.  "));
        } else {
            out = false;
        }
    } else if (lore->deaths) {
        textblock_append(tb, LT("%d of your ancestors have been killed by this creature, "), lore->deaths);
        if (lore->pkills) {
            textblock_append(tb, LT("and you have exterminated at least %d of the creatures.  "), lore->pkills);
        } else if (lore->tkills) {
            textblock_append(tb, LT("and your ancestors have exterminated at least %d of the creatures.  "),
                lore->tkills);
        } else {
            textblock_append_c(tb, COLOUR_RED, LT("and %s is not ever known to have been defeated.  "), p);
        }
    } else {
        if (lore->pkills) {
            textblock_append(tb, LT("You have killed at least %d of these creatures.  "), lore->pkills);
        } else if (lore->tkills) {
            textblock_append(tb, LT("Your ancestors have killed at least %d of these creatures.  "), lore->tkills);
        } else {
            textblock_append(tb, "%s", LT("No battles to the death are recalled.  "));
        }
    }
    if (out) {
        textblock_append(tb, "\n");
    }
}

/*! 速さの語（`lore_describe_speed()` と同じ区切り）。 */
static const char *an_lore_speed_word(uint8_t speed)
{
    if (speed > 130) return LT("incredibly quickly");
    if (speed > 120) return LT("very quickly");
    if (speed > 115) return LT("quickly");
    if (speed > 110) return LT("fairly quickly");
    if (speed > 109) return LT("at normal speed");
    if (speed > 99) return LT("slowly");
    if (speed > 89) return LT("very slowly");
    return LT("incredibly slowly");
}

/*! 速さを倍率で（`lore_multiplier_speed()` の日本語版）。「通常の2.3倍の速さ（あなたの0.9倍）で」 */
static void an_lore_multiplier_speed(textblock *tb, const struct monster_race *race)
{
    char buf[32];
    int multiplier = 10 * extract_energy[race->speed] / extract_energy[110];
    uint8_t attr = COLOUR_ORANGE;

    textblock_append(tb, "%s", LT("at "));
    strnfmt(buf, sizeof(buf), "%d.%d", multiplier / 10, multiplier % 10);
    textblock_append_c(tb, COLOUR_L_BLUE, LT("%sx"), buf);
    textblock_append(tb, "%s", LT(" normal speed ("));
    multiplier = 100 * extract_energy[race->speed] / extract_energy[player->state.speed];
    if (!(multiplier % 100)) {
        strnfmt(buf, sizeof(buf), "%d", multiplier / 100);
    } else if (!((multiplier % 100) % 10)) {
        strnfmt(buf, sizeof(buf), "%d.%d", multiplier / 100, (multiplier % 100) / 10);
    } else {
        strnfmt(buf, sizeof(buf), "%d.%02d", multiplier / 100, multiplier % 100);
    }
    if (player->state.speed > race->speed) {
        attr = COLOUR_L_GREEN;
    } else if (player->state.speed < race->speed) {
        attr = COLOUR_RED;
    }
    if (player->state.speed == race->speed) {
        textblock_append(tb, "%s", LT("the same as you"));
    } else {
        textblock_append(tb, "%s", LT("your speed x"));
        textblock_append_c(tb, attr, LT("%sx"), buf);
    }
    textblock_append(tb, "%s", LT(") "));
}

static void an_lore_movement(textblock *tb, const struct monster_race *race, const bitflag *known_flags)
{
    int f;
    bitflag flags[RF_SIZE];
    bool rand = false;

    textblock_append(tb, "%s", LT("This"));
    create_mon_flag_mask(flags, RFT_RACE_A, RFT_MAX);
    rf_inter(flags, race->flags);
    for (f = rf_next(flags, FLAG_START); f; f = rf_next(flags, f + 1)) {
        textblock_append_c(tb, COLOUR_L_BLUE, "%s", LT(describe_race_flag(f)));
    }
    create_mon_flag_mask(flags, RFT_RACE_N, RFT_MAX);
    rf_inter(flags, race->flags);
    f = rf_next(flags, FLAG_START);
    textblock_append_c(tb, COLOUR_L_BLUE, "%s", f ? LT(describe_race_flag(f)) : LT("creature"));

    if (race->level == 0) {
        textblock_append(tb, "%s", LT(" lives in the town"));
    } else {
        uint8_t colour = (race->level > player->max_depth) ? COLOUR_RED : COLOUR_L_BLUE;
        textblock_append(tb, "%s", rf_has(known_flags, RF_FORCE_DEPTH) ? LT(" is found ") : LT(" is normally found "));
        textblock_append(tb, "%s", LT("at depths of "));
        textblock_append_c(tb, colour, "%d", an_u_ft(race->level * 50));
        textblock_append(tb, "%s", LT(" feet (level "));
        textblock_append_c(tb, colour, "%d", race->level);
        textblock_append(tb, "%s", LT(" level)"));
    }
    textblock_append(tb, "%s", LT(", and moves"));

    if (rf_has(known_flags, RF_RAND_50) && rf_has(known_flags, RF_RAND_25)) {
        textblock_append(tb, "%s", LT(" extremely erratically"));
        rand = true;
    } else if (rf_has(known_flags, RF_RAND_50)) {
        textblock_append(tb, "%s", LT(" somewhat erratically"));
        rand = true;
    } else if (rf_has(known_flags, RF_RAND_25)) {
        textblock_append(tb, "%s", LT(" a bit erratically"));
        rand = true;
    }
    if (rand) {
        textblock_append(tb, "%s", LT(", and"));
    }
    if (OPT(player, effective_speed)) {
        an_lore_multiplier_speed(tb, race);
    } else {
        textblock_append_c(tb, COLOUR_GREEN, "%s", an_lore_speed_word(race->speed));
    }
    textblock_append(tb, "%s", LT(" moves"));
    if (rf_has(known_flags, RF_NEVER_MOVE)) {
        textblock_append(tb, "%s", LT(", but "));
        textblock_append_c(tb, COLOUR_L_GREEN, "%s", LT("does not deign to chase intruders"));
    }
    textblock_append(tb, "%s", LT(".  "));
}

static void an_lore_toughness(textblock *tb, const struct monster_race *race, const struct monster_lore *lore,
    const bitflag *known_flags)
{
    struct object *weapon = equipped_item_by_slot_name(player, "weapon");
    random_chance c;
    int percent;

    if (!lore->armour_known) {
        return;
    }
    textblock_append(tb, rf_has(known_flags, RF_UNIQUE) ? LT("%s has a life rating of ")
                                                        : LT("%s has an average life rating of "),
        an_lore_pronoun(race));
    textblock_append_c(tb, COLOUR_L_BLUE, "%d", race->avg_hp);
    textblock_append(tb, "%s", LT(", and an armor rating of "));
    textblock_append_c(tb, COLOUR_L_BLUE, "%d", race->ac);
    textblock_append(tb, "%s", LT(".  "));

    hit_chance(&c, chance_of_melee_hit_base(player, weapon), race->ac);
    percent = random_chance_scaled(c, 100);
    textblock_append(tb, "%s", LT("You have a "));
    textblock_append_c(tb, COLOUR_L_BLUE, "%d", percent);
    textblock_append(tb, "%s", LT(" percent chance to hit such a creature in melee (if you can see it).  "));
}

static void an_lore_exp(textblock *tb, const struct monster_race *race)
{
    char buf[40];
    long exp_integer;
    long exp_fraction;

    if (!race->rarity) {
        return;
    }
    exp_integer = (long)race->mexp * race->level / player->lev;
    exp_fraction = ((((long)race->mexp * race->level % player->lev) * (long)1000 / player->lev + 5) / 10);
    strnfmt(buf, sizeof(buf), "%ld", exp_integer);
    if (exp_fraction) {
        my_strcat(buf, format(".%02ld", exp_fraction), sizeof(buf));
    }
    textblock_append(tb, LT("A kill of this creature for a level %d character is worth "), (int)player->lev);
    textblock_append_c(tb, COLOUR_BLUE, LT("%s points"), buf);
    textblock_append(tb, "%s", LT(" of experience.  "));
}

static void an_lore_drop(textblock *tb, const struct monster_race *race, const struct monster_lore *lore,
    const bitflag *known_flags)
{
    int n;
    int nspec = 0;

    if (!lore->drop_known) {
        return;
    }
    n = mon_create_drop_count(race, true, false, &nspec);
    if ((n <= 0) && (nspec <= 0)) {
        return;
    }
    textblock_append(tb, LT("%s may carry "), an_lore_pronoun(race));
    if (n > 0) {
        bool only_item = rf_has(known_flags, RF_ONLY_ITEM);
        bool only_gold = rf_has(known_flags, RF_ONLY_GOLD);
        if (n == 1) {
            textblock_append_c(tb, COLOUR_BLUE, "%s", LT("a single "));
        } else if (n == 2) {
            textblock_append_c(tb, COLOUR_BLUE, "%s", LT("one or two "));
        } else {
            textblock_append(tb, "%s", LT("up to "));
            textblock_append_c(tb, COLOUR_BLUE, LT("%d of "), n);
        }
        if (rf_has(known_flags, RF_DROP_GREAT)) {
            textblock_append_c(tb, COLOUR_BLUE, "%s", LT("exceptional "));
        } else if (rf_has(known_flags, RF_DROP_GOOD)) {
            textblock_append_c(tb, COLOUR_BLUE, "%s", LT("good "));
        }
        if (only_item && only_gold) {
            textblock_append_c(tb, COLOUR_BLUE, "%s", LT("errors"));
        } else if (only_item) {
            textblock_append_c(tb, COLOUR_BLUE, "%s", LT("objects"));
        } else if (only_gold) {
            textblock_append_c(tb, COLOUR_BLUE, "%s", LT("treasures"));
        } else {
            textblock_append_c(tb, COLOUR_BLUE, "%s", LT("objects or treasures"));
        }
    }
    if (nspec > 0) {
        if (n > 0) {
            textblock_append(tb, "%s", LT(" and "));
        }
        if (nspec == 1) {
            textblock_append(tb, "%s", LT("a single "));
        } else if (nspec == 2) {
            textblock_append(tb, "%s", LT("one or two "));
        } else {
            textblock_append(tb, "%s", LT("up to "));
            textblock_append_c(tb, COLOUR_BLUE, LT("%d of "), nspec);
        }
        textblock_append(tb, "%s", LT("specific items"));
    }
    textblock_append(tb, "%s", LT(" in its possession.  "));
}

static void an_lore_abilities(textblock *tb, const struct monster_race *race, const struct monster_lore *lore,
    const bitflag *known_flags)
{
    int flag;
    const char *p = an_lore_pronoun(race);
    bitflag current_flags[RF_SIZE];
    bitflag test_flags[RF_SIZE];

    create_mon_flag_mask(current_flags, RFT_ALTER, RFT_MAX);
    rf_inter(current_flags, known_flags);
    an_lore_clause(tb, current_flags, COLOUR_WHITE, p, LT("%s can: "), LT(".  "));

    create_mon_flag_mask(current_flags, RFT_DET, RFT_MAX);
    rf_inter(current_flags, known_flags);
    an_lore_clause(tb, current_flags, COLOUR_WHITE, p, LT("%s is: "), LT(".  "));

    if (rf_has(known_flags, RF_UNAWARE)) {
        textblock_append(tb, LT("%s disguises itself as something else.  "), p);
    }
    if (rf_has(known_flags, RF_MULTIPLY)) {
        textblock_append_c(tb, COLOUR_ORANGE, LT("%s breeds explosively.  "), p);
    }
    if (rf_has(known_flags, RF_REGENERATE)) {
        textblock_append(tb, LT("%s regenerates quickly.  "), p);
    }

    if (race->light > 1) {
        textblock_append(tb, LT("%s illuminates its surroundings.  "), p);
    } else if (race->light == 1) {
        textblock_append(tb, LT("%s is illuminated.  "), p);
    } else if (race->light == -1) {
        textblock_append(tb, LT("%s is darkened.  "), p);
    } else if (race->light < -1) {
        textblock_append(tb, LT("%s shrouds its surroundings in darkness.  "), p);
    }

    /* 弱点 */
    create_mon_flag_mask(current_flags, RFT_VULN, RFT_VULN_I, RFT_MAX);
    rf_inter(current_flags, known_flags);
    an_lore_clause(tb, current_flags, COLOUR_VIOLET, p, LT("%s is hurt by "), LT(" (hurt by).  "));

    /* 耐性（弱点が無いと分かったものも耐性として数える。原作と同じ） */
    create_mon_flag_mask(current_flags, RFT_RES, RFT_MAX);
    rf_inter(current_flags, known_flags);
    create_mon_flag_mask(test_flags, RFT_VULN, RFT_MAX);
    for (flag = rf_next(test_flags, FLAG_START); flag; flag = rf_next(test_flags, flag + 1)) {
        if (rf_has(lore->flags, flag) && !rf_has(known_flags, flag)) {
            rf_on(current_flags, flag);
        }
    }
    an_lore_clause(tb, current_flags, COLOUR_L_UMBER, p, LT("%s resists "), LT(" (resists).  "));

    /* 耐性が無いと分かったもの */
    rf_wipe(current_flags);
    create_mon_flag_mask(test_flags, RFT_RES, RFT_MAX);
    for (flag = rf_next(test_flags, FLAG_START); flag; flag = rf_next(test_flags, flag + 1)) {
        if (rf_has(lore->flags, flag) && !rf_has(known_flags, flag)) {
            rf_on(current_flags, flag);
        }
    }
    create_mon_flag_mask(test_flags, RFT_VULN_I, RFT_MAX);
    rf_inter(test_flags, known_flags);
    for (flag = rf_next(test_flags, FLAG_START); flag; flag = rf_next(test_flags, flag + 1)) {
        int susc_flag;
        for (susc_flag = rf_next(current_flags, FLAG_START); susc_flag;
             susc_flag = rf_next(current_flags, susc_flag + 1)) {
            if (streq(describe_race_flag(flag), describe_race_flag(susc_flag))) {
                rf_off(current_flags, susc_flag);
            }
        }
    }
    if (rf_has(known_flags, RF_UNDEAD)) {
        rf_off(current_flags, RF_IM_NETHER);
    }
    an_lore_clause(tb, current_flags, COLOUR_L_UMBER, p, LT("%s does not resist "), LT(" (does not resist).  "));

    /* 効かない状態 */
    create_mon_flag_mask(current_flags, RFT_PROT, RFT_MAX);
    rf_inter(current_flags, known_flags);
    an_lore_clause(tb, current_flags, COLOUR_L_UMBER, p, LT("%s cannot be "), LT(" (cannot be).  "));
}

/*! 気付きやすさ（`lore_describe_awareness()` と同じ区切り）。 */
static const char *an_lore_awareness_word(int16_t awareness)
{
    if (awareness > 200) return LT("prefers to ignore");
    if (awareness > 95) return LT("pays very little attention to");
    if (awareness > 75) return LT("pays little attention to");
    if (awareness > 45) return LT("tends to overlook");
    if (awareness > 25) return LT("takes quite a while to see");
    if (awareness > 10) return LT("takes a while to see");
    if (awareness > 5) return LT("is fairly observant of");
    if (awareness > 3) return LT("is observant of");
    if (awareness > 1) return LT("is very observant of");
    if (awareness > 0) return LT("is vigilant for");
    return LT("is ever vigilant for");
}

static void an_lore_awareness(textblock *tb, const struct monster_race *race, const struct monster_lore *lore)
{
    const char *p = an_lore_pronoun(race);

    if (!lore->sleep_known) {
        return;
    }
    textblock_append(tb, LT("%s %s intruders.  "), p, an_lore_awareness_word(race->sleep));
    textblock_append(tb, LT("%s may notice them from "), p);
    textblock_append_c(tb, COLOUR_L_BLUE, "%d", an_u_ft(10 * race->hearing));
    textblock_append(tb, "%s", LT(" feet away.  "));
}

static void an_lore_friends(textblock *tb, const struct monster_race *race, const bitflag *known_flags)
{
    if (!race->friends && !race->friends_base) {
        return;
    }
    textblock_append(tb, LT("%s may appear with other monsters"), an_lore_pronoun(race));
    if (rf_has(known_flags, RF_GROUP_AI)) {
        textblock_append(tb, "%s", LT(" and hunts in packs"));
    }
    textblock_append(tb, "%s", LT(".  "));
}

static void an_lore_spells(textblock *tb, const struct monster_race *race, const struct monster_lore *lore,
    const bitflag *known_flags)
{
    const char *p = an_lore_pronoun(race);
    bool innate = false;
    bool breath = false;
    bool know_hp = lore->armour_known;
    bitflag current_flags[RSF_SIZE];
    bitflag test_flags[RSF_SIZE];
    const struct monster_race *old_ref = ref_race;

    ref_race = race;

    create_mon_spell_mask(current_flags, RST_INNATE, RST_NONE);
    rsf_inter(current_flags, lore->spell_flags);
    create_mon_spell_mask(test_flags, RST_BREATH, RST_NONE);
    rsf_diff(current_flags, test_flags);
    if (!rsf_is_empty(current_flags)) {
        textblock_append(tb, LT("%s may use: "), p);
        an_lore_spell_clause(tb, current_flags, know_hp, race);
        innate = true;
    }

    create_mon_spell_mask(current_flags, RST_BREATH, RST_NONE);
    rsf_inter(current_flags, lore->spell_flags);
    if (!rsf_is_empty(current_flags)) {
        if (innate) {
            textblock_append(tb, "%s", LT("; and may "));
        } else {
            textblock_append(tb, LT("%s may "), p);
        }
        textblock_append_c(tb, COLOUR_L_RED, "%s", LT("breathe"));
        textblock_append(tb, "%s", LT(" (breath): "));
        an_lore_spell_clause(tb, current_flags, know_hp, race);
        breath = true;
    }
    if (innate || breath) {
        if (race->freq_innate) {
            an_lore_freq(tb, lore->innate_freq_known, lore->cast_innate, race->freq_innate);
        }
        textblock_append(tb, "%s", LT(".  "));
    }

    rsf_copy(current_flags, lore->spell_flags);
    create_mon_spell_mask(test_flags, RST_BREATH, RST_INNATE, RST_NONE);
    rsf_diff(current_flags, test_flags);
    if (!rsf_is_empty(current_flags)) {
        textblock_append(tb, LT("%s may "), p);
        if (rf_has(known_flags, RF_SMART)) {
            textblock_append(tb, "%s", LT("intelligently "));
        }
        textblock_append_c(tb, COLOUR_L_RED, "%s", LT("cast spells"));
        textblock_append(tb, "%s", LT(" (spells): "));
        an_lore_spell_clause(tb, current_flags, know_hp, race);
        if (race->freq_spell) {
            an_lore_freq(tb, lore->spell_freq_known, lore->cast_spell, race->freq_spell);
        }
        textblock_append(tb, "%s", LT(".  "));
    }

    ref_race = old_ref;
}

static void an_lore_attack(textblock *tb, const struct monster_race *race, const struct monster_lore *lore,
    const bitflag *known_flags)
{
    int i;
    int known_attacks = 0;
    int total_attacks = 0;
    int described_count = 0;
    int total_centidamage = 99; /* 原作と同じく最後に切り上げる */
    const char *p = an_lore_pronoun(race);

    if (rf_has(known_flags, RF_NEVER_BLOW)) {
        textblock_append(tb, LT("%s has no physical attacks.  "), p);
        return;
    }
    for (i = 0; i < z_info->mon_blows_max; i++) {
        if (!race->blow[i].method) {
            continue;
        }
        total_attacks++;
        if (lore->blow_known[i]) {
            known_attacks++;
        }
    }
    if (known_attacks == 0) {
        textblock_append_c(tb, COLOUR_ORANGE, LT("Nothing is known about %s attack.  "), p);
        return;
    }

    for (i = 0; i < z_info->mon_blows_max; i++) {
        random_value dice;
        const char *effect_str;

        if (!race->blow[i].method || !lore->blow_known[i]) {
            continue;
        }
        dice = race->blow[i].dice;
        effect_str = race->blow[i].effect->desc;

        if (described_count == 0) {
            textblock_append(tb, LT("%s can attack: "), p);
        } else {
            textblock_append(tb, "%s", LT(", "));
        }
        textblock_append(tb, "%s", race->blow[i].method->desc);

        if (effect_str && strlen(effect_str) > 0) {
            int index = blow_index(race->blow[i].effect->name);
            random_chance c;
            int percent;

            textblock_append(tb, "%s", LT(" ("));
            textblock_append_c(tb, blow_color(player, index), "%s", effect_str);
            textblock_append(tb, "%s", LT(", "));
            if (dice.base || (dice.dice && dice.sides) || dice.m_bonus) {
                if (dice.base) {
                    textblock_append_c(tb, COLOUR_L_GREEN, "%d", dice.base);
                }
                if (dice.dice && dice.sides) {
                    textblock_append_c(tb, COLOUR_L_GREEN, "%dd%d", dice.dice, dice.sides);
                }
                if (dice.m_bonus) {
                    textblock_append_c(tb, COLOUR_L_GREEN, "M%d", dice.m_bonus);
                }
                textblock_append(tb, "%s", LT(", "));
            }
            hit_chance(&c, chance_of_monster_hit_base(race, race->blow[i].effect),
                player->state.ac + player->state.to_a);
            percent = random_chance_scaled(c, 100);
            textblock_append(tb, "%s", LT("hit "));
            textblock_append_c(tb, COLOUR_L_BLUE, "%d", percent);
            textblock_append(tb, "%s", LT(" percent)"));
            total_centidamage += (percent * randcalc(dice, 0, AVERAGE));
        }
        described_count++;
    }

    textblock_append(tb, "%s", LT(".  "));
    textblock_append(tb, "%s", LT("On each of its turns it averages "));
    if (known_attacks < total_attacks) {
        textblock_append_c(tb, COLOUR_ORANGE, "%s", LT("at least "));
    }
    textblock_append_c(tb, COLOUR_L_GREEN, "%d", total_centidamage / 100);
    textblock_append(tb, "%s", LT(" damage.  "));
}

/*!
 * @brief `mon-lore.c` の各 `lore_append_*()` の頭から呼ぶ（§20。フック #29）。
 * @param part 0=kills 1=movement 2=toughness 3=exp 4=drop 5=abilities 6=awareness 7=friends 8=spells 9=attack
 * @return 日本語層が寝ていれば 0（原作の道へ）。組んだら 1。
 */
int an_lore_ja(int part, textblock *tb, const struct monster_race *race, const struct monster_lore *lore,
    const bitflag *known_flags)
{
    if (!an_lang_enabled() || !tb || !race || !lore) {
        return 0;
    }
    switch (part) {
    case 0: an_lore_kills(tb, race, lore, known_flags); break;
    case 1: an_lore_movement(tb, race, known_flags); break;
    case 2: an_lore_toughness(tb, race, lore, known_flags); break;
    case 3: an_lore_exp(tb, race); break;
    case 4: an_lore_drop(tb, race, lore, known_flags); break;
    case 5: an_lore_abilities(tb, race, lore, known_flags); break;
    case 6: an_lore_awareness(tb, race, lore); break;
    case 7: an_lore_friends(tb, race, known_flags); break;
    case 8: an_lore_spells(tb, race, lore, known_flags); break;
    case 9: an_lore_attack(tb, race, lore, known_flags); break;
    default: return 0;
    }
    return 1;
}

/*!
 * @brief 思い出の見出しの名（`ui-mon-lore.c` の `lore_title()`）。日本語では名前の表の名。
 * 英語の `The ` は日本語では付けない（置換の側で `an_lang_enabled()` を見る）。
 */
const char *an_lore_race_name(const char *name)
{
    const char *ja;
    if (!an_lang_enabled() || !name) {
        return name;
    }
    ja = an_name("monster", name);
    return ja ? ja : name;
}

/*!
 * @brief アイテムの由来の「〜が落とした」の怪物の名（`obj-info.c` の `describe_origin()`。§20）。
 * @details 原作は英語の冠詞（`a `/`an `）を付けた名を組む。日本語では冠詞を付けず名前の表の名にする。
 * `race_name` が NULL（記録の無い怪物）なら L: の句。英語では何もしない。
 */
void an_origin_name_ja(char *name, size_t cap, const char *race_name)
{
    const char *ja;
    if (!an_lang_enabled() || !name || (cap == 0)) {
        return;
    }
    if (!race_name) {
        my_strcpy(name, LT("monster lost to history"), cap);
        return;
    }
    ja = an_name("monster", race_name);
    my_strcpy(name, ja ? ja : race_name, cap);
}

/* ------------------------------------------------------------------ 変身の説明（ui-knowledge.c の shape_lore()） */

/*! 属性・能力の名などの 1 語を引く（M: → A: → E:。訳が無ければ英語）。 */
static const char *an_shape_word(const char *en)
{
    const char *t;
    if (!en) {
        return "";
    }
    t = an_tr_tag('L', en);
    if (t != en) {
        return t;
    }
    t = an_tr_tag('M', en);
    if (t != en) {
        return t;
    }
    t = an_tr_tag('A', en);
    if (t != en) {
        return t;
    }
    return an_tr(en);
}

/*! 技能の名（ui-knowledge.c の skill_index_to_name() と同じ区分）。 */
static const char *an_shape_skill_name(int skill)
{
    switch (skill) {
    case SKILL_DISARM_PHYS: return LT("physical disarming");
    case SKILL_DISARM_MAGIC: return LT("magical disarming");
    case SKILL_DEVICE: return LT("magic devices");
    case SKILL_SAVE: return LT("saving throws");
    case SKILL_SEARCH: return LT("searching");
    case SKILL_TO_HIT_MELEE: return LT("melee to hit");
    case SKILL_TO_HIT_BOW: return LT("shooting to hit");
    case SKILL_TO_HIT_THROW: return LT("throwing to hit");
    case SKILL_DIGGING: return LT("digging");
    default: return LT("unknown skill");
    }
}

/*! 見出しの後に語を読点で並べ、句点と改行で閉じる。 */
static void an_shape_list(textblock *tb, const char *head, const char *const *items, int n)
{
    int i;
    if (n <= 0) {
        return;
    }
    textblock_append(tb, "%s", head);
    for (i = 0; i < n; ++i) {
        textblock_append(tb, "%s%s", (i > 0) ? LT(", ") : "", items[i]);
    }
    textblock_append(tb, "%s", LT(".\n"));
}

/*!
 * @brief `shape_lore()` の日本語版（§20。英語の句を継いで文を組むので、語順ごと組み直す）。
 * @return 日本語層が寝ていれば 0（原作の道へ）。表示したら 1。
 */
int an_shape_lore_ja(const struct player_shape *s)
{
    textblock *tb;
    char bufs[OBJ_MOD_MAX + SKILL_MAX + 3][80];
    const char *items[OBJ_MOD_MAX + SKILL_MAX + ELEM_MAX + OF_MAX + 3];
    int n;
    int nb = 0;
    int i;
    struct player_class *c;
    struct player_ability *ability;
    textblock *tbe;

    if (!an_lang_enabled() || !s) {
        return 0;
    }
    tb = textblock_new();
    textblock_append(tb, "%s", an_shape_word(s->name));
    textblock_append(tb, "%s", LT("\nLike all shapes, the equipment at the time of the shapechange sets the base "
        "attributes, including damage per blow, number of blows and resistances.  While changed, items in your "
        "pack or on the floor (except for pickup or eating) are inaccessible.  To switch back to your normal "
        "shape, cast a spell or use an item command other than eat (drop, for instance).\n"));

    /* AC・命中・ダメージ */
    n = 0;
    if (s->to_a != 0) {
        strnfmt(bufs[nb], sizeof(bufs[nb]), LT("%+d to AC"), s->to_a);
        items[n++] = bufs[nb++];
    }
    if (s->to_h != 0) {
        strnfmt(bufs[nb], sizeof(bufs[nb]), LT("%+d to hit"), s->to_h);
        items[n++] = bufs[nb++];
    }
    if (s->to_d != 0) {
        strnfmt(bufs[nb], sizeof(bufs[nb]), LT("%+d to damage"), s->to_d);
        items[n++] = bufs[nb++];
    }
    an_shape_list(tb, LT("Adds: "), items, n);

    /* 技能 */
    n = 0;
    for (i = 0; i < SKILL_MAX; ++i) {
        if (s->skills[i] != 0) {
            strnfmt(bufs[nb], sizeof(bufs[nb]), LT("%s %+d"), an_shape_skill_name(i), s->skills[i]);
            items[n++] = bufs[nb++];
        }
    }
    an_shape_list(tb, LT("Adds: "), items, n);

    /* 能力値以外の修正・能力値の修正（原作と同じく 2 つの文） */
    n = 0;
    for (i = STAT_MAX; i < OBJ_MOD_MAX; ++i) {
        if (s->modifiers[i] != 0) {
            strnfmt(bufs[nb], sizeof(bufs[nb]), LT("%s %+d"),
                an_shape_word(lookup_obj_property(OBJ_PROPERTY_MOD, i)->name), s->modifiers[i]);
            items[n++] = bufs[nb++];
        }
    }
    an_shape_list(tb, LT("Adds: "), items, n);
    n = 0;
    for (i = 0; i < STAT_MAX; ++i) {
        if (s->modifiers[i] != 0) {
            strnfmt(bufs[nb], sizeof(bufs[nb]), LT("%s %+d"),
                an_shape_word(lookup_obj_property(OBJ_PROPERTY_MOD, i)->name), s->modifiers[i]);
            items[n++] = bufs[nb++];
        }
    }
    an_shape_list(tb, LT("Adds: "), items, n);

    /* 耐性 */
    {
        const char *vul[ELEM_MAX];
        const char *res[ELEM_MAX];
        const char *imm[ELEM_MAX];
        int nvul = 0;
        int nres = 0;
        int nimm = 0;
        for (i = 0; i < ELEM_MAX; ++i) {
            if (s->el_info[i].res_level < 0) {
                vul[nvul++] = an_shape_word(projections[i].name);
            } else if (s->el_info[i].res_level >= 3) {
                imm[nimm++] = an_shape_word(projections[i].name);
            } else if (s->el_info[i].res_level != 0) {
                res[nres++] = an_shape_word(projections[i].name);
            }
        }
        an_shape_list(tb, LT("Makes you vulnerable to: "), vul, nvul);
        an_shape_list(tb, LT("Makes you resistant to: "), res, nres);
        an_shape_list(tb, LT("Makes you immune to: "), imm, nimm);
    }

    /* 状態からの保護 */
    n = 0;
    for (i = 1; i < OF_MAX; ++i) {
        struct obj_property *prop = lookup_obj_property(OBJ_PROPERTY_FLAG, i);
        if (prop && (prop->subtype == OFT_PROT) && of_has(s->flags, prop->index)) {
            items[n++] = an_shape_word(prop->desc);
        }
    }
    an_shape_list(tb, LT("Provides protection from: "), items, n);

    /* 能力値の維持 */
    n = 0;
    for (i = 0; i < STAT_MAX; ++i) {
        struct obj_property *prop = lookup_obj_property(OBJ_PROPERTY_STAT, i);
        if (of_has(s->flags, sustain_flag(prop->index))) {
            items[n++] = an_shape_word(prop->name);
        }
    }
    an_shape_list(tb, LT("Sustains: "), items, n);

    /* その他の性質（説明文は lib-ja の訳） */
    n = 0;
    for (i = 1; i < OF_MAX; ++i) {
        struct obj_property *prop = lookup_obj_property(OBJ_PROPERTY_FLAG, i);
        if (prop && ((prop->subtype == OFT_MISC) || (prop->subtype == OFT_MELEE) || (prop->subtype == OFT_BAD))
                && of_has(s->flags, prop->index)) {
            textblock_append(tb, "%s%s", an_shape_word(prop->desc), LT("."));
            ++n;
        }
    }
    for (ability = player_abilities; ability; ability = ability->next) {
        if (streq(ability->type, "player") && pf_has(s->pflags, ability->index)) {
            textblock_append(tb, "%s", ability->desc);
            ++n;
        }
    }
    if (n > 0) {
        textblock_append(tb, "\n");
    }

    /* 変身したときの効果 */
    tbe = effect_describe(s->effect, LT("Changing into the shape "), 0, false);
    if (tbe) {
        textblock_append_textblock(tb, tbe);
        textblock_free(tbe);
        textblock_append(tb, "%s", LT(".\n"));
    }

    /* 変身を起こす呪文 */
    n = 0;
    for (c = classes; c; c = c->next) {
        int ibook;
        for (ibook = 0; ibook < c->magic.num_books; ++ibook) {
            const struct class_book *book = c->magic.books + ibook;
            const struct object_kind *kind = lookup_kind(book->tval, book->sval);
            int ispell;
            if (!kind || !kind->name) {
                continue;
            }
            for (ispell = 0; ispell < book->num_spells; ++ispell) {
                const struct class_spell *spell = book->spells + ispell;
                const struct effect *effect;
                for (effect = spell->effect; effect; effect = effect->next) {
                    if ((effect->index == EF_SHAPECHANGE) && (effect->subtype == s->sidx)) {
                        char key[160];
                        const char *cls = an_name("race_class", c->name);
                        const char *sp = an_name("spell", spell->name);
                        const char *bk;
                        strnfmt(key, sizeof(key), "%s:%s", tval_find_name(book->tval), kind->name);
                        bk = an_name("object", key);
                        if (n == 0) {
                            textblock_append(tb, "\n");
                        }
                        textblock_append(tb, LT("The %s spell, %s, from %s triggers the shapechange."),
                            cls ? cls : c->name, sp ? sp : spell->name, bk ? bk : kind->name);
                        ++n;
                    }
                }
            }
        }
    }
    if (n > 0) {
        textblock_append(tb, "\n");
    }

    textui_textblock_show(tb, SCREEN_REGION, NULL);
    textblock_free(tb);
    return 1;
}

/*! 呪文の一覧の呪文の名（`ui-spell.c`。§20）。名前の表 `spell` の名。英語では受け取った字のまま。 */
const char *an_spell_name_ja(const char *name)
{
    const char *ja;
    if (!an_lang_enabled() || !name) {
        return name;
    }
    ja = an_name("spell", name);
    return ja ? ja : name;
}
