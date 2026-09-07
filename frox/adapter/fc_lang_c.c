/*!
 * @file fc_lang_c.c
 * @brief `fc_lang_c.h` のうち**素のバイト算**（切る・通す・単語を切る）。純 C。
 *
 * カタログを引く 4 つ（`fc_lang_enabled` /
 * `fc_tr` / `fc_tr_fmt` / `fc_tr_arg`）は C++ 側（`fc_lang.cpp`）にある。
 *
 * ## ここに Frox のヘッダを include しない
 * `frox/src` と同じ TU 群に混ぜて組むが、**依存の向きは一方通行**である
 * （`frox/src` → `fc_lang_c.h` → ここ）。逆を作ると上流追随が崩れる。
 *
 * ## 文字コード
 * 入ってくる文字列はすべて **CP932（SJIS）**（設計 §3.1）。
 * `windows.h` も要らない——2 バイト文字の範囲は数字で書ける。
 */

#include "fc_lang_c.h"

/* ====================================================================== 禁則の表 */

/*!
 * @name 禁則の表（CP932 の 2 バイト符号をそのまま並べる）
 * @details `silq/adapter/sq_lang_c.c` と同じ並び。半角の記号は入れない
 * ——半角は英語の折り方（空白で折る）に任せる。
 * @{
 */

//! 行頭に来てはいけない字（句読点・閉じ括弧・長音・小書き仮名）。
static const unsigned short fc_kinsoku_head[] = {
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
static const unsigned short fc_kinsoku_tail[] = {
    0x8167, 0x8169, 0x816B, 0x816D, 0x816F, 0x8171, 0x8173, 0x8175,
    0x8177,
    0
};

/*! @} */

static int fc_in_table(const unsigned short *table, unsigned int code)
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
static unsigned int fc_wide_code(const char *s)
{
    unsigned char lead;
    unsigned char trail;

    if ((s == 0) || (s[0] == '\0')) {
        return 0;
    }
    lead = (unsigned char)s[0];
    if (!fc_is_sjis_lead((int)lead)) {
        return 0;
    }
    trail = (unsigned char)s[1];
    if (!fc_is_sjis_trail((int)trail)) {
        return 0; /* 尻尾が欠けている／組になっていない。1 バイト扱い */
    }
    return ((unsigned int)lead << 8) | (unsigned int)trail;
}

/*!
 * @brief `s` の先頭の字のバイト数（1 か 2）。宣言は `fc_lang_c.h`。
 * @details **追補 A1 で公開した**（`util.c` の自由入力が桁送りに使う）。
 * それまではこの TU の中だけで使っていた。
 */
int fc_char_bytes(const char *s)
{
    return (fc_wide_code(s) != 0) ? 2 : 1;
}

/* ================================================================== 公開する関数 */

/* ================================ セーブの符号の印（追補 A3。フック #29 #30） */

/*! @brief 印なし。上流・M0 のセーブ（`sf_system` は 0 で書かれる）。 */
#define FC_SAVE_ENC_NONE  0x00000000UL
/*! @brief 純 ASCII で書いた。 */
#define FC_SAVE_ENC_ASCII 0x46430001UL
/*! @brief 人の字の欄に CP932 が載っている。 */
#define FC_SAVE_ENC_CP932 0x46430002UL

/*!
 * @brief いま開いているセーブの印。**読んだときだけ立つ**（新規は 0 のまま）。
 * @details 起こし直し（追補 A2）は別プロセスなので、ここは毎回 0 から始まる。
 */
static unsigned long fc_save_enc_loaded = FC_SAVE_ENC_NONE;

unsigned long fc_save_enc_mark(void)
{
    if (fc_lang_enabled() || (fc_save_enc_loaded == FC_SAVE_ENC_CP932)) {
        /*
         * **下げない。** 日本語で作った人物を英語で開いて保存し直しても、
         * 記憶の中の字は CP932 のバイトのままである（ヘッダの註記）。
         */
        return FC_SAVE_ENC_CP932;
    }
    return FC_SAVE_ENC_ASCII;
}

void fc_save_enc_note(unsigned long mark)
{
    fc_save_enc_loaded = mark;
}

int fc_cp932_text(void)
{
    /*
     * 日本語で立てた **または** 読んだセーブに CP932 の印がある（追補 A3）。
     * 呼び手は全部この 1 関数を見ている。
     */
    if (fc_lang_enabled()) {
        return 1;
    }
    return (fc_save_enc_loaded == FC_SAVE_ENC_CP932) ? 1 : 0;
}

int fc_is_sjis_lead(int c)
{
    const unsigned int u = (unsigned int)(c & 0xFF);

    return (((u >= 0x81u) && (u <= 0x9Fu)) || ((u >= 0xE0u) && (u <= 0xFCu))) ? 1 : 0;
}

int fc_is_sjis_trail(int c)
{
    const unsigned int u = (unsigned int)(c & 0xFF);

    return (((u >= 0x40u) && (u <= 0x7Eu)) || ((u >= 0x80u) && (u <= 0xFCu))) ? 1 : 0;
}

int fc_last_char_bytes(const char *buf, int len)
{
    int at = 0;
    int last = 1;

    if ((buf == 0) || (len <= 0)) {
        return 1;
    }
    /* 頭から辿る（ヘッダの註記）。`at` が `len` へ届いたところの 1 文字が答え。 */
    while (at < len) {
        const int bytes = fc_char_bytes(buf + at);

        if ((at + bytes) > len) {
            break; /* 尻尾が欠けている。1 バイト戻す */
        }
        last = bytes;
        at += bytes;
    }
    return last;
}

int fc_is_text_byte(int c)
{
    const unsigned int u = (unsigned int)(c & 0xFF);

    /*
     * `isprint()` と同じ答え（0x20〜0x7E）に、コアの字が CP932 でありうるときだけ
     * **0x80 以上**を足す。`setlocale()` に頼らないのは、コアの他の場所
     * （`isalpha` など）の答えまで変えてしまわないためである（設計 §4 の註）。
     */
    if ((u >= 0x20u) && (u < 0x7Fu)) {
        return 1;
    }
    if (!fc_cp932_text()) {
        return 0;
    }
    return (u >= 0x80u) ? 1 : 0;
}

int fc_clip(const char *s, int n)
{
    int i;

    if (!fc_cp932_text() || (s == 0) || (n <= 0)) {
        return n;
    }

    i = 0;
    while (i < n) {
        int len;

        if (s[i] == '\0') {
            return i;
        }
        len = fc_char_bytes(s + i);
        if ((i + len) > n) {
            return i; /* この字は入りきらない。手前で止める */
        }
        i += len;
    }
    return i;
}

int fc_ref_index(const char *name)
{
    int value = 0;
    const char *p;

    if (!fc_lang_enabled() || (name == 0) || (name[0] == '\0')) {
        return 0;
    }
    for (p = name; *p != '\0'; p++) {
        if ((*p < '0') || (*p > '9')) {
            return 0; /* 数字でない字が 1 つでもあれば名前の参照。触らない */
        }
        value = (value * 10) + (*p - '0');
        if (value > 100000) {
            return 0; /* 桁あふれの守り。実体の番号は 4 桁まで */
        }
    }
    return value;
}

int fc_doc_word_bytes(const char *pos)
{
    int size;
    int guard;

    if (!fc_cp932_text() || (pos == 0)) {
        return 0;
    }
    if (fc_wide_code(pos) == 0) {
        return 0; /* 1 バイト文字。**英語の切り方に一切触らない** */
    }

    size = 2;

    /* 行末禁則（`（「『`）。開いたら次の 1 字を離さない。 */
    if (fc_in_table(fc_kinsoku_tail, fc_wide_code(pos))) {
        if (fc_wide_code(pos + size) != 0) {
            size += 2;
        }
    }

    /*
     * 行頭禁則（`。、）」`）。連なるぶんだけ後ろへ足す。
     * **際限なく足さない**——4 字も続くなら、そこで折れても読める。
     */
    guard = 4;
    while ((guard-- > 0) && fc_in_table(fc_kinsoku_head, fc_wide_code(pos + size))) {
        size += 2;
    }
    return size;
}

int fc_roff_kinsoku(const char *s)
{
    if (!fc_cp932_text() || (s == 0)) {
        return 0;
    }
    return fc_in_table(fc_kinsoku_head, fc_wide_code(s)) ? 1 : 0;
}

/* ============================================== 自由入力の旗（追補 A1。#9） */

/*!
 * @brief 自由入力の待ちの深さ。**数えるのは入れ子になるからである**
 * ——`get_string()` → `askfor()` → `_askfor_aux()` と重なって呼ばれても、
 * いちばん外が閉じるまで旗を落とさない。
 */
static int fc_text_input_depth = 0;

void fc_text_input_set(int active)
{
    if (active) {
        ++fc_text_input_depth;
    } else if (fc_text_input_depth > 0) {
        --fc_text_input_depth;
    }
}

/*! @brief ASCII だけの欄（名前）の深さ。理由は `fc_lang_c.h` の註記。 */
static int fc_text_input_ascii_depth = 0;

void fc_text_input_ascii(int on)
{
    if (on) {
        ++fc_text_input_ascii_depth;
    } else if (fc_text_input_ascii_depth > 0) {
        --fc_text_input_ascii_depth;
    }
}

int fc_text_input_active(void)
{
    if (!fc_lang_enabled()) {
        /*
         * **英語では 1 ビットも変えない**（設計 §1 制約 1）。旗はフレームへ
         * 出る値なので、立てると M0 とハッシュが合わなくなる。
         */
        return 0;
    }
    if (fc_text_input_ascii_depth > 0) {
        return 0; /* 名前の欄。IME を出させない（`fc_lang_c.h` の註記） */
    }
    return (fc_text_input_depth > 0) ? 1 : 0;
}
