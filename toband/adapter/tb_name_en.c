/*!
 * @file tb_name_en.c
 * @brief 名前の英語の組み立て。フック #6（`object_desc`）・#7（`monster_desc`）の中身。
 *
 * JP ビルドの `object_desc()` / `monster_desc()` は**日本語の文法**で組む——個数の助数詞を頭に置き
 * （`3本の Dagger`）、エゴと銘を名前の**前**に生で貼り（`of ProtectionDagger`）、冠詞を作らず、
 * 指輪の種別は後ろに付ける（`Speedの指輪`）。`#else`（英語の枝）は一度も組まれたことがなく、しかも
 * 識別済みの `Ring of X` の「of」を欠いているなど、そのまま使える手本ではない。
 *
 * ## 方式（幻想蛮怒の `gb_name_en.c` が文字列を後から直すのと違う点）
 * - **名前の部分は構造から英語で組み直す**。個数・冠詞・種別の語・エゴ・銘・複数形は、日本語の文字列を
 *   読み取らずに `object_type`／`k_info` の英語の名前から作る（日本語の字を探して切り貼りしない）。
 * - **飾りの部分（ダメージ・修正値・回数・充填・銘）は JP の組み立てをそのまま使い**、日本語の語だけを表で
 *   英語にする。名前の部分と飾りの部分の境目は、同じ品を `OD_NAME_ONLY` で組んだ長さで正確に分かる
 *   （文字列の中の `(` を探す推測をしない）。
 * - 英語のときだけ働く（`tb_lang_enabled()` が偽なら呼ばれても何もしない。呼び出し側のフックも同じ）。
 *
 * ## 形（決めたこと・設計 §8）
 * `3 Daggers`・`a Dagger of Protection (1d4) (+1,+2)`・`The Long Sword 'Firecrest' (3d5)`・
 * `a Ring of Speed (+5 Sp)`・`a Granite Ring`・`2 Potions of Cure Light Wounds`・`the Scrawny cat`・`Grip`。
 *
 * ## 作法
 * - 日本語の照合バイトは**すべて 16 進**で書く（この TU は UTF-8/BOM。リテラルに日本語を書くと
 *   コンパイラの文字コードの差で照合が外れる）。表は `tools/toband/` の外の生成器で作った（このファイルが正本）。
 * - 器の約束（`MAX_NLEN` = 160）を超えたら黙って切り詰める（呼び手の器を壊さない）。
 */

#include "angband.h"

#include "tb_lang_c.h"

#include <string.h>

/* ================================================================ 小さな道具 */

/*! `buf`（容量 cap）の末尾へ `s` を足す。入り切らないぶんは切る。 */
static void en_cat(char *buf, size_t cap, const char *s)
{
    const size_t n = strlen(buf);
    size_t i;
    if (n + 1 >= cap) return;
    for (i = 0; s[i] && (n + i + 1 < cap); i++) buf[n + i] = s[i];
    buf[n + i] = '\0';
}

/*! `buf` の `pos` から `len` バイトを取り除く。 */
static void en_cut(char *buf, size_t pos, size_t len)
{
    memmove(buf + pos, buf + pos + len, strlen(buf + pos + len) + 1);
}

/*! `buf`（容量 cap）の `pos` に `ins` を差し込む。入り切らなければ偽。 */
static int en_insert(char *buf, size_t cap, size_t pos, const char *ins)
{
    const size_t blen = strlen(buf);
    const size_t ilen = strlen(ins);
    if (blen + ilen + 1 > cap) return 0;
    memmove(buf + pos + ilen, buf + pos, blen - pos + 1);
    memcpy(buf + pos, ins, ilen);
    return 1;
}

/*! ASCII の大文字小文字を無視した一致。 */
static int en_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if ((x >= 'A') && (x <= 'Z')) x = (char)(x - 'A' + 'a');
        if ((y >= 'A') && (y <= 'Z')) y = (char)(y - 'A' + 'a');
        if (x != y) return 0;
    }
    return *a == *b;
}

/*! 部分文字列の位置（無ければ -1）。 */
static long en_find(const char *buf, const char *pat)
{
    const char *p = strstr(buf, pat);
    return p ? (long)(p - buf) : -1;
}

/*! 純 ASCII か。 */
static int en_is_ascii(const char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s >= 0x80) return 0;
    }
    return 1;
}

/*! CP932 の先行バイトか。 */
static int en_is_lead(unsigned char c)
{
    return ((c >= 0x81) && (c <= 0x9F)) || ((c >= 0xE0) && (c <= 0xFC));
}

/*!
 * 単語を複数形にする。`Torch`→`Torches`・`Berry`→`Berries`・`Boots`→そのまま・`Staff`→`Staves`。
 * 既に複数形らしい語（単独の `s` で終わる。`Boots`・`Gloves`・`Cesti`）は触らない。
 */
static void en_pluralize_word(char *word, size_t cap)
{
    const size_t n = strlen(word);
    char a, b;
    if ((n == 0) || (n + 4 > cap)) return;
    a = word[n - 1];
    b = (n >= 2) ? word[n - 2] : '\0';
    if (strcmp(word, "Staff") == 0) {
        strcpy(word, "Staves");
    } else if ((a == 's') && (b != 's') && (b != 'u')) {
        /* 既に複数形（Boots・Gloves・Gauntlets…） */
    } else if ((a == 's') || (a == 'x') || (a == 'z') || ((a == 'h') && ((b == 's') || (b == 'c')))) {
        strcpy(word + n, "es");
    } else if ((a == 'y') && !strchr("aeiouAEIOU", b)) {
        strcpy(word + n - 1, "ies");
    } else if ((a == 'f') && (n >= 2) && (b == 'l' || b == 'e' || b == 'a' || b == 'r') && (n > 3)) {
        /* Dwarf/Wolf/Knife/Leaf/Loaf のような語は無いので何もしない（"Elf" 等を壊さない） */
        strcpy(word + n, "s");
    } else {
        strcpy(word + n, "s");
    }
}

/* ================================================== 飾りの表（JP の語 → 英語） */

/*!
 * `object_desc()` が名前の後ろへ継ぎ足す日本語の語と、その英語。**並びが優先順**（長い物を先に）。
 * 鍵は `flavor.c` が `object_desc_str()` で直に書く字（`(空)` `(施錠)` ほか）。
 * 先頭が `(` の物は、名前に空白なしで続くので、英語側に空白を足してある。
 */
static const struct { const char *jp; const char *en; } en_decor[] = {
    /* 箱（`flavor.c` の TV_CHEST） */
    { "(\x8b\xf3)" /* (空) */,           " (empty)" },
    { "(\x89\xf0\x8f\x9c\x8d\xcf)" /* (解除済) */,       " (disarmed)" },
    { "(\x94\xf1\x8e{\x8f\xf9)" /* (非施錠) */,       " (unlocked)" },
    { "(\x8e{\x8f\xf9)" /* (施錠) */,         " (Locked)" },
    { "(\x93\xc5\x90j)" /* (毒針) */,         " (Poison Needle)" },
    { "(\x83K\x83X\x81" "E\x83g\x83\x89\x83" "b\x83v)" /* (ガス・トラップ) */, " (Gas Trap)" },
    { "(\x94\x9a\x94\xad\x91\x95\x92u)" /* (爆発装置) */,     " (Explosion Device)" },
    { "(\x8f\xa2\x8a\xab\x82\xcc\x83\x8b\x81[\x83\x93)" /* (召喚のルーン) */, " (Summoning Runes)" },
    { "(\x8e\xd7\x88\xab\x82\xc8\x83\x8b\x81[\x83\x93)" /* (邪悪なルーン) */, " (Gleaming Black Runes)" },
    { "(\x8cx\x95\xf1\x91\x95\x92u)" /* (警報装置) */,     " (Alarm)" },
    { "(\x83}\x83\x8b\x83`\x81" "E\x83g\x83\x89\x83" "b\x83v)" /* (マルチ・トラップ) */, " (Multiple Traps)" },
    /* ロッドの充填 */
    { "(\x8f[\x93U\x92\x86)" /* (充填中) */,       " (charging)" },
    { "\x96{ \x8f[\x93U\x92\x86)" /* 本 充填中) */,     " charging)" },
    /* 灯り */
    { "\x83^\x81[\x83\x93\x82\xcc\x8e\xf5\x96\xbd)" /* ターンの寿命) */,  " turns of light)" },
};

/*! 表を当てる（何回でも・何か所でも）。 */
static void en_apply_decor(char *buf, size_t cap)
{
    size_t i;
    for (i = 0; i < sizeof(en_decor) / sizeof(en_decor[0]); i++) {
        long pos;
        while ((pos = en_find(buf, en_decor[i].jp)) >= 0) {
            en_cut(buf, (size_t)pos, strlen(en_decor[i].jp));
            if (!en_insert(buf, cap, (size_t)pos, en_decor[i].en)) break;
        }
    }
}

/*!
 * 杖・魔法棒の回数 `(7回分)` を `(7 charges)` へ（`1回分` は `(1 charge)`）。積み重ねた杖の `(2x 6回分)` も同じ。
 */
static void en_fix_charges(char *buf, size_t cap)
{
    static const char unit[] = "\x89\xf1\x95\xaa" /* 回分 */;
    size_t guard;
    for (guard = 0; guard < 4; guard++) {
        const char *at = strstr(buf, unit);
        size_t pos, digits_from;
        int one;
        while (at && (at[sizeof(unit) - 1] != ')')) at = strstr(at + 1, unit);
        if (!at) return;
        pos = (size_t)(at - buf);
        digits_from = pos;
        while ((digits_from > 0) && (buf[digits_from - 1] >= '0') && (buf[digits_from - 1] <= '9')) digits_from--;
        if (digits_from == pos) return;
        one = ((pos - digits_from) == 1) && (buf[digits_from] == '1');
        en_cut(buf, pos, sizeof(unit) - 1);
        if (!en_insert(buf, cap, pos, one ? " charge" : " charges")) return;
    }
}

/*! `{}` の中の、ゲームが付ける偽の銘（品質の感じ・割引ほか）。日本語の語と英語の対。 */
static const struct { const char *jp; const char *en; } en_fake_inscription[] = {
    { "\x89\xf3\x82\xea\x82\xc4\x82\xa2\x82\xe9" /* 壊れている */, "broken" },
    { "\x8b\xb0\x82\xeb\x82\xb5\x82\xa2" /* 恐ろしい */, "terrible" },
    { "\x96\xb3\x89\xbf\x92l" /* 無価値 */, "worthless" },
    { "\x8e\xf4\x82\xed\x82\xea\x82\xc4\x82\xa2\x82\xe9" /* 呪われている */, "cursed" },
    { "\x8f\xe3\x8e\xbf\x88\xc8\x8f\xe3" /* 上質以上 */, "uncursed" },
    { "\x95\xc0" /* 並 */, "average" },
    { "\x8f\xe3\x8e\xbf" /* 上質 */, "good" },
    { "\x8d\x82\x8b\x89\x95i" /* 高級品 */, "excellent" },
    { "\x93\xc1\x95\xca\x90\xbb" /* 特別製 */, "special" },
    { "\x96\xa2\x94\xbb\x96\xbe" /* 未判明 */, "tried" },
    { "\x96\xa2\x8a\xd3\x92\xe8" /* 未鑑定 */, "unidentified" },
    { "\x8b\xf3" /* 空 */, "empty" },
    { "\x8e\x8e\x82\xb5\x82\xbd" /* 試した */, "tried" },
};

/*! 偽の銘の割引 `30%引き` → `30% off`。原作（`flavor.c`）は `"%引き"`。 */
static void en_fix_discount(char *buf, size_t cap)
{
    static const char jp[] = "%\x88\xf8\x82\xab" /* %引き */;
    long pos;
    while ((pos = en_find(buf, jp)) >= 0) {
        en_cut(buf, (size_t)pos, sizeof(jp) - 1);
        if (!en_insert(buf, cap, (size_t)pos, "% off")) break;
    }
}

/*!
 * 末尾の `{…}` の中の偽の銘を英語にする。**`{` の直後か `, ` の直後で始まり、`,` か `}` の直前で終わる語だけ**を
 * 表で引く（遊ぶ人の書いた銘の中の字は触らない）。
 */
static void en_fix_braces(char *buf, size_t cap)
{
    char *open = strrchr(buf, '{');
    size_t i;
    if (!open) return;
    for (i = 0; i < sizeof(en_fake_inscription) / sizeof(en_fake_inscription[0]); i++) {
        const size_t jl = strlen(en_fake_inscription[i].jp);
        char *p = open + 1;
        while ((p = strstr(p, en_fake_inscription[i].jp)) != NULL) {
            const char before = p[-1];
            const char after = p[jl];
            if (((before == '{') || (before == ' ')) && ((after == ',') || (after == '}'))) {
                const size_t pos = (size_t)(p - buf);
                en_cut(buf, pos, jl);
                if (!en_insert(buf, cap, pos, en_fake_inscription[i].en)) return;
                open = strrchr(buf, '{');
                p = buf + pos;
            } else {
                p++;
            }
        }
    }
}

/*!
 * 能力の修正 `(+2StIn,+1Sp)`（`object_desc_bonus()`。値のすぐ後ろに札が詰まる）を `(+2 StIn,+1 Sp)` にする。
 * 値＝`(` か `,` の次の符号つきの数。その直後が字（札）か `?`（未判明）のときだけ空白を入れる。
 */
static void en_space_bonus(char *d, size_t cap)
{
    size_t i;
    for (i = 1; d[i]; i++) {
        if (((d[i] == '+') || (d[i] == '-')) && ((d[i - 1] == '(') || (d[i - 1] == ','))) {
            size_t j = i + 1;
            while ((d[j] >= '0') && (d[j] <= '9')) j++;
            if ((j > i + 1) && (((d[j] >= 'A') && (d[j] <= 'Z')) || ((d[j] >= 'a') && (d[j] <= 'z')) || (d[j] == '?'))) {
                if (!en_insert(d, cap, j, " ")) return;
            }
        }
    }
}

/*! 飾りの部分を英語にする。 */
static void en_fix_details(char *d, size_t cap)
{
    en_space_bonus(d, cap);
    en_apply_decor(d, cap);
    en_fix_charges(d, cap);
    en_fix_discount(d, cap);
    en_fix_braces(d, cap);
}

/* ============================================ アイテム（フック #6 の中身） */

/*! 種別の語を持つ品（識別すると `X of Y`、未識別だと `<flavor> X`）。 */
static const char *en_flavor_kind(int tval)
{
    switch (tval) {
    case TV_AMULET: return "Amulet";
    case TV_RING: return "Ring";
    case TV_STAFF: return "Staff";    case TV_WAND: return "Wand";
    case TV_ROD: return "Rod";
    case TV_SCROLL: return "Scroll";
    case TV_POTION: return "Potion";
    default: return NULL;
    }
}

/*! 魔法書の領域の語。 */
static const char *en_book_realm(int tval)
{
    switch (tval) {
    case TV_MAGERY_BOOK: return "Magery";
    case TV_FIRE_BOOK: return "Fire";
    case TV_AQUA_BOOK: return "Aqua";
    case TV_EARTH_BOOK: return "Earth";
    case TV_WIND_BOOK: return "Wind";
    case TV_HOLY_BOOK: return "Holy";
    case TV_DEATH_BOOK: return "Death";
    case TV_SYMBIOTIC_BOOK: return "Symbiotic";
    case TV_WITCH_BOOK: return "Witch";
    case TV_DRAKONITE_BOOK: return "Drakonite";
    case TV_CRUSADE_BOOK: return "Crusade";
    default: return NULL;
    }
}

/*! `object_desc()` が名前の全部の組み立てを通す種別か（金・塊・未知の種別は JP の道を早めに出る）。 */
static int en_full_path_tval(int tval)
{
    switch (tval) {
    case TV_SKELETON: case TV_BOTTLE: case TV_JUNK: case TV_TAROT: case TV_SPIKE: case TV_STONE: case TV_CHEST:
    case TV_FIGURINE: case TV_STATUE: case TV_CORPSE:
    case TV_BULLET: case TV_ROUND: case TV_SHELL: case TV_ROCKET: case TV_ARROW: case TV_BOLT: case TV_BOW:
    case TV_SWORD: case TV_POLEARM: case TV_HAFTED: case TV_DIGGING:
    case TV_BOOTS: case TV_GLOVES: case TV_HELM: case TV_CROWN: case TV_SHIELD: case TV_CLOAK: case TV_SOFT_ARMOR:
    case TV_HARD_ARMOR: case TV_LITE: case TV_AMULET: case TV_RING: case TV_CARD: case TV_TRUMP: case TV_SCRATCH_CARD:
    case TV_STAFF: case TV_WAND: case TV_ROD: case TV_SCROLL: case TV_POTION: case TV_FLASK: case TV_FOOD:
    case TV_MAGERY_BOOK: case TV_FIRE_BOOK: case TV_AQUA_BOOK: case TV_EARTH_BOOK: case TV_WIND_BOOK:
    case TV_HOLY_BOOK: case TV_DEATH_BOOK: case TV_SYMBIOTIC_BOOK: case TV_WITCH_BOOK: case TV_DRAKONITE_BOOK:
    case TV_CRUSADE_BOOK:
        return 1;
    default:
        return 0;
    }
}

/*! ロケットの材質・効果の前置（`flavor.c` の TV_ROCKET の英語の枝と同じ語）。 */
static void en_rocket_modstr(char *out, size_t cap, object_type *o_ptr)
{
    out[0] = '\0';
    {
        /* xtra4 = 材質（1..4）。反重力のロケットは材質に ROCKET_ANTIGRAV を足した値 */
        static const char *const material[] = { "", "Shot-shell-made ", "Baldar ", "Mithril ", "Adamantite " };
        const int m = (o_ptr->xtra4 >= ROCKET_ANTIGRAV) ? (o_ptr->xtra4 - ROCKET_ANTIGRAV) : o_ptr->xtra4;
        if ((m >= 1) && (m <= 4)) en_cat(out, cap, material[m]);
    }
    switch ((int)o_ptr->xtra3 - 1) {
    case TR_FORCE_WEAPON: en_cat(out, cap, "Mana "); break;
    case TR_CHAOTIC: en_cat(out, cap, "Chaotic "); break;
    case TR_VAMPIRIC: en_cat(out, cap, "Life-drain "); break;
    case TR_SLAY_EVIL: en_cat(out, cap, "Holy "); break;
    case TR_IMPACT: en_cat(out, cap, "Shattering "); break;
    case TR_BRAND_POIS: en_cat(out, cap, "Gas "); break;
    case TR_BRAND_ACID: en_cat(out, cap, "Acid "); break;
    case TR_BRAND_ELEC: en_cat(out, cap, "Lightning "); break;
    case TR_BRAND_FIRE: en_cat(out, cap, "Fire "); break;
    case TR_BRAND_COLD: en_cat(out, cap, "Freezing "); break;
    case TR_SLAY_GOOD: en_cat(out, cap, "Evil "); break;
    default:
        if (o_ptr->xtra4 >= ROCKET_ANTIGRAV) en_cat(out, cap, "Anti-gravity ");
        break;
    }
}

/*!
 * 名前の頭の語（複数形にする名詞）を探す。`s` の最初の ` of `・置き換え記号の手前までの、最後の語。
 * @param sub_start 置き換えた記号（`#` `%`）の最初の位置（無ければ SIZE_MAX。0 は無視する）
 * @param hs 頭の語の始まり
 * @param he 頭の語の終わりの次
 */
static void en_find_head(const char *s, size_t sub_start, size_t *hs, size_t *he)
{
    size_t cut = strlen(s);
    const char *of = strstr(s, " of ");
    size_t w;
    if (of && ((size_t)(of - s) < cut)) cut = (size_t)(of - s);
    if ((sub_start > 0) && (sub_start < cut)) cut = sub_start;
    while ((cut > 0) && (s[cut - 1] == ' ')) cut--;
    w = cut;
    while ((w > 0) && (s[w - 1] != ' ')) w--;
    *hs = w;
    *he = cut;
}

/*! `tmpl` の `#` を `modstr`、`%` を `kindname` に置き換えて `out` に書く。最初の置き換えの位置を返す（無ければ SIZE_MAX）。 */
static size_t en_substitute(char *out, size_t cap, const char *tmpl, const char *modstr, const char *kindname)
{
    size_t first = (size_t)-1;
    out[0] = '\0';
    for (; *tmpl; tmpl++) {
        if (*tmpl == '#') {
            if (first == (size_t)-1) first = strlen(out);
            en_cat(out, cap, modstr);
        } else if (*tmpl == '%') {
            if (first == (size_t)-1) first = strlen(out);
            en_cat(out, cap, kindname);
        } else {
            char one[2];
            one[0] = *tmpl;
            one[1] = '\0';
            en_cat(out, cap, one);
        }
    }
    return first;
}

/*!
 * 名前の部分を英語で組む。
 * @param name 出力（名前の部分。個数・冠詞から銘の終わりまで）
 * @param mode `object_desc()` の mode
 */
static void en_object_name(char *name, size_t cap, object_type *o_ptr, unsigned long mode)
{
    object_kind *k_ptr = k_info + o_ptr->k_idx;
    object_kind *flavor_k_ptr = k_info + k_ptr->flavor;
    cptr kindname = k_name + k_ptr->name;
    cptr modstr = "";
    char modbuf[MAX_NLEN];
    char subst[MAX_NLEN];
    char lead[MAX_NLEN], tail[MAX_NLEN], head[96];
    u32b flgs[TR_FLAG_SIZE];
    bool aware = object_is_aware(o_ptr) ? TRUE : FALSE;
    bool known = object_is_known(o_ptr) ? TRUE : FALSE;
    int unique_corpse = 0;
    cptr tmpl = kindname;
    size_t sub_start = (size_t)-1;
    int plural;
    const int tval = o_ptr->tval;
    const char *kind_word = en_flavor_kind(tval);

    { object_flags(o_ptr, flgs); }
    if ((mode & OD_STORE) || (o_ptr->ident & IDENT_STORE)) {
        aware = TRUE;
        known = TRUE;
    }
    if (mode & OD_FORCE_FLAVOR) {
        aware = FALSE;
        known = FALSE;
        flavor_k_ptr = k_ptr;
    }
    modbuf[0] = '\0';
    lead[0] = tail[0] = head[0] = '\0';

    switch (tval) {
    case TV_TAROT:
        modstr = tb_tr(tarot_info[o_ptr->pval].name);
        break;
    case TV_TRUMP:
        if (known) {
            monster_race *r_ptr = r_info + o_ptr->pval;
            if (!o_ptr->pval) {
                modstr = " (white)";
            } else if (!(r_ptr->flags1 & RF1_UNIQUE)) {
                char *t = r_name + r_ptr->name;
                snprintf(modbuf, sizeof(modbuf), " (%s%s)", is_a_vowel(*t) ? "an " : "a ", t);
                modstr = modbuf;
            } else {
                snprintf(modbuf, sizeof(modbuf), " (%s)", r_name + r_ptr->name);
                modstr = modbuf;
            }
        }
        break;
    case TV_FIGURINE:
    case TV_STATUE: {
        monster_race *r_ptr = r_info + o_ptr->pval;
        char *t = r_name + r_ptr->name;
        if (!(r_ptr->flags1 & RF1_UNIQUE)) {
            snprintf(modbuf, sizeof(modbuf), "%s%s", is_a_vowel(*t) ? "an " : "a ", t);
            modstr = modbuf;
        } else {
            modstr = t;
        }
        break;
    }
    case TV_CORPSE: {
        monster_race *r_ptr = r_info + o_ptr->pval;
        modstr = r_name + r_ptr->name;
        unique_corpse = (r_ptr->flags1 & RF1_UNIQUE) ? 1 : 0;
        break;
    }
    case TV_ROCKET:
        en_rocket_modstr(modbuf, sizeof(modbuf), o_ptr);
        if (modbuf[0]) modstr = modbuf;
        break;
    default:
        break;
    }

    /* ---- 全名（FULL_NAME）の品：識別済みなら名前そのものを使う ---- */
    if (aware && have_flag(flgs, TR_FULL_NAME)) {
        if (known && o_ptr->name1) tmpl = a_name + a_info[o_ptr->name1].name;
        else tmpl = kindname;
        kind_word = NULL;
    }

    /* ---- 構成要素 lead / head / tail ---- */
    if (kind_word && (tval != TV_CORPSE)) {
        const int fixed = (object_is_fixed_artifact(o_ptr) && aware) || ((k_ptr->gen_flags & TRG_INSTA_ART) && aware);
        if ((tval == TV_AMULET || tval == TV_RING) && fixed) {
            kind_word = NULL;      /* 固定アーティファクト／特別な指輪・アミュレット：種別の名前そのもの */
        }
    }

    if (kind_word) {
        cptr flavor = k_name + flavor_k_ptr->flavor_name;
        while (*flavor == ':') flavor++;   /* 在来の英語のデータに `Power::Mistletoe` のような二重の区切りがある */
        strcpy(head, kind_word);
        if (aware) {
            if (!en_ieq(kindname, kind_word)) {
                snprintf(tail, sizeof(tail), " of %s", kindname);
            }
        } else if (tval == TV_SCROLL) {
            snprintf(tail, sizeof(tail), " titled \"%s\"", flavor);
        } else {
            snprintf(lead, sizeof(lead), "%s ", flavor);
        }
    } else if (en_book_realm(tval)) {
        snprintf(lead, sizeof(lead), "%s ", en_book_realm(tval));
        strcpy(head, "Spellbook");
        snprintf(tail, sizeof(tail), " %s", kindname);
    } else if (tval == TV_CORPSE) {
        if (unique_corpse) {
            strncpy(head, kindname, sizeof(head) - 1);
            head[sizeof(head) - 1] = '\0';
            snprintf(tail, sizeof(tail), " of %s", modstr);
        } else {
            snprintf(lead, sizeof(lead), "%s ", modstr);
            strncpy(head, kindname, sizeof(head) - 1);
            head[sizeof(head) - 1] = '\0';
        }
    } else {
        size_t hs, he;
        sub_start = en_substitute(subst, sizeof(subst), tmpl, modstr, kindname);
        en_find_head(subst, sub_start, &hs, &he);
        if (he <= hs) {
            hs = 0;
            he = strlen(subst);
        }
        strncpy(lead, subst, hs);
        lead[hs] = '\0';
        strncpy(head, subst + hs, he - hs);
        head[he - hs] = '\0';
        strncpy(tail, subst + he, sizeof(tail) - 1);
        tail[sizeof(tail) - 1] = '\0';
    }

    /* ---- 複数形 ---- */
    plural = (!(mode & OD_NO_PLURAL) && (o_ptr->number != 1)) ? 1 : 0;
    if (plural) en_pluralize_word(head, sizeof(head));

    /* ---- 名前の本体 ---- */
    name[0] = '\0';
    en_cat(name, cap, lead);
    en_cat(name, cap, head);
    en_cat(name, cap, tail);

    /* ---- エゴ・アーティファクトの銘（識別済みのとき後ろに足す。JP は前に貼る） ---- */
    if (known) {
        if (o_ptr->art_name) {
            en_cat(name, cap, " ");
            en_cat(name, cap, quark_str(o_ptr->art_name));
        } else if (o_ptr->name1 && !have_flag(flgs, TR_FULL_NAME)) {
            en_cat(name, cap, " ");
            en_cat(name, cap, a_name + a_info[o_ptr->name1].name);
        } else if (!o_ptr->name1) {
            if (o_ptr->name2) {
                en_cat(name, cap, " ");
                en_cat(name, cap, e_name + e_info[o_ptr->name2].name);
            }
            if (o_ptr->inscription && my_strchr(quark_str(o_ptr->inscription), '#')) {
                cptr str = strchr(quark_str(o_ptr->inscription), '#');
                en_cat(name, cap, " ");
                en_cat(name, cap, str + 1);
            }
        }
    }

    /* ---- 個数・冠詞（名前の前） ---- */
    if (!(mode & OD_OMIT_PREFIX)) {
        char pre[24];
        pre[0] = '\0';
        if (o_ptr->number <= 0) {
            strcpy(pre, "no more ");
        } else if (o_ptr->number > 1) {
            snprintf(pre, sizeof(pre), "%d ", (int)o_ptr->number);
        } else if (known && (object_is_fixed_artifact(o_ptr) || o_ptr->art_name)) {
            if (strncmp(name, "The ", 4) != 0) strcpy(pre, "The ");
        } else if ((tval == TV_CORPSE) && unique_corpse) {
            if (strncmp(name, "The ", 4) != 0) strcpy(pre, "The ");
        } else if ((strncmp(name, "The ", 4) != 0) && (strncmp(name, "the ", 4) != 0)) {
            strcpy(pre, is_a_vowel((unsigned char)name[0]) ? "an " : "a ");
        }
        if (pre[0]) (void)en_insert(name, cap, 0, pre);
    }
}

#ifdef TB_NAME_DUMP
/*!
 * 検査用（`CL=/DTB_NAME_DUMP` で組んだときだけ入る）。環境変数 TB_NAME_DUMP が指すファイルへ、全部の種類・
 * アーティファクト・エゴ・モンスターの名前を書く。出荷の実行体には入らない。
 */
static void en_name_dump(const char *path)
{
    FILE *fp = fopen(path, "w");
    int k, a, e, r, v;
    object_type forge;
    if (!fp) return;
    for (k = 1; k < max_k_idx; k++) {
        object_kind *k_ptr = &k_info[k];
        const bool saved_aware = k_ptr->aware;
        if (!k_ptr->name) continue;
        for (v = 0; v < 6; v++) {
            char buf[MAX_NLEN * 2];
            object_wipe(&forge);
            object_prep(&forge, k);
            forge.number = (v == 2 || v == 3) ? 3 : ((v == 5) ? 0 : 1);
            k_ptr->aware = (v == 0) ? FALSE : TRUE;
            if (v >= 4) forge.ident |= IDENT_STORE;
            if (v == 1 || v == 2 || v == 3) forge.ident |= IDENT_KNOWN;
            object_desc(buf, &forge, (v == 3) ? OD_OMIT_PREFIX : 0);
            fprintf(fp, "K%03d v%d %s\n", k, v, buf);
        }
        k_ptr->aware = saved_aware;
    }
    for (a = 1; a < max_a_idx; a++) {
        artifact_type *a_ptr = &a_info[a];
        int kk;
        if (!a_ptr->name) continue;
        kk = lookup_kind(a_ptr->tval, a_ptr->sval);
        if (!kk) continue;
        for (v = 0; v < 2; v++) {
            char buf[MAX_NLEN * 2];
            object_wipe(&forge);
            object_prep(&forge, kk);
            forge.name1 = (byte)a;
            forge.number = 1;
            forge.ident |= IDENT_KNOWN | IDENT_STORE;
            forge.to_h = forge.to_d = forge.to_a = 3;
            object_desc(buf, &forge, v ? OD_NAME_ONLY : 0);
            fprintf(fp, "A%03d v%d %s\n", a, v, buf);
        }
    }
    {
        int base = lookup_kind(TV_SWORD, 1);
        for (e = 1; e < max_e_idx; e++) {
            char buf[MAX_NLEN * 2];
            if (!e_info[e].name || !base) continue;
            object_wipe(&forge);
            object_prep(&forge, base);
            forge.name2 = (byte)e;
            forge.number = 2;
            forge.ident |= IDENT_KNOWN;
            forge.to_h = forge.to_d = 5;
            object_desc(buf, &forge, 0);
            fprintf(fp, "E%03d %s\n", e, buf);
        }
    }
    for (r = 1; r < max_r_idx; r++) {
        monster_type m;
        int mode_i;
        static const int modes[] = { 0, MD_INDEF_VISIBLE, MD_PRON_VISIBLE, MD_POSSESSIVE, MD_ASSUME_VISIBLE | MD_INDEF_VISIBLE, MD_INDEF_HIDDEN | MD_ASSUME_HIDDEN };
        if (!r_info[r].name) continue;
        for (mode_i = 0; mode_i < (int)(sizeof(modes) / sizeof(modes[0])); mode_i++) {
            char buf[256];
            memset(&m, 0, sizeof(m));
            m.r_idx = (s16b)r;
            m.ap_r_idx = (s16b)r;
            m.ml = TRUE;
            monster_desc(buf, &m, modes[mode_i]);
            fprintf(fp, "R%04d m%d %s\n", r, mode_i, buf);
        }
    }
    for (v = 1; v < max_ms_idx; v++) {
        monster_type m;
        char buf[256];
        memset(&m, 0, sizeof(m));
        m.r_idx = m.ap_r_idx = 2;
        m.s_idx = (s16b)v;
        m.ml = TRUE;
        monster_desc(buf, &m, MD_INDEF_VISIBLE);
        fprintf(fp, "S%03d %s\n", v, buf);
    }
    {
        char tmp[600];
        char *t;
        roff_to_buf("You are a Bakram, one of several children. Your family treasures you. You have resolute black eyes, straight black hair, and white skin.", 60, tmp, sizeof(tmp));
        for (t = tmp; *t; t += strlen(t) + 1) fprintf(fp, "W[%s]\n", t);
    }
    {
        int saved_level = dun_level, round_i;
        dun_level = 12;
        for (round_i = 0; round_i < 40; round_i++) {
            for (r = MON_RANDOM_UNIQUE_1; r <= MON_RANDOM_UNIQUE_3; r++) {
                if (random_unique_generate(r)) fprintf(fp, "U%d %s\n", r, r_name + r_info[r].name);
            }
        }
        dun_level = saved_level;
    }
    fclose(fp);
}
#endif

int tb_object_desc_en(char *buf, void *obj, unsigned long mode)
{
    static int depth = 0;
    object_type *o_ptr = (object_type *)obj;
    char full[MAX_NLEN * 2];
    char nameonly[MAX_NLEN * 2];
    char name[MAX_NLEN * 2];
    size_t base_len;

    if (!tb_lang_enabled() || depth > 0 || !buf || !o_ptr) return 0;

#ifdef TB_NAME_DUMP
    {
        static int dumped = 0;
        const char *path = getenv("TB_NAME_DUMP");
        if (!dumped && path && path[0]) {
            dumped = 1;
            en_name_dump(path);
        }
    }
#endif

    depth++;
    object_desc(full, o_ptr, mode);
    if (!en_full_path_tval(o_ptr->tval)) {
        depth--;
        {
            static const char nothing[] = "(\x82\xc8\x82\xb5)" /* (なし) */;
            if (strcmp(full, nothing) == 0) strcpy(full, "(nothing)");
        }
        my_strcpy(buf, full, MAX_NLEN);
        return 1;
    }
    object_desc(nameonly, o_ptr, mode | OD_NAME_ONLY);
    depth--;

    base_len = strlen(nameonly);
    if (strncmp(full, nameonly, base_len) != 0) {
        /* 境目が取れない形（想定外）：名前は組み直さず、飾りの語だけ直す */
        en_fix_details(full, sizeof(full));
        my_strcpy(buf, full, MAX_NLEN);
        return 1;
    }

    en_object_name(name, sizeof(name), o_ptr, mode);
    {
        char det[MAX_NLEN * 2];
        my_strcpy(det, full + base_len, sizeof(det));
        en_fix_details(det, sizeof(det));
        en_cat(name, sizeof(name), det);
    }

    /* `(` `[` が前の字に空白なしで続く形（JP は `Torch(2500…)` のように詰める）に空白を入れる */
    {
        size_t i;
        for (i = 1; name[i]; i++) {
            if (en_is_lead((unsigned char)name[i]) && name[i + 1]) {
                i++;
                continue;
            }
            if (((name[i] == '(') || (name[i] == '[')) && (name[i - 1] != ' ') && ((unsigned char)name[i - 1] < 0x80)
                && (name[i - 1] != '(') && (name[i - 1] != '[')) {
                if (!en_insert(name, sizeof(name), i, " ")) break;
                i++;
            }
        }
    }

    my_strcpy(buf, name, MAX_NLEN);
    return 1;
}

/* ========================================== モンスター（フック #7 の中身） */

/*!
 * `monster_desc()` の英語の組み立て。原作の英語の枝（`monster2.c` の `#else`）と同じ形を、`#else` を組まずに
 * ここで書く。**乱数の使い方は JP の枝と同じ順**（幻覚のときの `one_in_(2)`・`get_rnd_line`・`randint1`）。
 */
int tb_monster_desc_en(char *desc_out, void *mon, int mode)
{
    monster_type *m_ptr = (monster_type *)mon;
    monster_race *r_ptr;
    cptr name;
    char desc[256];
    char silly_name[1024];
    bool seen, pron;
    bool named = FALSE;

    if (!tb_lang_enabled() || !desc_out || !m_ptr) return 0;

    r_ptr = r_info + m_ptr->ap_r_idx;
    name = r_name + r_ptr->name;

    /* 幻覚 */
    if (p_ptr->image && !(mode & MD_IGNORE_HALLU)) {
        if (one_in_(2)) {
            if (!get_rnd_line("silly_j.txt", m_ptr->r_idx, silly_name)) named = TRUE;
        }
        if (!named) {
            monster_race *hallu_race;
            do {
                hallu_race = r_info + randint1(max_r_idx - 1);
            } while (hallu_race->flags1 & RF1_UNIQUE);
            strcpy(silly_name, r_name + hallu_race->name);
        }
        name = silly_name;
    }

    /* 見えているか（見えると仮定する・見えないと仮定する・実際に見えている） */
    seen = (mode & MD_ASSUME_VISIBLE) ? TRUE : ((mode & MD_ASSUME_HIDDEN) ? FALSE : (m_ptr->ml ? TRUE : FALSE));
    pron = seen ? ((mode & MD_PRON_VISIBLE) ? TRUE : FALSE) : ((mode & MD_PRON_HIDDEN) ? TRUE : FALSE);

    if (!seen || pron) {
        /* 代名詞の表。行 = 性（中性・男・女）、列 = (INDEF_HIDDEN ? 4 : 0) | (POSSESSIVE ? 2 : 0) | (OBJECTIVE ? 1 : 0)。
         * 性は「代名詞にする」とき（pron）だけ使い、そうでなければ中性（隠れた敵は性を問わない）。 */
        static const char *const words[3][8] = {
            { "it", "it", "its", "itself", "something", "something", "something's", "itself" },
            { "he", "him", "his", "himself", "someone", "someone", "someone's", "himself" },
            { "she", "her", "her", "herself", "someone", "someone", "someone's", "herself" },
        };
        int row = 0;
        if (m_ptr && pron) {
            if (r_ptr->flags1 & RF1_FEMALE) row = 2;
            else if (r_ptr->flags1 & RF1_MALE) row = 1;
        }
        strcpy(desc, words[row][mode & 7]);
    } else if ((mode & (MD_POSSESSIVE | MD_OBJECTIVE)) == (MD_POSSESSIVE | MD_OBJECTIVE)) {
        strcpy(desc, (r_ptr->flags1 & RF1_FEMALE) ? "herself" : ((r_ptr->flags1 & RF1_MALE) ? "himself" : "itself"));
    } else {
        const char *ego_name = m_ptr->s_idx ? (ms_name + ms_info[m_ptr->s_idx].name) : NULL;

        if (is_pet(m_ptr) && m_ptr->ap_r_idx != m_ptr->r_idx) {
            /* 化けている仲間（狸・カメレオン） */
            snprintf(desc, sizeof(desc), "%s?", name);
        } else if ((r_ptr->flags1 & RF1_UNIQUE) && !(p_ptr->image && !(mode & MD_IGNORE_HALLU))) {
            strcpy(desc, name);
        } else if (mode & MD_INDEF_VISIBLE) {
            strcpy(desc, is_a_vowel((unsigned char)name[0]) ? "an " : "a ");
            en_cat(desc, sizeof(desc), name);
        } else {
            strcpy(desc, is_pet(m_ptr) ? "your " : "the ");
            en_cat(desc, sizeof(desc), name);
        }

        if (ego_name && ego_name[0]) {
            if (ego_name[0] != ' ') en_cat(desc, sizeof(desc), " ");
            en_cat(desc, sizeof(desc), ego_name);
        }
        if (m_ptr->nickname) {
            en_cat(desc, sizeof(desc), " called ");
            en_cat(desc, sizeof(desc), quark_str(m_ptr->nickname));
        }
        if ((m_ptr->fy == py) && (m_ptr->fx == px)) en_cat(desc, sizeof(desc), " (riding)");
        if ((mode & MD_IGNORE_HALLU) && m_ptr->ap_r_idx != m_ptr->r_idx) {
            en_cat(desc, sizeof(desc), " (");
            en_cat(desc, sizeof(desc), r_name + r_info[m_ptr->r_idx].name);
            en_cat(desc, sizeof(desc), ")");
        }
        if (mode & MD_POSSESSIVE) en_cat(desc, sizeof(desc), "'s");
    }

    /* 呼び手の器はほぼ全部 `char m_name[80]`。それより長くしない（あふれさせない） */
    if (strlen(desc) >= 80) desc[79] = '\0';
    strcpy(desc_out, desc);
    return 1;
}

/* ======================== ランダムユニークの名前（random-unique.c の穴。設計 §12.1） */

#include "tb_name_en_tables.inc"

/*! 直近に調べた候補（人名の表の種別と添字）。`random_unique_name()` は、候補が空いていれば `break` するので、
 *  ループを出た時点の最後の候補が選ばれた名前である（JP の `suffix` を後で使うのと同じ）。 */
static int ru_last_human = 0;
static int ru_last_index = -1;

/*!
 * @brief 重複の判定に使う鍵を、英語の形（`Arnold the `）に差し替える。
 * @param suffix JP が `《名前》` を書いた器（`RANDOM_UNIQUE_NAME_SIZE`）
 * @param human 人名の表（真）か大型の表（偽）か
 * @param index その表の中の添字（男性 0..255 → 女性 256..511。大型は 0..255）
 * @details 既に出ているランダムユニークの名前は `Arnold the Firecracker` なので、`strstr(名前, "Arnold the ")` で
 * 同じ人名の重複が分かる。` the ` まで含めるのは、`Arnold` が `Arnoldine` の頭に当たらないため。
 */
void tb_ru_suffix_en(char *suffix, size_t size, int human, int index)
{
    const char *en = NULL;
    if (!tb_lang_enabled() || !suffix) return;
    if (human && (index >= 0) && (index < TB_RU_HUMAN_EN_COUNT)) en = tb_ru_human_en[index];
    if (!human && (index >= 0) && (index < TB_RU_LARGE_EN_COUNT)) en = tb_ru_large_en[index];
    if (!en) return;
    ru_last_human = human;
    ru_last_index = index;
    snprintf(suffix, size, "%s the ", en);
}

/*!
 * @brief ランダムユニークの名前を `Arnold the Firecracker`（名前 the 二つ名）に組む。《》は付けない。
 * @param name JP が `二つ名の《名前》` を書いた器
 * @param profile `ru_profiles[]` の添字（二つ名の表と同じ並び）
 */
void tb_ru_name_en(char *name, size_t size, int profile)
{
    const char *personal = NULL;
    if (!tb_lang_enabled() || !name) return;
    if ((profile < 0) || (profile >= TB_RU_TITLE_EN_COUNT)) return;
    if (ru_last_human && (ru_last_index >= 0) && (ru_last_index < TB_RU_HUMAN_EN_COUNT)) personal = tb_ru_human_en[ru_last_index];
    if (!ru_last_human && (ru_last_index >= 0) && (ru_last_index < TB_RU_LARGE_EN_COUNT)) personal = tb_ru_large_en[ru_last_index];
    if (!personal) return;
    snprintf(name, size, "%s the %s", personal, tb_ru_title_en[profile]);
}
