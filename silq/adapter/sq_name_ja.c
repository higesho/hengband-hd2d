/*!
 * @file sq_name_ja.c
 * @brief 名前の日本語での組み立て。
 *
 * `object_desc()` と `monster_desc()` の**日本語版**。`silq/src` からは
 * `sq_lang_c.h` の 2 つの関数としてだけ見える。
 *
 * ## C である理由（設計 §2 への追記）
 * 設計は `sq_name_ja.{h,cpp}`（C++）と書いていたが、**C にした**。
 * 組み立てには `object_type` / `monster_type` / `k_info` / `a_info` / `e_info` /
 * `flavor_info` の中身が要る。C++ 側（`sq_shim.h` 越し）へ運ぶには
 * それらを丸ごと写す構造体を作ることになり、**上流が 1 つ欄を足すたびに
 * 写しを直す**羽目になる。ここは `sq_shim.c` や `sq_bootstrap.c` と同じ
 * 「Sil-Q のヘッダを見てよい C の TU」に置くのが素直である。
 *
 * ## 760 行を写さないための手（設計 §6.3 の「書き下ろす」の実際）
 * `object_desc()` の出力は **「名前」＋「飾り」** の 2 段でできていて、
 * 飾りは名前の**後ろに足されるだけ**である（`object1.c:912-1310`）。
 * しかも飾りの区切りは `mode` そのもの:
 *
 * | mode | どこまで出るか |
 * |---|---|
 * | `< 0` | 頭（数・冠詞）＋ 基の名だけ |
 * | `0` | ＋ `of X`・アーティファクト名・銘 = **名前の全部** |
 * | `1` | ＋ 箱の状態・打撃ダイス・回避/防護 |
 * | `2` | ＋ 充填数・松明の残り・`<+n>`・`(recharging)` |
 * | `3` | ＋ `{刻銘}` |
 *
 * よって **`object_desc(mode)` と `object_desc(0)` を両方呼び、後者を前者の
 * 頭から差し引けば「飾りだけ」が取れる**。飾りは数字と括弧が主で、
 * 英語のままでも意味が落ちない（P3 以降で必要なら別途訳す）。
 * こうすると**組み替えるのは名前だけ**で済み、写しは 1 行も要らない。
 *
 * **再入よけが要る**——`object_desc()` の頭は `sq_object_desc_ja()` を呼ぶので、
 * 素で呼ぶと無限に潜る。`sq_ja_busy` で止める。
 *
 * ## 文字コード
 * ここを通る文字列はすべて **CP932**（設計 §3.1）。
 * 作った `lib-ja/edit` から来る名前は既に CP932 なので、変換は 1 度も要らない。
 */

#include "angband.h"

#include <string.h>

#include "sq_lang_c.h"

/*! @brief `s` の先頭の字のバイト数（1 か 2）。定義は下（`sq_is_suffix_title` の手前）。 */
static int sq_char_len(cptr s);

/*! 再入よけ。`object_desc()` / `monster_desc()` を内側から呼ぶための旗。 */
static int sq_ja_busy = 0;

/* ==================================================================== 継ぎ足し */

/*!
 * @brief 上限を守って継ぎ足す。**`my_strcat` と違い 2 バイト文字を割らない**。
 * @details 溢れたときに CP932 の先行バイトだけが残ると、画面側の結合が
 * 次のセルを巻き込んで化ける（`sq_frame.cpp` の註記）。
 */
static void sq_cat(char *buf, size_t max, cptr add)
{
    size_t have;
    size_t room;
    int fit;

    if (!add || !add[0]) {
        return;
    }
    have = strlen(buf);
    if ((have + 1) >= max) {
        return;
    }
    room = max - have - 1;
    fit = sq_clip(add, (int)((room > 0x7FFF) ? 0x7FFF : room));
    if (fit <= 0) {
        return;
    }
    memcpy(buf + have, add, (size_t)fit);
    buf[have + (size_t)fit] = '\0';
}

/*! @brief 数を継ぎ足す。 */
static void sq_cat_num(char *buf, size_t max, int n)
{
    char tmp[16];

    sprintf(tmp, "%d", n);
    sq_cat(buf, max, tmp);
}

/*! @brief 最後の字の位置と長さを取る（頭から歩く）。空なら 0 を返す。 */
static size_t sq_last_char(cptr s, int *out_len)
{
    size_t len = 0;
    size_t last = 0;

    *out_len = 0;
    while (s[len]) {
        const int step = sq_char_len(s + len);
        last = len;
        *out_len = step;
        len += step;
    }
    return last;
}

/*!
 * @brief `of X` の名（`幽閉` `回避`）を名詞の前へ置く。**いつも「の」で繋ぐ**。
 * @details こちらは `k_name` から来る**素の名詞**なので、繋ぎは必ず要る
 * （`幽閉` ＋ `杖` → `幽閉の杖`）。既に「の」で終わっていたら足さない。
 */
static void sq_cat_kind(char *buf, size_t max, cptr kind)
{
    int last_len = 0;
    size_t last;

    if (!kind || !kind[0]) {
        return;
    }
    sq_cat(buf, max, kind);

    last = sq_last_char(kind, &last_len);
    if ((last_len == 2) && ((unsigned char)kind[last] == 0x82)
        && ((unsigned char)kind[last + 1] == 0xCC)) {
        return; /* 既に「の」で終わっている */
    }
    sq_cat(buf, max, "\x82\xCC"); /* の */
}

/*!
 * @brief 銘を名の前へ置く形にする（設計 §6.3「銘は常に前」）。
 *
 * @details **「の」を足すのは、閉じ鉤括弧か ASCII で終わるときだけ**である。
 * 訳文の側が既に繋がる形で書かれているので、こちらが足すのは
 * 「名前そのもの」を名詞へ繋ぐときに限られる:
 *
 * | 銘 | 足すか | 出来上がり |
 * |---|---|---|
 * | `守りの` | 足さない | `守りの鎖帷子` |
 * | `多くのルーンを刻んだ` | 足さない | `多くのルーンを刻んだ丸盾`（連体形） |
 * | `燦然たる` | 足さない | `燦然たる丸盾` |
 * | `『アングリスト』` | **足す** | `『アングリスト』のダガー` |
 *
 * **末尾が「の」かどうかで判じるのは雑だった**——`刻んだ` を拾えず
 * `刻んだの丸盾` になった（`--name-check` が見つけた）。
 *
 * **`（毒）` のように丸括弧で始まるものは前に置かない**——英語でも
 * 名の後ろに付く別枠なので、日本語でも後ろへ回す（呼び手が判じる）。
 */
static void sq_cat_title(char *buf, size_t max, cptr title)
{
    size_t last;
    int last_len;

    if (!title || !title[0]) {
        return;
    }
    sq_cat(buf, max, title);

    last = sq_last_char(title, &last_len);
    if (last_len == 1) {
        sq_cat(buf, max, "\x82\xCC"); /* ASCII で終わる名。の で繋ぐ */
        return;
    }
    /* 』（0x81 0x78）と 」（0x81 0x76）で終わる名。の で繋ぐ。 */
    if (((unsigned char)title[last] == 0x81)
        && (((unsigned char)title[last + 1] == 0x78)
            || ((unsigned char)title[last + 1] == 0x76))) {
        sq_cat(buf, max, "\x82\xCC");
    }
}

/*!
 * @brief `s` の先頭の字のバイト数（1 か 2）。**CP932 を判って歩くため**。
 * @details `~`（0x7E）は後続バイトとして正当なので、素のバイト比較で印を
 * 落とすと 2 バイト文字が割れる。走査は必ずこれを通す。
 */
static int sq_char_len(cptr s)
{
    if (sq_is_sjis_lead((unsigned char)s[0]) && s[1]) {
        return 2;
    }
    return 1;
}

/*! @brief 丸括弧（全角・半角とも）で始まるか。始まるなら名の後ろへ回す銘である。 */
static int sq_is_suffix_title(cptr title)
{
    if (!title || !title[0]) {
        return 0;
    }
    if (title[0] == '(') {
        return 1;
    }
    /* 全角の（は CP932 で 0x81 0x69。 */
    return (((unsigned char)title[0] == 0x81) && ((unsigned char)title[1] == 0x69)) ? 1 : 0;
}

/*!
 * @brief `&`（冠詞）・`~`（複数）・`#`（風味の入る所）を落とす（設計 §6.3）。
 * @details **`#` は展開しない**——日本語では風味を頭に置くので位置が英語と違う。
 * 印の跡に残る空白も畳む（`& # 指輪~` → `指輪`）。
 */
static void sq_strip_marks(char *dst, size_t cap, cptr src)
{
    char *w = dst;
    cptr s = src;

    while (*s && ((size_t)(w - dst) < cap - 3)) {
        const int step = sq_char_len(s);
        if (step == 1) {
            if ((*s == '&') || (*s == '~') || (*s == '#')) {
                s++;
                continue;
            }
            if ((*s == ' ') && (w == dst)) {
                s++;
                continue; /* `& ` の後ろの空白 */
            }
            *w++ = *s++;
            continue;
        }
        *w++ = *s++;
        *w++ = *s++;
    }
    while ((w > dst) && (w[-1] == ' ')) {
        w--;
    }
    *w = '\0';
    {
        char *head = dst;
        while (*head == ' ') {
            head++;
        }
        if (head != dst) {
            memmove(dst, head, strlen(head) + 1);
        }
    }
}

/* ====================================================================== 助数詞 */

/*!
 * @brief 品の助数詞（設計 §6.3「数」）。
 *
 * @details 設計は `object.ja.txt` の註記行 `#counter:本` で持つと書いていたが、
 * **tval の表にした**（設計 §6.3 への追記）。助数詞は「物の種類」の性質で、
 * Sil-Q の tval はまさにその種類で切れている。品ごとに持つと 195 行の
 * ほとんど同じ値を並べることになり、上流が品を足すたびに書き足す要も生じる。
 *
 * tval で割り切れないものだけ、下の `sval` の枝で拾う。
 */
static cptr sq_counter_for(const object_type *o_ptr)
{
    switch (o_ptr->tval) {
    case TV_ARROW:
    case TV_BOW:
    case TV_SWORD:
    case TV_POLEARM:
    case TV_HAFTED:
    case TV_DIGGING:
    case TV_STAFF:
    case TV_HORN:
    case TV_POTION:
    case TV_FLASK:
        return "\x96\x7B"; /* 本 */

    case TV_SOFT_ARMOR:
    case TV_MAIL:
    case TV_CLOAK:
        return "\x92\x85"; /* 着 */

    case TV_BOOTS:
    case TV_GLOVES:
        return "\x91\x67"; /* 組 */

    case TV_NOTE:
        return "\x96\x87"; /* 枚 */

    case TV_SKELETON:
        return "\x91\xCC"; /* 体 */

    case TV_LIGHT:
        /* 松明は「本」、ランタンと燈は「個」。 */
        return (o_ptr->sval == SV_LIGHT_TORCH) ? "\x96\x7B" : "\x8C\xC2";

    default:
        return "\x8C\xC2"; /* 個 */
    }
}

/* ============================================================ 飾り（括弧と刻銘） */

/*!
 * @brief 飾りの中の英語を訳し替える表（英語 → CP932）。**長い綴りを先に置く**。
 *
 * @details 飾りは `object_desc()` が名前の後ろへ足す
 * `" (5 charges)"` `" (Locked)"` `" {special}"` のたぐいで、
 * **名前の組み立て（フック #6）の外**にある。カタログでは引けない
 * ——数が中に挟まる（`(5 charges)`）ので鍵にならないからである。
 * そこで**部分文字列の置き換え**で訳す。
 *
 * 括弧は **ASCII のまま**にしてある。`(+3,1d5)` のダイス表示と揃うのと、
 * 開き括弧だけが ASCII で閉じが全角、という不揃いを避けるため。
 *
 * `uncursed}` は `cursed}` を含むので**先に置く**。表の並びがそのまま優先順である。
 */
static const struct {
    const char *en;
    const char *ja;
} sq_decor[] = {
    { " (Multiple Traps)",   " (\x95\xA1\x90\x94\x82\xCC\xE3\xA9)" },   /*  (複数の罠) */
    { " (Poison Needle)",    " (\x93\xC5\x90\x6A)" },                   /*  (毒針) */
    { " (Flame Trap)",       " (\x89\x8A\x82\xCC\xE3\xA9)" },           /*  (炎の罠) */
    { " (Gas Trap)",         " (\x83\x4B\x83\x58\x82\xCC\xE3\xA9)" },   /*  (ガスの罠) */
    { " (recharging)",       " (\x8F\x5B\x93\x55\x92\x86)" },           /*  (充填中) */
    { " (disarmed)",         " (\xE3\xA9\x89\xF0\x8F\x9C)" },           /*  (罠解除) */
    { " (unlocked)",         " (\x8A\x4A\x8F\xF9)" },                   /*  (開ロック) */
    { " (searched)",         " (\x92\x54\x8D\xF5\x8D\xCF)" },           /*  (探索済) */
    { " (Locked)",           " (\x8E\x7B\x8F\xF9)" },                   /*  (施錠) */
    { " (empty)",            " (\x8B\xF3)" },                           /*  (空) */
    { " charges)",           " \x89\xF1\x95\xAA)" },                    /*  回分) */
    { " charge)",            " \x89\xF1\x95\xAA)" },                    /*  回分) */
    { "(used ",              "(\x8E\x67\x97\x70 " },                    /* (使用  */
    { " times)",             " \x89\xF1)" },                            /*  回) */
    { " time)",              " \x89\xF1)" },                            /*  回) */
    { " turns)",             " \x83\x5E\x81\x5B\x83\x93)" },            /*  ターン) */
    { "artefact, cursed}",   "\x96\xC1, \x8E\xF4\x82\xA2}" },           /* 銘, 呪い} */
    { "special, cursed}",    "\x93\xC1\x95\xCA, \x8E\xF4\x82\xA2}" },   /* 特別, 呪い} */
    { "indestructible}",     "\x95\x73\x89\xF3}" },                     /* 不壊} */
    { "uncursed}",           "\x8E\xF4\x82\xA2\x96\xB3\x82\xB5}" },     /* 呪い無し} */
    { "artefact}",           "\x96\xC1}" },                             /* 銘} */
    { "average}",            "\x95\xC0}" },                             /* 並} */
    { "special}",            "\x93\xC1\x95\xCA}" },                     /* 特別} */
    { "broken}",             "\x89\xF3\x82\xEA}" },                     /* 壊れ} */
    { "cursed}",             "\x8E\xF4\x82\xA2}" },                     /* 呪い} */
    { "empty}",              "\x8B\xF3}" },                             /* 空} */
    { "tried}",              "\x8E\x8E\x82\xB5\x82\xBD}" },             /* 試した} */
    { "fine}",               "\x8F\xE3\x95\xA8}" },                     /* 上物} */
    { "% off}",              "% \x88\xF8\x82\xAB}" },                   /* % 引き} */
    { NULL, NULL },
};

/*!
 * @brief 飾りを訳しながら `out` へ継ぐ。
 * @param out 継ぎ先（CP932）
 * @param max `out` の大きさ
 * @param tail 英語の飾り（`object_desc()` の全文から名前を差し引いた残り）
 *
 * @details 左から 1 文字ずつ見て、表のどれかに当たればそれを置く。
 * 当たらなければその 1 バイトをそのまま写す。**刻銘は遊ぶ人が打った字**なので、
 * 表に無ければ手を触れない。
 */
static void sq_cat_decor(char *out, size_t max, const char *tail)
{
    size_t i = 0;

    while (tail[i] != '\0') {
        int hit = 0;
        int k;
        for (k = 0; sq_decor[k].en != NULL; k++) {
            const size_t n = strlen(sq_decor[k].en);
            if (strncmp(tail + i, sq_decor[k].en, n) == 0) {
                sq_cat(out, max, sq_decor[k].ja);
                i += n;
                hit = 1;
                break;
            }
        }
        if (!hit) {
            char one[2];
            one[0] = tail[i];
            one[1] = '\0';
            sq_cat(out, max, one);
            i++;
        }
    }
}

/* ============================================================ 実体（フック #6） */

int sq_object_desc_ja(char *buf, size_t max, const void *obj, int pref, int mode)
{
    const object_type *o_ptr = (const object_type *)obj;
    const object_kind *k_ptr;
    char full[256];
    char name_en[256];
    char out[256];
    cptr basenm;
    cptr modstr = "";
    cptr title = "";
    cptr kindnm = "";
    int append_name = 0;
    int known;
    int aware;
    int spoil;
    int flavor;
    size_t head_len;
    const char *s;
    char *w;
    char stripped[128];
    char kindbuf[128];

    if (!sq_lang_enabled() || sq_ja_busy || !o_ptr || !buf || (max < 8)) {
        return 0;
    }

    /* --- 英語の全文と「名前だけ」を作る（飾りを取り出すため）。 --- */
    sq_ja_busy = 1;
    object_desc(full, sizeof(full), o_ptr, pref, mode);
    object_desc(name_en, sizeof(name_en), o_ptr, pref, (mode < 0) ? mode : 0);
    sq_ja_busy = 0;

    /* `(nothing)` は品が無いマス。ここで訳す（装備の画に何度も出る）。 */
    if (streq(full, "(nothing)")) {
        my_strcpy(buf, sq_tr("(nothing)"), max);
        return 1;
    }

    k_ptr = &k_info[o_ptr->k_idx];
    aware = object_aware_p(o_ptr) ? 1 : 0;
    known = object_known_p(o_ptr) ? 1 : 0;
    spoil = (o_ptr->ident & IDENT_SPOIL) ? 1 : 0;
    flavor = k_ptr->flavor ? 1 : 0;
    if (spoil) {
        /* 一覧・鍛冶の画では風味を出さず、判っている扱いにする（`object1.c:613`）。 */
        flavor = 0;
        aware = 1;
        known = 1;
    }

    /*
     * 基の名。**風味を持つ種類は `object1.c` が英語の綴りをべた書きしている**
     * （`"& # Ring~"` など）。あれは表に無いのでカタログで引く
     * （`silq/lang/ja/ui/messages.ja.txt` に `E:& # Ring~` がある）。
     */
    basenm = (k_name + k_ptr->name);
    switch (o_ptr->tval) {
    case TV_AMULET:
        if (artefact_p(o_ptr) && aware) {
            break;
        }
        modstr = flavor_text + flavor_info[k_ptr->flavor].text;
        append_name = aware;
        basenm = sq_tr(flavor ? "& # Amulet~" : "& Amulet~");
        break;
    case TV_RING:
        if (artefact_p(o_ptr) && aware) {
            break;
        }
        modstr = flavor_text + flavor_info[k_ptr->flavor].text;
        append_name = aware;
        basenm = sq_tr(flavor ? "& # Ring~" : "& Ring~");
        break;
    case TV_STAFF:
        modstr = flavor_text + flavor_info[k_ptr->flavor].text;
        append_name = aware;
        basenm = sq_tr(flavor ? "& # Staff~" : "& Staff~");
        break;
    case TV_HORN:
        modstr = flavor_text + flavor_info[k_ptr->flavor].text;
        append_name = aware;
        basenm = sq_tr(flavor ? "& # Horn~" : "& Horn~");
        break;
    case TV_POTION:
        modstr = flavor_text + flavor_info[k_ptr->flavor].text;
        append_name = aware;
        basenm = sq_tr(flavor ? "& # Potion~" : "& Potion~");
        break;
    case TV_FOOD:
        if (o_ptr->sval >= SV_FOOD_MIN_FOOD) {
            break;
        }
        modstr = flavor_text + flavor_info[k_ptr->flavor].text;
        append_name = aware;
        basenm = sq_tr(flavor ? "& # Herb~" : "& Herb~");
        break;
    default:
        break;
    }
    if (!flavor) {
        modstr = "";
    }

    /* 基の名から `&`・`~`・`#` を落とす（設計 §6.3）。 */
    sq_strip_marks(stripped, sizeof(stripped), basenm);

    if (append_name && (mode >= 0)) {
        /*
         * **印を落としてから使う。** 実の遊びでは `of X` に回るのは風味の
         * 銘（`回避` など）で印を持たないが、`--name-check` で作った品では
         * `& 蛇形の指輪~` が回ってきて印がそのまま出た。塞いでおく。
         */
        sq_strip_marks(kindbuf, sizeof(kindbuf), k_name + k_ptr->name);
        kindnm = kindbuf;
    }
    if (known && (mode >= 0)) {
        if (o_ptr->name1) {
            title = a_info[o_ptr->name1].name;
        } else if (o_ptr->name2) {
            title = e_name + e_info[o_ptr->name2].name;
        }
    }

    /* --- 日本語の名前を組む。**風味 → 銘 → 基の名 → 数** --- */
    out[0] = '\0';
    sq_cat(out, sizeof(out), modstr);
    sq_cat_kind(out, sizeof(out), kindnm);
    if (!sq_is_suffix_title(title)) {
        sq_cat_title(out, sizeof(out), title);
    }
    sq_cat(out, sizeof(out), stripped);
    if (sq_is_suffix_title(title)) {
        sq_cat(out, sizeof(out), title);
    }

    /*
     * 数と助数詞（設計 §6.3）。**1 個のときは付けない**——英語も 1 個には
     * 数を出さない（`object1.c:800`）ので、揃えておくほうが読みやすい。
     */
    if (pref) {
        if (o_ptr->number <= 0) {
            sq_cat(out, sizeof(out), "\x81\x69\x82\xE0\x82\xA4\x96\xB3\x82\xA2\x81\x6A"); /* （もう無い） */
        } else if (o_ptr->number > 1) {
            sq_cat_num(out, sizeof(out), o_ptr->number);
            sq_cat(out, sizeof(out), sq_counter_for(o_ptr));
        }
    }

    /* --- 飾り（英語の全文から「名前だけ」を差し引いた残り）。 --- */
    head_len = strlen(name_en);
    if ((mode >= 0) && (strlen(full) > head_len) && (strncmp(full, name_en, head_len) == 0)) {
        sq_cat_decor(out, sizeof(out), full + head_len);
    }

    my_strcpy(buf, out, max);
    return 1;
}

/* ============================================================== 敵（フック #7） */

int sq_monster_desc_ja(char *desc, size_t max, const void *mon, int mode)
{
    const monster_type *m_ptr = (const monster_type *)mon;
    const monster_race *r_ptr;
    cptr name;
    int seen;
    int pron;
    char out[128];

    if (!sq_lang_enabled() || sq_ja_busy || !m_ptr || !desc || (max < 4)) {
        return 0;
    }

    r_ptr = &r_info[p_ptr->image ? m_ptr->image_r_idx : m_ptr->r_idx];
    name = (r_name + r_ptr->name);

    seen = ((mode & 0x80) || (!(mode & 0x40) && m_ptr->ml)) ? 1 : 0;
    pron = ((seen && (mode & 0x20)) || (!seen && (mode & 0x10))) ? 1 : 0;

    out[0] = '\0';

    /* --- 代名詞と、見えていない敵（`monster2.c:942-1035` と同じ枝分かれ）。 --- */
    if (!seen || pron) {
        int kind = 0x00;
        cptr res;

        if (r_ptr->flags1 & (RF1_FEMALE)) {
            kind = 0x20;
        } else if (r_ptr->flags1 & (RF1_MALE)) {
            kind = 0x10;
        }
        if (!pron) {
            kind = 0x00;
        }

        /*
         * 日本語には主格・目的格の別が無いので、英語の 8 通りは 4 通りに畳まる。
         * **見えていない敵は「何か」**（設計 §6.3）。
         */
        switch (kind + (mode & 0x07)) {
        case 0x02: res = "\x82\xBB\x82\xEA\x82\xCC"; break;             /* それの */
        case 0x03: res = "\x82\xBB\x82\xEA\x8E\xA9\x90\x67"; break;     /* それ自身 */
        case 0x04:
        case 0x05: res = "\x89\xBD\x82\xA9"; break;                     /* 何か */
        case 0x06: res = "\x89\xBD\x82\xA9\x82\xCC"; break;             /* 何かの */
        case 0x07: res = "\x82\xBB\x82\xEA\x8E\xA9\x90\x67"; break;     /* それ自身 */

        case 0x12: res = "\x94\xDE\x82\xCC"; break;                     /* 彼の */
        case 0x13: res = "\x94\xDE\x8E\xA9\x90\x67"; break;             /* 彼自身 */
        case 0x14:
        case 0x15: res = "\x92\x4E\x82\xA9"; break;                     /* 誰か */
        case 0x16: res = "\x92\x4E\x82\xA9\x82\xCC"; break;             /* 誰かの */
        case 0x17: res = "\x94\xDE\x8E\xA9\x90\x67"; break;             /* 彼自身 */
        case 0x10:
        case 0x11: res = "\x94\xDE"; break;                             /* 彼 */

        case 0x22: res = "\x94\xDE\x8F\x97\x82\xCC"; break;             /* 彼女の */
        case 0x23: res = "\x94\xDE\x8F\x97\x8E\xA9\x90\x67"; break;     /* 彼女自身 */
        case 0x24:
        case 0x25: res = "\x92\x4E\x82\xA9"; break;                     /* 誰か */
        case 0x26: res = "\x92\x4E\x82\xA9\x82\xCC"; break;             /* 誰かの */
        case 0x27: res = "\x94\xDE\x8F\x97\x8E\xA9\x90\x67"; break;     /* 彼女自身 */
        case 0x20:
        case 0x21: res = "\x94\xDE\x8F\x97"; break;                     /* 彼女 */

        default: res = "\x82\xBB\x82\xEA"; break;                       /* それ */
        }
        my_strcpy(desc, res, max);
        return 1;
    }

    /* --- 見えていて、再帰代名詞を求められた。 --- */
    if ((mode & 0x02) && (mode & 0x01)) {
        cptr res = "\x82\xBB\x82\xEA\x8E\xA9\x90\x67"; /* それ自身 */
        if (r_ptr->flags1 & (RF1_FEMALE)) {
            res = "\x94\xDE\x8F\x97\x8E\xA9\x90\x67"; /* 彼女自身 */
        } else if (r_ptr->flags1 & (RF1_MALE)) {
            res = "\x94\xDE\x8E\xA9\x90\x67"; /* 彼自身 */
        }
        my_strcpy(desc, res, max);
        return 1;
    }

    /*
     * --- 見えている敵。**冠詞は落とす**（設計 §6.3）。
     * `the kobold` も `a kobold` も日本語では同じ「コボルド」である。
     * 複数形も付けない。
     */
    sq_cat(out, sizeof(out), name);

    /* 所有格は「の」。 */
    if (mode & 0x02) {
        sq_cat(out, sizeof(out), "\x82\xCC"); /* の */
    }

    /* 画面の外に居る敵（`monster2.c:1097`）。 */
    if (!panel_contains(m_ptr->fy, m_ptr->fx)) {
        sq_cat(out, sizeof(out), "\x81\x69\x89\xE6\x96\xCA\x8A\x4F\x81\x6A"); /* （画面外） */
    }

    my_strcpy(desc, out, max);
    return 1;
}

/* ============================================================ 診断（`--name-check`） */

/*!
 * @brief 組み立ての結果を並べて出す。**遊ばずに見るための手段**（設計 §8 の P2 受け入れ）。
 *
 * @details `--selftest` と同じ立場のもので、遊びの筋には一切入らない。
 * 実際に出会うのを待つと、銘つきの品や風味つきの器が出るまで何百手も掛かる
 * （チュートリアルの階では奥の部屋まで届かなかった）。ここで型ごとに 1 つずつ作って
 * 見れば、**助数詞・銘の前置き／後置き・風味・アーティファクト・代名詞**を
 * 一度に確かめられる。P4（説明文）と P5（画面）でも同じ口を使う。
 *
 * `object_desc()` を呼ぶので、日本語層が起きていれば
 * `sq_object_desc_ja()` を通った結果が出る。
 */
void sq_name_check(void)
{
    object_type obj;
    char buf[192];
    int i;
    int shown;

    fprintf(stderr, "[silq:name] --- 品の名（3 個ずつ・判っている扱い）---\n");
    for (i = 1; i < z_info->k_max; i++) {
        if (!k_info[i].name) {
            continue;
        }
        object_wipe(&obj);
        object_prep(&obj, i);
        object_aware(&obj);
        obj.ident |= IDENT_KNOWN;
        obj.number = 3;
        object_desc(buf, sizeof(buf), &obj, TRUE, 1);
        fprintf(stderr, "[silq:name] k%-4d %s\n", i, buf);
    }

    fprintf(stderr, "[silq:name] --- 銘（`of X` と `(X)`）---\n");
    shown = 0;
    for (i = 1; i < z_info->e_max; i++) {
        int k;

        if (!e_info[i].name) {
            continue;
        }
        /* その銘が付きうる品を 1 つ探す（tval が合うもの）。 */
        for (k = 1; k < z_info->k_max; k++) {
            if (!k_info[k].name) {
                continue;
            }
            if (k_info[k].tval != e_info[i].tval[0]) {
                continue;
            }
            object_wipe(&obj);
            object_prep(&obj, k);
            object_aware(&obj);
            obj.ident |= IDENT_KNOWN;
            obj.number = 1;
            obj.name2 = (byte)i;
            object_desc(buf, sizeof(buf), &obj, TRUE, 0);
            fprintf(stderr, "[silq:name] e%-4d %s\n", i, buf);
            ++shown;
            break;
        }
    }
    fprintf(stderr, "[silq:name] （銘 %d 件）\n", shown);

    fprintf(stderr, "[silq:name] --- アーティファクト ---\n");
    for (i = 1; i < z_info->art_max; i++) {
        if (!a_info[i].name[0] || !a_info[i].tval) {
            continue;
        }
        {
            int k = lookup_kind(a_info[i].tval, a_info[i].sval);
            if (k <= 0) {
                continue;
            }
            object_wipe(&obj);
            object_prep(&obj, k);
            object_aware(&obj);
            obj.ident |= IDENT_KNOWN;
            obj.number = 1;
            obj.name1 = (byte)i;
            object_desc(buf, sizeof(buf), &obj, TRUE, 0);
            fprintf(stderr, "[silq:name] a%-4d %s\n", i, buf);
        }
    }

    fprintf(stderr, "[silq:name] --- 敵の名（`monster_desc` の 5 つの型）---\n");
    {
        monster_type mon;
        /*
         * **5 つの型**（設計 §8 の P2 受け入れ）。0x40 を立てないと
         * `m_ptr->ml` が真なので「見えない」型にならない（1 度取り違えた）。
         */
        static const int modes[] = { 0x00, 0x02, 0x44, 0x23, 0x08 };
        static const char *what[] = { "見える", "所有格", "見えない", "自分自身", "冠詞つき" };
        int r;

        for (r = 1; r < z_info->r_max; r++) {
            int m;

            if (!r_info[r].name) {
                continue;
            }
            if ((r != 11) && (r != 241) && (r != 251)) {
                continue; /* 狼・ゴスモグ・モルゴス の 3 体だけ見れば足りる */
            }
            for (m = 0; m < 5; m++) {
                WIPE(&mon, monster_type);
                mon.r_idx = (s16b)r;
                mon.image_r_idx = (s16b)r;
                mon.ml = TRUE;
                mon.fy = (byte)p_ptr->py;
                mon.fx = (byte)p_ptr->px;
                monster_desc(buf, sizeof(buf), &mon, modes[m]);
                fprintf(stderr, "[silq:name] r%-4d %-8s %s\n", r, what[m], buf);
            }
        }
    }
    fflush(stderr);
}
