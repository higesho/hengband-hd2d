/*!
 * @file sq_lang_c.c
 * @brief `sq_lang_c.h` のうち**素のバイト算**（切る・折る・通す）。純 C。
 *
 * カタログを引く 3 つ（`sq_lang_enabled` /
 * `sq_tr` / `sq_tr_fmt`）は C++ 側（`sq_lang.cpp`）にある。
 *
 * ## ここに Sil-Q のヘッダを include しない
 * `silq/src` と同じ TU 群に混ぜて組むが、**依存の向きは一方通行**である
 * （`silq/src` → `sq_lang_c.h` → ここ）。逆を作ると上流追随が崩れる。
 *
 * ## 文字コード
 * 入ってくる文字列はすべて **CP932（SJIS）**（設計 §3.1）。
 * `windows.h` も要らない——2 バイト文字の範囲は数字で書ける。
 */

#include "sq_lang_c.h"

/* ====================================================================== 禁則の表 */

/*!
 * @name 禁則の表（CP932 の 2 バイト符号をそのまま並べる）
 * @details 並びは「よく出る順」ではなく**符号順に近い並び**にしてある
 * （足すときに重複を見つけやすい）。半角の記号は入れない——半角は英語の
 * 折り方（空白で折る）に任せる。
 * @{
 */

//! 行頭に来てはいけない字（句読点・閉じ括弧・長音・小書き仮名）。
static const unsigned short sq_kinsoku_head[] = {
    0x8141, 0x8142, 0x8143, 0x8144, 0x8145, 0x8146, 0x8147, 0x8148,
    0x8149, 0x814A, 0x814B, 0x8152, 0x8153, 0x8154, 0x8155, 0x8158,
    0x815B, 0x8168, 0x816A, 0x816C, 0x816E, 0x8170, 0x8172, 0x8174,
    0x8176, 0x8178,
    0x829F, 0x82A1, 0x82A3, 0x82A5, 0x82A7, 0x82C1, 0x82E1, 0x82E3,
    0x82E5, 0x82EC,
    0x8340, 0x8342, 0x8344, 0x8346, 0x8348, 0x8362, 0x8383, 0x8385,
    0x8387, 0x838E,
    0
};

//! 行末に来てはいけない字（開き括弧）。
static const unsigned short sq_kinsoku_tail[] = {
    0x8167, 0x8169, 0x816B, 0x816D, 0x816F, 0x8171, 0x8173, 0x8175,
    0x8177,
    0
};

/*! @} */

static int sq_in_table(const unsigned short *table, unsigned int code)
{
    int i;

    for (i = 0; table[i] != 0; i++) {
        if ((unsigned int)table[i] == code) {
            return 1;
        }
    }
    return 0;
}

/*! @brief `s` の先頭の字の CP932 符号（2 バイト文字なら 2 バイトぶん）。1 バイトなら 0 を返す。 */
static unsigned int sq_wide_code(const char *s)
{
    unsigned char lead;
    unsigned char trail;

    if ((s == 0) || (s[0] == '\0')) {
        return 0;
    }
    lead = (unsigned char)s[0];
    if (!sq_is_sjis_lead((int)lead)) {
        return 0;
    }
    trail = (unsigned char)s[1];
    if (trail == 0) {
        return 0; /* 尻尾が欠けている。1 バイト扱い */
    }
    return ((unsigned int)lead << 8) | (unsigned int)trail;
}

/*! @brief `s` の先頭の字のバイト数（1 か 2）。 */
static int sq_char_bytes(const char *s)
{
    return (sq_wide_code(s) != 0) ? 2 : 1;
}

/* ============================================================ セーブの符号の印（A3） */

/*!
 * @brief 読み込んだセーブの印。まだ何も読んでいなければ `SQ_SAVE_ENC_NONE`。
 * @details 新しい人物を作ったときは読み込みが走らないので `NONE` のままで、
 * `sq_cp932_text()` は `sq_lang_enabled()` と同じ答えになる。
 */
static unsigned long sq_loaded_enc = SQ_SAVE_ENC_NONE;

unsigned long sq_save_enc_mark(void)
{
    return sq_cp932_text() ? (unsigned long)SQ_SAVE_ENC_CP932 : (unsigned long)SQ_SAVE_ENC_ASCII;
}

void sq_save_enc_note(unsigned long mark)
{
    sq_loaded_enc = (mark == (unsigned long)SQ_SAVE_ENC_CP932) ? (unsigned long)SQ_SAVE_ENC_CP932
                                                               : (unsigned long)SQ_SAVE_ENC_NONE;
}

int sq_cp932_text(void)
{
    if (sq_lang_enabled()) {
        return 1;
    }
    return (sq_loaded_enc == (unsigned long)SQ_SAVE_ENC_CP932) ? 1 : 0;
}

/* ================================================================== 公開する関数 */

int sq_is_sjis_lead(int c)
{
    const unsigned int u = (unsigned int)(c & 0xFF);

    return (((u >= 0x81u) && (u <= 0x9Fu)) || ((u >= 0xE0u) && (u <= 0xFCu))) ? 1 : 0;
}

int sq_is_sjis_trail(int c)
{
    const unsigned int u = (unsigned int)(c & 0xFF);

    return (((u >= 0x40u) && (u <= 0x7Eu)) || ((u >= 0x80u) && (u <= 0xFCu))) ? 1 : 0;
}

int sq_last_char_bytes(const char *buf, int len)
{
    int at = 0;
    int last = 1;

    if ((buf == 0) || (len <= 0)) {
        return 1;
    }
    /* 頭から辿る（上の註記）。`at` が `len` へ届いたところの 1 文字が答え。 */
    while (at < len) {
        const int bytes = sq_char_bytes(buf + at);

        if ((at + bytes) > len) {
            break; /* 尻尾が欠けている。1 バイト戻す */
        }
        last = bytes;
        at += bytes;
    }
    return last;
}

int sq_is_text_byte(int c)
{
    const unsigned int u = (unsigned int)(c & 0xFF);

    /*
     * `isprint()` と同じ答え（0x20〜0x7E）に、記憶の字が CP932 でありうるときだけ
     * **0x80 以上**を足す（`sq_cp932_text()`。日本語で立てたとき＋
     * 日本語のセーブを英語で開いたとき）。`setlocale()` に頼らないのは、コアの他の場所
     * （`isalpha` など）の答えまで変えてしまわないためである
     * （検討 §7-1 の実測に代えてこちらを採った。設計 §11 の「私の推定」）。
     */
    if ((u >= 0x20u) && (u < 0x7Fu)) {
        return 1;
    }
    if (!sq_cp932_text()) {
        return 0;
    }
    return (u >= 0x80u) ? 1 : 0;
}

int sq_clip(const char *s, int n)
{
    int i;

    if (!sq_cp932_text() || (s == 0) || (n <= 0)) {
        return n;
    }

    i = 0;
    while (i < n) {
        const int len = sq_char_bytes(s + i);
        if (s[i] == '\0') {
            return i;
        }
        if ((i + len) > n) {
            return i; /* この字は入りきらない。手前で止める */
        }
        i += len;
    }
    return i;
}

int sq_break(const char *s, int w)
{
    int p;

    if (!sq_cp932_text() || (s == 0) || (w <= 0)) {
        return w;
    }

    /* まず 2 バイト境界まで下げる（`sq_clip` と同じ歩き方）。 */
    p = sq_clip(s, w);
    if (p <= 0) {
        return w;
    }

    /*
     * 行頭禁則。折った先の頭に「。」が来るなら 1 字ぶん手前で折る。
     * **連なっているぶんだけ下がる**（「）。」で 2 字下がることがある）。
     * 下がりきって行が空になるくらいなら禁則を諦める（元の `p` を返す）。
     */
    {
        int q = p;
        int guard = 8; /* 際限なく下がらない。8 字も下がるなら諦めるべき場面 */

        while ((guard-- > 0) && (q > 0) && sq_in_table(sq_kinsoku_head, sq_wide_code(s + q))) {
            const int back = ((q >= 2) && (sq_char_bytes(s + q - 2) == 2)) ? 2 : 1;
            if ((q - back) <= 0) {
                return p; /* 行が空になる。諦める */
            }
            q -= back;
        }
        if (q > 0) {
            return q;
        }
    }

    return p;
}

int sq_wrap_here(const char *s, int x, int wrap, int wid)
{
    unsigned int code;
    int len;
    int hang;

    if (!sq_cp932_text() || (s == 0) || (s[0] == '\0')) {
        return 0;
    }

    code = sq_wide_code(s);
    if (code == 0) {
        return 0; /* 1 バイト文字。**英語の折り方に一切触らない** */
    }
    len = 2;

    if (sq_in_table(sq_kinsoku_head, code)) {
        /*
         * 行頭禁則。**全角 1 字ぶん**はみ出させて行末にぶら下げる。
         * 最後の桁が `hang` に収まればよい——`wrap` そのもの（ふつうの字より
         * 2 桁先）まで許し、端末の幅で頭打ちにする。
         */
        hang = (wrap < (wid - 1)) ? wrap : (wid - 1);
        return ((x + len - 1) > hang) ? 1 : 0;
    }
    if (sq_in_table(sq_kinsoku_tail, code)) {
        /* 行末禁則。次の 1 字（2 桁）が同じ行に入らないなら先に折る。 */
        return ((x + len + 2) > (wrap - 1)) ? 1 : 0;
    }
    return ((x + len) > (wrap - 1)) ? 1 : 0;
}
