/*!
 * @file an_name_ja.c
 * @brief `object_desc` / `monster_desc` の名前の部分の日本語版（
 * フック #6 #7）。
 *
 * @details 名前は内部では英語のまま持ち（セーブと gamedata が名前を鍵にしている。
 * 設計 §0.1・制約 4）、画へ出す所でだけ `lang/ja/names/<kind>.ja.tsv` を引く。
 * 表に無い名前は英語のまま組む（制約 5）。
 *
 * 組み立ては変愚に合わせる（変愚の `flavor/tval-description-switcher.cpp` と
 * `flavor/flavor-util.cpp` の型。**名前の型だけを借り、説明文は写さない**——J7）:
 *
 * | 種別 | 鑑定済み | 未鑑定 |
 * |---|---|---|
 * | 薬・指輪・杖・魔法棒・ロッド・アミュレット・キノコ | 〈効果〉の〈種別〉（スピードの薬） | 〈風味〉〈種別〉（マゼンタ色の薬） |
 * | 巻物 | 〈効果〉の巻物 | 「〈風味〉」と書かれた巻物 |
 * | それ以外 | 名前そのもの（ダガー） | 同左 |
 *
 * 数は「〈数〉〈助数詞〉の 」を頭に付ける（変愚の `describe_count_with_counter_suffix()`
 * と同じ助数詞）。冠詞（a/an/the）と英語の複数形は付けない。エゴは名前の前、
 * アーティファクトは『』で始まる名なら後ろ・「〜の」なら前（変愚と同じ）。
 *
 * 名前の後ろの飾り（`(1d4) (+0,+0)`・`(5000 turns)`・`{cursed}`）は原作の
 * `object_desc()` の後半がそのまま足す——その書式は `vstrnfmt()` のフック #2 が
 * `E:` で引く。
 */

#include "angband.h"

#include "game-input.h"
#include "mon-desc.h"
#include "mon-predicate.h"
#include "obj-desc.h"
#include "obj-knowledge.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player.h"

#include "z-textblock.h"

#include "an_lang_c.h"

#include <string.h>

/*!
 * @brief フック #16 の中身。`copy_to_textblock_with_coloring()`（`effects-info.c`）の日本語版。
 * @details 原作は 1 バイトずつ `"%c"` で継ぐ（数字だけ薄緑）。UTF-8 の多バイト字を割らない
 * よう、ASCII 以外は字の続き（10xxxxxx）までまとめて継ぐ。色分けは原作と同じ。
 * @return 日本語層が寝ていれば 0（原作の道へ）。
 */
int an_copy_to_textblock_with_coloring(textblock *tb, const char *s)
{
    if (!an_lang_enabled()) {
        return 0;
    }
    while (*s) {
        const unsigned char c = (unsigned char)*s;
        if (c < 0x80) {
            if (isdigit(c)) {
                textblock_append_c(tb, COLOUR_L_GREEN, "%c", *s);
            } else {
                textblock_append(tb, "%c", *s);
            }
            ++s;
            continue;
        }
        {
            char one[8];
            size_t n = 1;
            one[0] = *s;
            while ((n < sizeof(one) - 1) && s[n] && (((unsigned char)s[n] & 0xC0) == 0x80)) {
                one[n] = s[n];
                n++;
            }
            one[n] = '\0';
            textblock_append(tb, "%s", one);
            s += n;
        }
    }
    return 1;
}

/*!
 * @brief アイテムの種類の名前を引く。鍵は `<tval の名>:<object.txt の name>`
 * （`an_build_names.py`。同じ英語が別の種別に出る——`Speed` の薬と指輪）。
 * 無ければ英語の型を落とした名前（`& Dagger~` → `Dagger`）。
 */
static void an_ja_kind_name(char *out, size_t cap, const struct object_kind *kind)
{
    char key[200];
    const char *ja;

    strnfmt(key, sizeof(key), "%s:%s", tval_find_name(kind->tval), kind->name);
    ja = an_name("object", key);
    if (ja) {
        my_strcpy(out, ja, cap);
        return;
    }
    out[0] = '\0';
    (void)obj_desc_name_format(out, cap, 0, kind->name, NULL, false);
}

/*! 変愚の `describe_count_with_counter_suffix()` と同じ助数詞。 */
static const char *an_ja_counter(const struct object *obj, int number)
{
    switch (obj->tval) {
    case TV_BOLT:
    case TV_ARROW:
    case TV_POLEARM:
    case TV_STAFF:
    case TV_WAND:
    case TV_ROD:
    case TV_DIGGING:
        return "\xE6\x9C\xAC"; /* 本 */
    case TV_SCROLL:
        return "\xE5\xB7\xBB"; /* 巻 */
    case TV_POTION:
        return "\xE6\x9C\x8D"; /* 服 */
    case TV_MAGIC_BOOK:
    case TV_PRAYER_BOOK:
    case TV_NATURE_BOOK:
    case TV_SHADOW_BOOK:
    case TV_OTHER_BOOK:
        return "\xE5\x86\x8A"; /* 冊 */
    case TV_SOFT_ARMOR:
    case TV_HARD_ARMOR:
    case TV_DRAG_ARMOR:
    case TV_CLOAK:
        return "\xE7\x9D\x80"; /* 着 */
    case TV_SWORD:
    case TV_HAFTED:
    case TV_BOW:
        return "\xE6\x8C\xAF"; /* 振 */
    case TV_BOOTS:
        return "\xE8\xB6\xB3"; /* 足 */
    default:
        return (number < 10) ? "\xE3\x81\xA4" /* つ */ : "\xE5\x80\x8B" /* 個 */;
    }
}

/*! 種別の語（変愚の型の〈種別〉）。風味つきの種別だけ。無ければ NULL。 */
static const char *an_ja_flavored_base(int tval)
{
    switch (tval) {
    case TV_AMULET: return "\xE3\x82\xA2\xE3\x83\x9F\xE3\x83\xA5\xE3\x83\xAC\xE3\x83\x83\xE3\x83\x88"; /* アミュレット */
    case TV_RING: return "\xE6\x8C\x87\xE8\xBC\xAA"; /* 指輪 */
    case TV_STAFF: return "\xE6\x9D\x96"; /* 杖 */
    case TV_WAND: return "\xE9\xAD\x94\xE6\xB3\x95\xE6\xA3\x92"; /* 魔法棒 */
    case TV_ROD: return "\xE3\x83\xAD\xE3\x83\x83\xE3\x83\x89"; /* ロッド */
    case TV_POTION: return "\xE8\x96\xAC"; /* 薬 */
    case TV_MUSHROOM: return "\xE3\x82\xAD\xE3\x83\x8E\xE3\x82\xB3"; /* キノコ */
    case TV_SCROLL: return "\xE5\xB7\xBB\xE7\x89\xA9"; /* 巻物 */
    default: return NULL;
    }
}

/*! 本の種別の語（`& Book~ of Magic Spells #` ほかの日本語）。 */
static const char *an_ja_book_base(int tval)
{
    switch (tval) {
    case TV_MAGIC_BOOK: return "\xE9\xAD\x94\xE8\xA1\x93\xE6\x9B\xB8"; /* 魔術書 */
    case TV_PRAYER_BOOK: return "\xE7\xA5\x88\xE7\xA5\xB7\xE6\x9B\xB8"; /* 祈祷書 */
    case TV_NATURE_BOOK: return "\xE8\x87\xAA\xE7\x84\xB6\xE9\xAD\x94\xE8\xA1\x93\xE6\x9B\xB8"; /* 自然魔術書 */
    case TV_SHADOW_BOOK: return "\xE6\xAD\xBB\xE9\x9C\x8A\xE8\xA1\x93\xE3\x81\xAE\xE6\x9B\xB8"; /* 死霊術の書 */
    case TV_OTHER_BOOK: return "\xE7\xA5\x9E\xE7\xA7\x98\xE3\x81\xAE\xE6\x9B\xB8"; /* 神秘の書 */
    default: return NULL;
    }
}

/*!
 * @brief 風味の日本語。鍵は `<tval の名>:<風味の英語>`（同じ英語が別の種別で別の字に
 * なるため）で引き、無ければ英語だけで引く。形容の形（〜の／〜い）で終わらなければ
 * 「の」を足す（変愚の風味は「緑柱石の」「青い」の形）。
 */
static void an_ja_flavor(char *out, size_t cap, const struct object_kind *kind)
{
    const char *text = (kind && kind->flavor) ? kind->flavor->text : "";
    char key[160];
    const char *ja;
    size_t n;

    strnfmt(key, sizeof(key), "%s:%s", tval_find_name(kind->tval), text);
    ja = an_name("flavor", key);
    if (!ja) {
        ja = an_name("flavor", text);
    }
    if (!ja) {
        my_strcpy(out, text, cap);
        my_strcat(out, " ", cap);
        return;
    }
    my_strcpy(out, ja, cap);
    n = strlen(out);
    /* 「の」（E3 81 AE）・「い」（E3 81 84）で終わらなければ「の」を足す。 */
    if (!((n >= 3) && ((unsigned char)out[n - 3] == 0xE3) && ((unsigned char)out[n - 2] == 0x81)
            && (((unsigned char)out[n - 1] == 0xAE) || ((unsigned char)out[n - 1] == 0x84)))) {
        my_strcat(out, "\xE3\x81\xAE", cap);
    }
}

/*!
 * @brief 種類の名（`obj-desc.c` の `object_kind_name()`。P4 の知識の一覧・無視のメニュー）。
 * @details 未鑑定で風味があれば風味（＋種別。巻物は「〈風味〉」と書かれた巻物）、それ以外は
 * 種類の名（名前の表）。日本語層が寝ていれば 0（原作の道へ）。
 */
int an_object_kind_name_ja(char *buf, size_t max, const struct object_kind *kind, bool easy_know)
{
    if (!an_lang_enabled() || !buf || !kind) {
        return 0;
    }
    buf[0] = '\0';
    if (!easy_know && !kind->aware && kind->flavor) {
        const char *base = an_ja_flavored_base(kind->tval);
        if (kind->tval == TV_SCROLL) {
            strnfmt(buf, max, "\xE3\x80\x8C%s\xE3\x80\x8D\xE3\x81\xA8\xE6\x9B\xB8\xE3\x81\x8B\xE3\x82\x8C\xE3\x81\x9F%s",
                kind->flavor->text, base ? base : ""); /* 「〈風味〉」と書かれた巻物 */
        } else {
            char flavor[96];
            an_ja_flavor(flavor, sizeof(flavor), kind);
            strnfmt(buf, max, "%s%s", flavor, base ? base : "");
        }
        return 1;
    }
    an_ja_kind_name(buf, max, kind);
    return 1;
}

/*! 名前の後に置く字か（『』で始まる）。 */
static bool an_ja_is_after_body(const char *ja)
{
    return (strncmp(ja, "\xE3\x80\x8E", 3) == 0) /* 『 */ || (strncmp(ja, "\xE3\x80\x8A", 3) == 0) /* 《 */;
}

/*!
 * @brief フック #6 の中身。`object_desc()` の `obj_desc_name()` の代わりに名前の部分を組む。
 * @return 日本語層が寝ていれば 0（原作の道へ）。組んだら 1（`*end` を進める）。
 */
int an_obj_desc_name_ja(char *buf, size_t max, size_t *end, const struct object *obj, bool prefix,
    uint32_t mode, bool terse, const struct player *p)
{
    bool store = (mode & ODESC_STORE) ? true : false;
    bool spoil = (mode & ODESC_SPOIL) ? true : false;
    uint16_t number = (mode & ODESC_ALTNUM) ? (uint16_t)((mode & 0xFFFF0000) >> 16) : obj->number;
    bool aware = object_flavor_is_aware(obj) || store || spoil;
    bool known_art = object_is_known_artifact(obj);
    bool show_flavor = !terse && obj->kind->flavor;
    char name[160];
    char before[160];
    char after[160];
    char body[240];
    const char *base;
    const char *book;

    if (!an_lang_enabled()) {
        return 0;
    }
    if (store) {
        show_flavor = false;
    }
    if (aware && p && !OPT(p, show_flavors)) {
        show_flavor = false;
    }

    before[0] = '\0';
    after[0] = '\0';
    body[0] = '\0';

    /* 固有名（アーティファクト・エゴ）。 */
    if (known_art && obj->artifact) {
        const char *ja = an_name("artifact", obj->artifact->name);
        if (ja) {
            if (an_ja_is_after_body(ja)) {
                my_strcpy(after, ja, sizeof(after));
            } else {
                my_strcpy(before, ja, sizeof(before));
            }
        } else {
            strnfmt(after, sizeof(after), " %s", obj->artifact->name);
        }
    } else if ((obj->known->ego && !(mode & ODESC_NOEGO)) || (obj->ego && store)) {
        const char *ja = an_name("ego", obj->ego->name);
        if (ja) {
            my_strcpy(before, ja, sizeof(before));
        } else {
            strnfmt(after, sizeof(after), " %s", obj->ego->name);
        }
    }

    /* 本体。 */
    base = an_ja_flavored_base(obj->tval);
    book = an_ja_book_base(obj->tval);
    an_ja_kind_name(name, sizeof(name), obj->kind);
    if (obj->artifact && (aware || known_art || terse || !obj->kind->flavor)) {
        /* 原作もアーティファクトは種類の名前そのもの（`obj_desc_get_basename()`）。 */
        my_strcpy(body, name, sizeof(body));
    } else if (base && (obj->kind->flavor || (obj->tval == TV_SCROLL))) {
        char flavor[96];
        if (obj->tval == TV_SCROLL) {
            if (aware) {
                strnfmt(body, sizeof(body), "%s\xE3\x81\xAE%s", name, base); /* 〈効果〉の巻物 */
            } else if (show_flavor) {
                strnfmt(body, sizeof(body), "\xE3\x80\x8C%s\xE3\x80\x8D\xE3\x81\xA8\xE6\x9B\xB8\xE3\x81\x8B\xE3\x82\x8C\xE3\x81\x9F%s",
                    obj->kind->flavor ? obj->kind->flavor->text : "", base); /* 「〈風味〉」と書かれた巻物 */
            } else {
                my_strcpy(body, base, sizeof(body));
            }
        } else {
            an_ja_flavor(flavor, sizeof(flavor), obj->kind);
            if (aware && show_flavor) {
                strnfmt(body, sizeof(body), "%s\xE3\x81\xAE%s%s", name, flavor, base);
            } else if (aware) {
                strnfmt(body, sizeof(body), "%s\xE3\x81\xAE%s", name, base);
            } else if (show_flavor) {
                strnfmt(body, sizeof(body), "%s%s", flavor, base);
            } else {
                my_strcpy(body, base, sizeof(body));
            }
        }
    } else if (book) {
        if (terse) {
            my_strcpy(body, name, sizeof(body));
        } else {
            strnfmt(body, sizeof(body), "%s%s", book, name);
        }
    } else {
        my_strcpy(body, name, sizeof(body));
    }

    /* 数（変愚の「〈数〉〈助数詞〉の 」）。 */
    if (prefix) {
        if (number == 0) {
            strnfcat(buf, max, end, "%s", "\xE3\x82\x82\xE3\x81\x86\xE3\x81\xAA\xE3\x81\x84"); /* もうない */
        } else if (number > 1) {
            strnfcat(buf, max, end, "%u%s\xE3\x81\xAE ", (unsigned)number, an_ja_counter(obj, number));
        }
    }
    strnfcat(buf, max, end, "%s%s%s", before, body, after);
    return 1;
}

/*!
 * @brief モンスター一覧の 1 行ぶんの名前（`get_mon_name()`。`mon-desc.c`）の日本語版（修-65）。
 * @details 原作は `race->name`（英語）をそのまま組み、数が 2 以上なら複数形（`plural` か `s`）にする。
 * 日本語は複数形が無いので、訳があれば「数 + 名前」（固有は `[U]` + 名前）に組む。**訳が無い名前は
 * 0 を返して原作の道へ**（英語の複数形のまま。捏造しない）。形（`%3d ` の数の幅・`[U] `）は原作と同じ。
 * 名前を欄の幅で切るのは原作の `utf8_clipto()`（フック `an_utf8_clipto_hook()`。全角は 2 桁）が受け持つ。
 * @return 日本語層が寝ている・訳が無いなら 0
 */
int an_mon_name_ja(char *buf, size_t buflen, const struct monster_race *race, int num)
{
    const char *ja;

    if (!an_lang_enabled()) {
        return 0;
    }
    ja = an_name("monster", race->name);
    if (!ja) {
        return 0;
    }
    if (rf_has(race->flags, RF_UNIQUE)) {
        strnfmt(buf, buflen, "[U] %s", ja);
    } else {
        strnfmt(buf, buflen, "%3d %s", num, ja);
    }
    return 1;
}

/*!
 * @brief フック #7 の中身。`monster_desc()` の日本語版。
 * @details `the`・`a` は落とす。代名詞は「それ」「何か」「彼」「彼女」。所有格は「〜の」、
 * 再帰は「〜自身」。**`plural_aux()` は呼ばない**（`s` を付ける罠。Frox §8.43 ⑦）。
 * @return 日本語層が寝ていれば 0（原作の道へ）。
 */
int an_monster_desc_ja(char *desc, size_t max, const struct monster *mon, int mode)
{
    bool seen;
    bool use_pronoun;

    if (!an_lang_enabled()) {
        return 0;
    }
    seen = (mode & MDESC_SHOW) || (!(mode & MDESC_HIDE) && monster_is_visible(mon));
    use_pronoun = (seen && (mode & MDESC_PRO_VIS)) || (!seen && (mode & MDESC_PRO_HID));

    if (!seen || use_pronoun) {
        int msex = 0x00;
        const char *who;
        const char *choice;
        if (use_pronoun) {
            if (rf_has(mon->race->flags, RF_FEMALE)) {
                msex = 0x20;
            } else if (rf_has(mon->race->flags, RF_MALE)) {
                msex = 0x10;
            }
        }
        if ((mode & 0x04) != 0) {
            who = (msex == 0) ? "\xE4\xBD\x95\xE3\x81\x8B" /* 何か */ : "\xE4\xBD\x95\xE8\x80\x85\xE3\x81\x8B" /* 何者か */;
        } else if (msex == 0x10) {
            who = "\xE5\xBD\xBC"; /* 彼 */
        } else if (msex == 0x20) {
            who = "\xE5\xBD\xBC\xE5\xA5\xB3"; /* 彼女 */
        } else {
            who = "\xE3\x81\x9D\xE3\x82\x8C"; /* それ */
        }
        switch (mode & 0x03) {
        case 0x02:
            choice = "\xE3\x81\xAE"; /* の */
            break;
        case 0x03:
            choice = "\xE8\x87\xAA\xE8\xBA\xAB"; /* 自身 */
            break;
        default:
            choice = "";
            break;
        }
        if (((mode & 0x07) == 0x07) && (msex == 0)) {
            who = "\xE3\x81\x9D\xE3\x82\x8C"; /* 再帰は「それ自身」 */
        }
        strnfmt(desc, max, "%s%s", who, choice);
        return 1;
    }

    {
        const char *ja = an_name("monster", mon->race->name);
        const char *name = ja ? ja : mon->race->name;
        if ((mode & MDESC_POSS) && (mode & MDESC_OBJE)) {
            strnfmt(desc, max, "%s\xE8\x87\xAA\xE8\xBA\xAB", name); /* 〜自身 */
        } else if (mode & MDESC_POSS) {
            strnfmt(desc, max, "%s\xE3\x81\xAE", name); /* 〜の */
        } else {
            my_strcpy(desc, name, max);
        }
        if (!panel_contains(mon->grid.y, mon->grid.x)) {
            /* AH-59: カタログの句は `V:`（`an_mon_subject_ja()` と同じ表）にある。`an_tr()`（`E:`）では引けず、
             * 見る・狙う・モンスター一覧の名前の後ろに英語の ` (offscreen)` が残っていた。 */
            my_strcat(desc, an_tr_v(" (offscreen)"), max);
        }
    }
    return 1;
}
