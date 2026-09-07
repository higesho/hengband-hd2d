/*!
 * @file gb_name_en.c
 * @brief 名前の英語組み立て直し。
 *
 * JP ビルドの `object_desc()` / `monster_desc()` は**日本語の文法**で組む——
 * 数詞を頭に置き（`6つの Wooden Torch`）、エゴと銘を**前に生糊付けし**
 * （`of SpeedBoots`）、冠詞を作らない。E1 で名詞が英語になったので、
 * ここで**組み上がった文字列を英語の形へ直す**（フック #6 #7 の中身）。
 *
 * ## なぜ「組み直し」でなく「後処理」か
 * - `#else`（英語枝）の再コンパイルは幻想蛮怒の追加物（スペルカード・鍛冶印・
 *   アビリティカード）を知らない——それらは JP 枝にしか無い。
 * - JP 枝に全部組ませてから直せば、飾り（(+7,+9) や {銘}）は 1 バイトも
 *   作り直さずに済む（Sil-Q が飾りを差し引きで取ったのと同じ理屈の逆）。
 *
 * ## 作法
 * - `gb_lang_enabled()` が偽なら**即座に何もしない**（設計 §2 制約 1）。
 * - 直せない形はそのまま残す（制約 4。日本語が見えるだけで、壊れない）。
 * - SJIS の照合バイトは**すべて 16 進**で書く（この TU は UTF-8/BOM。
 *   リテラルに日本語を書くと execution-charset の差で照合が外れる）。
 *
 * この TU は `gb_shim.c` と同族——幻想蛮怒のヘッダを include してよい C。
 */

#include "angband.h"

#include "gb_lang_c.h"

#include <string.h>

/* ================================================================ 小さな道具 */

/*! CP932 の先行バイトか。 */
static int en_is_lead(unsigned char c)
{
    return ((c >= 0x81) && (c <= 0x9F)) || ((c >= 0xE0) && (c <= 0xFC));
}

/*! 純 ASCII か（0x80 以上を含まないか）。 */
static int en_is_ascii(const char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s >= 0x80) return 0;
    }
    return 1;
}

/*! `buf` の `pos` から `len` バイトを取り除く。 */
static void en_cut(char *buf, size_t pos, size_t len)
{
    memmove(buf + pos, buf + pos + len, strlen(buf + pos + len) + 1);
}

/*! `buf`（容量 cap）の `pos` に `ins` を差し込む。入り切らなければ何もしない。 */
static int en_insert(char *buf, size_t cap, size_t pos, const char *ins)
{
    const size_t blen = strlen(buf);
    const size_t ilen = strlen(ins);
    if (blen + ilen + 1 > cap) return 0;
    memmove(buf + pos + ilen, buf + pos, blen - pos + 1);
    memcpy(buf + pos, ins, ilen);
    return 1;
}

/*! 部分文字列の位置（無ければ -1）。 */
static long en_find(const char *buf, const char *pat)
{
    const char *p = strstr(buf, pat);
    return p ? (long)(p - buf) : -1;
}

/*!
 * @brief 名前の終わり＝飾りの始まりの位置。
 * @details JP の飾りは `(2500ターンの寿命)` のように**空白なしで**続く。
 * 最初の `(` `[` `{` を境にする（英語名の中の `(` は `Sake-Bug (Kai)` くらいで、
 * エゴ・銘の付く武具には無い）。
 */
static size_t en_name_end(const char *buf)
{
    size_t i;
    for (i = 0; buf[i]; i++) {
        const char c = buf[i];
        if ((c == '(') || (c == '[') || (c == '{')) return i;
        if (en_is_lead((unsigned char)c) && buf[i + 1]) i++;
    }
    return i;
}

/*! 複数形にする（`Torch`→`Torches`・`Berry`→`Berries`・`Boots`→そのまま s）。 */
static void en_pluralize_word(char *word, size_t cap)
{
    const size_t n = strlen(word);
    if ((n == 0) || (n + 3 > cap)) return;
    {
        const char a = word[n - 1];
        const char b = (n >= 2) ? word[n - 2] : '\0';
        if ((a == 's') || (a == 'x') || (a == 'h' && (b == 's' || b == 'c')) || (a == 'o')) {
            strcpy(word + n, "es");
        } else if ((a == 'y') && !strchr("aeiou", b)) {
            strcpy(word + n - 1, "ies");
        } else {
            strcpy(word + n, "s");
        }
    }
}

/* ================================================== 対の置き換え（飾りの表） */

/*! JP の飾り → 英語。**並びが優先順**（`sq_cat_decor` と同じ作法）。 */
static const struct { const char *jp; const char *en; } en_decor[] = {
    { "\x89\xf1\x95\xaa\x82\xcc\x96\x82\x97\xcd", " charges" },      /* 回分の魔力 */
    { "\x83\x5e\x81\x5b\x83\x93\x82\xcc\x8e\xf5\x96\xbd", " turns of light" }, /* ターンの寿命 */
    { "(\x8b\xf3)", "(empty)" },                                     /* (空) */
    { "(\x8f\x5e\x93\x64\x92\x86)", "(charging)" },                  /* (充電中) */
    { "(\x91\x95\x94\xf5\x92\x86)", "(worn)" },                      /* (装備中) */
    /* `+5 魔法防御` / `+5 解除` の後ろの句。`flavor.c:3311` / `3321` は
     * `object_desc_str()` で直に継ぎ足すのでフックを 1 つも通らない——
     * 上流の `#else` も `" to infravision"` の写しのままだった。ここで直す。 */
    { "\x96\x82\x96\x40\x96\x68\x8c\xe4", " to saving throw" },   /* 魔法防御 */
    { "\x89\xf0\x8f\x9c", " to disarming" },                         /* 解除 */

    /* **全角の括弧と印を ASCII へ**（2026-08-28 に決めた）。
     * `object_desc_str()` はフックを 1 つも通らないので、名前が組み上がった
     * ここで当てる。**この表は最後に走る**ので、`『名』` を `'名'` へ回す処理
     * （上の #6 の 3 番）はもう済んでいる。 */
    { "\x81\x79", "[" },                          /* 【 */
    { "\x81\x7a", "]" },                          /* 】 */
    { "\x81\x77", "'" },                          /* 『 */
    { "\x81\x78", "'" },                          /* 』 */
    { "\x81\x73", "\"" },                          /* 《 */
    { "\x81\x74", "\"" },                          /* 》 */
    { "\x81\x9a", "*" },                          /* ★ 固定アーティファクト */
    { "\x81\x99", "+" },                          /* ☆ ランダムアーティファクト */
    { "\x81\x9f", "#" },                          /* ◆ 小傘の傘 */
    { "\x81\x9b", "o" },                          /* ○ 面 */
    { "\x81\x40", " " },                          /* 全角の空白 */
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

/* ============================================ アイテム（フック #6 の中身） */

/*! 種別 → 未識別の英語名（`<flavor> Potion` の後ろ半分）。 */
static const char *en_kind_of_tval(int tval)
{
    switch (tval) {
    case TV_POTION: return "Potion";
    case TV_SCROLL: return "Scroll";
    case TV_RING: return "Ring";
    case TV_AMULET: return "Amulet";
    case TV_STAFF: return "Staff";
    case TV_WAND: return "Wand";
    case TV_ROD: return "Rod";
    case TV_FOOD: return "Mushroom";
    default: return NULL;
    }
}

/*!
 * @brief 「前へ生糊付けされた名」を名前の終わりへ回す。
 * @param tag `e_name`/`a_name` の英語（`of Speed` `'Roukanken'` など）
 * @return 1 なら動かした
 */
static int en_rotate_tag(char *buf, size_t cap, const char *tag)
{
    long pos;
    size_t end;
    char tail[MAX_NLEN * 2];

    if (!tag || !tag[0] || !en_is_ascii(tag)) return 0;
    pos = en_find(buf, tag);
    if (pos < 0) return 0;

    /* `of ...` と `'...'` は後置へ。それ以外（形容詞形）は空白だけ整える。 */
    if ((strncmp(tag, "of ", 3) != 0) && (tag[0] != '\'')) {
        const size_t at = (size_t)pos + strlen(tag);
        if ((buf[at] != '\0') && (buf[at] != ' ')) (void)en_insert(buf, cap, at, " ");
        return 1;
    }

    en_cut(buf, (size_t)pos, strlen(tag));
    if (buf[pos] == ' ') en_cut(buf, (size_t)pos, 1);

    end = en_name_end(buf);
    while ((end > 0) && (buf[end - 1] == ' ')) end--;
    tail[0] = ' ';
    strncpy(tail + 1, tag, sizeof(tail) - 2);
    tail[sizeof(tail) - 1] = '\0';
    (void)en_insert(buf, cap, end, tail);
    return 1;
}

void gb_object_desc_en_fix(char *buf, void *obj, unsigned long mode)
{
    object_type *o_ptr = (object_type *)obj;
    const size_t cap = MAX_NLEN; /* 呼び手の器は object_desc の約束どおり */
    int plural = 0;

    if (!gb_lang_enabled() || !buf || !buf[0] || !o_ptr) return;

    /* --- 1) 数詞（`6つの `）を `6 ` へ。 --- */
    if (o_ptr->number > 1) {
        size_t d = 0;
        while ((buf[d] >= '0') && (buf[d] <= '9')) d++;
        if ((d > 0) && en_is_lead((unsigned char)buf[d])) {
            size_t p = d;
            while (buf[p] && (buf[p] != ' ') && (p < d + 8)) {
                p += en_is_lead((unsigned char)buf[p]) ? 2 : 1;
            }
            if (buf[p] == ' ') {
                en_cut(buf, d, p - d); /* 助数詞＋「の」を落とし、`6 名前` にする */
                plural = 1;
            }
        }
    }

    /* --- 2) エゴと銘を後ろへ回す（JP は前へ生糊付けする）。 --- */
    if (o_ptr->name2) {
        (void)en_rotate_tag(buf, cap, e_name + e_info[o_ptr->name2].name);
    }
    if (o_ptr->name1) {
        (void)en_rotate_tag(buf, cap, a_name + a_info[o_ptr->name1].name);
    }
    if (o_ptr->art_name) {
        cptr temp = quark_str(o_ptr->art_name);
        if (temp && en_is_ascii(temp)) {
            if (strncmp(temp, "of ", 3) == 0) {
                /* JP は `XXXの` を前置した（flavor.c:2307）。探して回す。 */
                char pre[256];
                size_t n = strlen(temp + 3);
                if (n < sizeof(pre) - 3) {
                    long pos;
                    memcpy(pre, temp + 3, n);
                    pre[n] = (char)0x82; /* の */
                    pre[n + 1] = (char)0xcc;
                    pre[n + 2] = '\0';
                    pos = en_find(buf, pre);
                    if (pos >= 0) {
                        char tail[256];
                        en_cut(buf, (size_t)pos, strlen(pre));
                        snprintf(tail, sizeof(tail), " %s", temp);
                        (void)en_insert(buf, cap, en_name_end(buf), tail);
                    }
                }
            } else if (temp[0] == '\'') {
                /* JP は引用符を剥がして 『』 で後置した（flavor.c:2427）。戻す。 */
                char jp[256];
                size_t n = strlen(temp) - 2; /* 中身の長さ */
                if ((n > 0) && (n < sizeof(jp) - 5)) {
                    long pos;
                    jp[0] = (char)0x81; /* 『 */
                    jp[1] = (char)0x77;
                    memcpy(jp + 2, temp + 1, n);
                    jp[n + 2] = (char)0x81; /* 』 */
                    jp[n + 3] = (char)0x78;
                    jp[n + 4] = '\0';
                    pos = en_find(buf, jp);
                    if (pos >= 0) {
                        char tail[256];
                        en_cut(buf, (size_t)pos, strlen(jp));
                        snprintf(tail, sizeof(tail), " %s", temp);
                        (void)en_insert(buf, cap, (size_t)pos, tail);
                    }
                }
            }
        }
    }

    /* --- 3a) 未識別の巻物（`「XYZ」と書かれた巻物`）。題は乱数の音節なので
     *     カタログでは引けない。形ごと `Scroll titled "XYZ"` に組み直す。 --- */
    if (o_ptr->tval == TV_SCROLL) {
        static const char written[] =
            "\x82\xc6\x8f\x91\x82\xa9\x82\xea\x82\xbd\x8a\xaa\x95\xa8"; /* と書かれた巻物 */
        long open = en_find(buf, "\x81\x75");  /* 「 */
        long close = en_find(buf, "\x81\x76"); /* 」 */
        if ((open >= 0) && (close > open)
            && (strncmp(buf + close + 2, written, sizeof(written) - 1) == 0)) {
            char title[MAX_NLEN];
            char built[MAX_NLEN];
            size_t n = (size_t)(close - open) - 2;
            if (n < sizeof(title)) {
                memcpy(title, buf + open + 2, n);
                title[n] = '\0';
                if (en_is_ascii(title)) {
                    snprintf(built, sizeof(built), "Scroll titled \"%s\"", title);
                    en_cut(buf, (size_t)open, (size_t)(close - open) + 2 + sizeof(written) - 1);
                    (void)en_insert(buf, cap, (size_t)open, built);
                }
            }
        }
    }

    /* --- 3) 未識別（`青い薬` の形＝種別の直前まで日本語）。 --- */
    {
        size_t d = 0;
        while ((buf[d] >= '0') && (buf[d] <= '9')) d++;
        if (buf[d] == ' ') d++;
        if (en_is_lead((unsigned char)buf[d])) {
            const char *kind = en_kind_of_tval(o_ptr->tval);
            if (kind) {
                /* 名前の終わりから種別の日本語を探すのではなく、
                 * 飾りの前までを丸ごと flavor として引く（鍵は日本語の複合語。
                 * 訳が無ければそのまま＝日本語で出る。E3 の一覧が拾う）。 */
                size_t end = en_name_end(buf);
                char jp[MAX_NLEN];
                size_t n = end - d;
                while ((n > 0) && (buf[d + n - 1] == ' ')) n--;
                if ((n > 0) && (n < sizeof(jp))) {
                    const char *tr;
                    memcpy(jp, buf + d, n);
                    jp[n] = '\0';
                    tr = gb_tr(jp);
                    if (tr != jp && en_is_ascii(tr)) {
                        en_cut(buf, d, n);
                        (void)en_insert(buf, cap, d, tr);
                    }
                }
            }
        }
    }

    /* --- 4) 複数形。頭の名詞（` of `・` '`・飾りの前まで）の最後の語へ。 --- */
    if (plural) {
        size_t d = 0;
        size_t end;
        long cut_at;
        while ((buf[d] >= '0') && (buf[d] <= '9')) d++;
        if (buf[d] == ' ') d++;
        end = en_name_end(buf);
        cut_at = en_find(buf, " of ");
        if ((cut_at >= 0) && ((size_t)cut_at < end)) end = (size_t)cut_at;
        cut_at = en_find(buf, " '");
        if ((cut_at >= 0) && ((size_t)cut_at < end)) end = (size_t)cut_at;
        while ((end > d) && (buf[end - 1] == ' ')) end--;
        if ((end > d) && ((unsigned char)buf[end - 1] < 0x80) && (buf[end - 1] != '#')) {
            char word[64];
            size_t ws = end;
            while ((ws > d) && (buf[ws - 1] != ' ')) ws--;
            if ((end - ws) < sizeof(word) - 4) {
                char grown[64];
                memcpy(word, buf + ws, end - ws);
                word[end - ws] = '\0';
                if (en_is_ascii(word)) {
                    strcpy(grown, word);
                    en_pluralize_word(grown, sizeof(grown));
                    if (strcmp(grown, word) != 0) {
                        en_cut(buf, ws, end - ws);
                        (void)en_insert(buf, cap, ws, grown);
                    }
                }
            }
        }
    }

    /* --- 5) 単数の冠詞。JP は付けない（英語の #else は付ける）。 --- */
    if (!(mode & OD_OMIT_PREFIX) && (o_ptr->number == 1)) {
        const unsigned char head = (unsigned char)buf[0];
        if (((head >= 'A' && head <= 'Z') || (head >= 'a' && head <= 'z'))
            && (strncmp(buf, "The ", 4) != 0) && (strncmp(buf, "a ", 2) != 0)
            && (strncmp(buf, "an ", 3) != 0) && (strncmp(buf, "no more ", 8) != 0)) {
            if ((o_ptr->name1 && object_is_known(o_ptr)) || (o_ptr->art_name && object_is_known(o_ptr))) {
                (void)en_insert(buf, cap, 0, "The ");
            } else {
                (void)en_insert(buf, cap, 0, strchr("AEIOUaeiou", buf[0]) ? "an " : "a ");
            }
        }
    }

    /* --- 6) 飾り: 表を当て、`Torch(2500...` の詰まりに空白を入れる。 --- */
    en_apply_decor(buf, cap);
    {
        size_t i;
        for (i = 1; buf[i]; i++) {
            if (en_is_lead((unsigned char)buf[i]) && buf[i + 1]) { i++; continue; }
            if (((buf[i] == '(') || (buf[i] == '[')) && (buf[i - 1] != ' ')
                && ((unsigned char)buf[i - 1] < 0x80)) {
                if (!en_insert(buf, cap, i, " ")) break;
                i++;
            }
        }
    }

    /* 器の約束（MAX_NLEN）を超えていたら黙って切り詰める（呼び手の器を壊さない）。 */
    if (strlen(buf) >= cap) buf[cap - 1] = '\0';
}

/* ========================================== モンスター（フック #7 の中身） */

/*! 代名詞と隠れた敵（monster2.c の JP 表 → 英語の #else と同じ対）。 */
static const struct { const char *jp; const char *en; } en_pron[] = {
    { "\x89\xbd\x82\xa9\x82\xcc", "something's" },       /* 何かの */
    { "\x89\xbd\x82\xa9", "something" },                 /* 何か */
    { "\x92\x4e\x82\xa9\x82\xcc", "someone's" },         /* 誰かの */
    { "\x92\x4e\x82\xa9", "someone" },                   /* 誰か */
    { "\x82\xbb\x82\xea\x8e\xa9\x90\x67", "itself" },    /* それ自身 */
    { "\x82\xbb\x82\xea\x82\xcc", "its" },               /* それの */
    { "\x82\xbb\x82\xea", "it" },                        /* それ */
    { "\x94\xde\x8f\x97\x8e\xa9\x90\x67", "herself" },   /* 彼女自身 */
    { "\x94\xde\x8f\x97\x82\xcc", "her" },               /* 彼女の */
    { "\x94\xde\x8f\x97", "she" },                       /* 彼女 */
    { "\x94\xde\x8e\xa9\x90\x67", "himself" },           /* 彼自身 */
    { "\x94\xde\x82\xcc", "his" },                       /* 彼の */
    { "\x94\xde", "he" },                                /* 彼 */
};

/*! 名前の後ろに付く印（乗馬・変化）。 */
static const struct { const char *jp; const char *en; } en_msuffix[] = {
    { "(\x8f\xe6\x93\x6e\x92\x86)", "(riding)" },        /* (乗馬中) */
    { "(\x94\x77\x90\xb6\x92\x86)", "(riding)" },        /* (背生中) */
    { "(\x93\xf1\x90\xb6\x92\x86)", "(riding)" },        /* (二生中) */
    { "(\x83\x4a\x83\x81\x83\x8c\x83\x49\x83\x93\x82\xcc\x89\xa4)", "(Chameleon Lord)" },
    { "(\x83\x4a\x83\x81\x83\x8c\x83\x49\x83\x93)", "(Chameleon)" },
};

void gb_monster_desc_en_fix(char *desc, void *mon, unsigned long mode)
{
    monster_type *m_ptr = (monster_type *)mon;
    monster_race *r_ptr;
    /* 呼び手の器はほぼ全部 `char m_name[80]`。それより長くしない（あふれさせない）。 */
    const size_t cap = 80;

    if (!gb_lang_enabled() || !desc || !desc[0] || !m_ptr) return;
    r_ptr = &r_info[m_ptr->ap_r_idx];

    /* 代名詞・隠れた敵（desc 丸ごとが表の項目のとき）。 */
    {
        size_t i;
        for (i = 0; i < sizeof(en_pron) / sizeof(en_pron[0]); i++) {
            if (strcmp(desc, en_pron[i].jp) == 0) {
                strcpy(desc, en_pron[i].en);
                return;
            }
        }
    }

    /* ペットの「あなたの」→ `your `。 */
    {
        static const char anatano[] = "\x82\xa0\x82\xc8\x82\xbd\x82\xcc"; /* あなたの */
        if (strncmp(desc, anatano, sizeof(anatano) - 1) == 0) {
            en_cut(desc, 0, sizeof(anatano) - 1);
            (void)en_insert(desc, cap, 0, "your ");
        }
    }

    /* 二つ名つき異名（狸・カメレオンの `？`）: 全角？ → ASCII。 */
    {
        long pos;
        while ((pos = en_find(desc, "\x81\x48")) >= 0) { /* ？ */
            en_cut(desc, (size_t)pos, 2);
            (void)en_insert(desc, cap, (size_t)pos, "?");
        }
    }

    /* あだ名 「X」 → ` called "X"`。 */
    {
        long open = en_find(desc, "\x81\x75"); /* 「 */
        long close = en_find(desc, "\x81\x76"); /* 」 */
        if ((open >= 0) && (close > open)) {
            en_cut(desc, (size_t)close, 2);
            (void)en_insert(desc, cap, (size_t)close, "\"");
            en_cut(desc, (size_t)open, 2);
            (void)en_insert(desc, cap, (size_t)open, " called \"");
        }
    }

    /* 乗馬などの印。 */
    {
        size_t i;
        for (i = 0; i < sizeof(en_msuffix) / sizeof(en_msuffix[0]); i++) {
            long pos = en_find(desc, en_msuffix[i].jp);
            if (pos >= 0) {
                en_cut(desc, (size_t)pos, strlen(en_msuffix[i].jp));
                (void)en_insert(desc, cap, (size_t)pos, en_msuffix[i].en);
            }
        }
    }

    /* 冠詞。ユニークは裸名のまま（JP と同じ）。非ユニークだけ the / a を付ける。 */
    {
        const unsigned char head = (unsigned char)desc[0];
        if (((head >= 'A' && head <= 'Z') || (head >= 'a' && head <= 'z'))
            && !(r_ptr->flags1 & RF1_UNIQUE)
            && (strncmp(desc, "your ", 5) != 0) && (strncmp(desc, "the ", 4) != 0)
            && (strncmp(desc, "a ", 2) != 0) && (strncmp(desc, "an ", 3) != 0)) {
            if (mode & MD_INDEF_VISIBLE) {
                (void)en_insert(desc, cap, 0, strchr("AEIOUaeiou", desc[0]) ? "an " : "a ");
            } else {
                (void)en_insert(desc, cap, 0, "the ");
            }
        }
    }

    if (strlen(desc) >= cap) desc[cap - 1] = '\0';
}
