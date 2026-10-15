/*!
 * @file an_wide.c
 * @brief `an_wide.h` の実体。
 *
 * @details 全角の判定は East Asian Width の W/F の範囲表（BMP だけ。Windows の
 * `wchar_t` は 16 ビットで、BMP 外の全角は無いものとする。設計 §3.1）。
 * 「A（曖昧）」は 1 桁に数える——Angband の字（`『』` 以外の記号）と、訳に使う
 * 記号（`「」『』、。・ー`）はどれも W/F の範囲にあり、曖昧の字を訳に使わない
 * ことで桁の揺れを避ける（`an_check_ja.py` が訳文の曖昧幅の字を数える）。
 */

#include "an_wide.h"

#include "an_lang_c.h"

#include "z-virt.h" /* mem_realloc(): 行の記録は原作の呼び手が mem_free() で捨てる */

#include <string.h>

int an_wcwidth(unsigned long wc)
{
    if (wc == (unsigned long)AN_WIDE_PAD) {
        return 0;
    }
    if (wc < 0x1100ul) {
        return 1;
    }
    if ((wc <= 0x115Ful)                              /* ハングル字母 */
        || ((wc >= 0x2E80ul) && (wc <= 0x303Eul))     /* CJK 部首・記号と句読点 */
        || ((wc >= 0x3041ul) && (wc <= 0x33FFul))     /* ひらがな・かたかな・CJK 互換 */
        || ((wc >= 0x3400ul) && (wc <= 0x4DBFul))     /* CJK 統合漢字 拡張 A */
        || ((wc >= 0x4E00ul) && (wc <= 0x9FFFul))     /* CJK 統合漢字 */
        || ((wc >= 0xA000ul) && (wc <= 0xA4CFul))     /* イ文字 */
        || ((wc >= 0xAC00ul) && (wc <= 0xD7A3ul))     /* ハングル音節 */
        || ((wc >= 0xF900ul) && (wc <= 0xFAFFul))     /* CJK 互換漢字 */
        || ((wc >= 0xFE30ul) && (wc <= 0xFE4Ful))     /* CJK 互換形 */
        || ((wc >= 0xFF00ul) && (wc <= 0xFF60ul))     /* 全角英数・記号 */
        || ((wc >= 0xFFE0ul) && (wc <= 0xFFE6ul))) {  /* 全角の通貨記号ほか */
        return 2;
    }
    return 1;
}

void an_wide_expand(wchar_t *s, int cap)
{
    int len;
    int wide;
    int i;
    int out;

    if (!s || (cap <= 1) || !an_lang_enabled()) {
        return;
    }
    len = 0;
    wide = 0;
    while ((len < cap) && s[len]) {
        if ((s[len] != AN_WIDE_PAD) && (an_wcwidth((unsigned long)s[len]) == 2)) {
            wide++;
        }
        len++;
    }
    if (wide == 0) {
        return;
    }
    /* 後ろから詰め直す（その場で）。容量を超える分は切る。 */
    out = len + wide;
    if (out > cap - 1) {
        /* 収まる所まで数え直す。全角の片割れを残さない。 */
        int cells = 0;
        int keep = 0;
        for (i = 0; i < len; i++) {
            const int w = ((s[i] != AN_WIDE_PAD) && (an_wcwidth((unsigned long)s[i]) == 2)) ? 2 : 1;
            if (cells + w > cap - 1) {
                break;
            }
            cells += w;
            keep = i + 1;
        }
        len = keep;
        out = cells;
    }
    s[out] = 0;
    for (i = len - 1; i >= 0; i--) {
        const wchar_t c = s[i];
        if ((c != AN_WIDE_PAD) && (an_wcwidth((unsigned long)c) == 2)) {
            s[--out] = AN_WIDE_PAD;
        }
        s[--out] = c;
    }
}

int an_wide_clip(const wchar_t *s, int n)
{
    if (!s || (n <= 0) || !an_lang_enabled()) {
        return n;
    }
    if (s[n] == AN_WIDE_PAD) {
        return n - 1; /* 切れ目が全角と埋め字の間 → 全角ごと落とす。 */
    }
    return n;
}

/*! UTF-8 の 1 符号位置を読む。戻り値は進んだバイト数（0 なら終わり）。 */
static size_t an_utf8_next(const char *p, unsigned long *cp)
{
    const unsigned char *s = (const unsigned char *)p;
    unsigned char c0 = s[0];
    int extra;
    unsigned long v;
    int i;

    if (c0 == 0) {
        *cp = 0;
        return 0;
    }
    if (c0 < 0x80) {
        *cp = c0;
        return 1;
    }
    if ((c0 & 0xE0) == 0xC0) {
        extra = 1;
        v = c0 & 0x1Fu;
    } else if ((c0 & 0xF0) == 0xE0) {
        extra = 2;
        v = c0 & 0x0Fu;
    } else if ((c0 & 0xF8) == 0xF0) {
        extra = 3;
        v = c0 & 0x07u;
    } else {
        *cp = 0xFFFDul;
        return 1;
    }
    for (i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) {
            *cp = 0xFFFDul;
            return 1;
        }
        v = (v << 6) | (unsigned long)(s[i] & 0x3Fu);
    }
    *cp = v;
    return (size_t)(extra + 1);
}

size_t an_utf8_cols(const char *s)
{
    size_t cols = 0;
    unsigned long cp;
    size_t adv;

    if (!s) {
        return 0;
    }
    while ((adv = an_utf8_next(s, &cp)) != 0) {
        cols += (size_t)an_wcwidth(cp);
        s += adv;
    }
    return cols;
}

void an_utf8_clip_cols(char *s, size_t cols)
{
    size_t used = 0;
    char *p = s;
    unsigned long cp;
    size_t adv;

    if (!s) {
        return;
    }
    while ((adv = an_utf8_next(p, &cp)) != 0) {
        const size_t w = (size_t)an_wcwidth(cp);
        if (used + w > cols) {
            break;
        }
        used += w;
        p += adv;
    }
    *p = '\0';
}

int an_utf8_strlen_hook(const char *s, size_t *out)
{
    if (!an_lang_enabled()) {
        return 0;
    }
    *out = an_utf8_cols(s);
    return 1;
}

int an_utf8_clipto_hook(char *s, size_t n)
{
    if (!an_lang_enabled()) {
        return 0;
    }
    an_utf8_clip_cols(s, n);
    return 1;
}

int an_kinsoku_head(unsigned long wc)
{
    switch (wc) {
    case 0x3001ul: /* 、 */
    case 0x3002ul: /* 。 */
    case 0x300Dul: /* 」 */
    case 0x300Ful: /* 』 */
    case 0xFF09ul: /* ） */
    case 0xFF01ul: /* ！ */
    case 0xFF1Ful: /* ？ */
    case 0x30FCul: /* ー */
    case 0x30FBul: /* ・ */
    case 0x3063ul: /* っ */
    case 0x30C3ul: /* ッ */
    case 0x3083ul: /* ゃ */
    case 0x3085ul: /* ゅ */
    case 0x3087ul: /* ょ */
    case 0x30E3ul: /* ャ */
    case 0x30E5ul: /* ュ */
    case 0x30E7ul: /* ョ */
    case 0x3041ul: /* ぁ */
    case 0x3043ul: /* ぃ */
    case 0x3045ul: /* ぅ */
    case 0x3047ul: /* ぇ */
    case 0x3049ul: /* ぉ */
    case 0x30A1ul: /* ァ */
    case 0x30A3ul: /* ィ */
    case 0x30A5ul: /* ゥ */
    case 0x30A7ul: /* ェ */
    case 0x30A9ul: /* ォ */
        return 1;
    default:
        return 0;
    }
}

size_t an_wide_room(size_t n)
{
    return an_lang_enabled() ? (2 * n) : n;
}

size_t an_wide_expand_len(wchar_t *s, size_t len, size_t cap)
{
    size_t wide = 0;
    size_t i;
    size_t out;

    if (!s || !an_lang_enabled()) {
        return len;
    }
    for (i = 0; i < len; i++) {
        if ((s[i] != AN_WIDE_PAD) && (an_wcwidth((unsigned long)s[i]) == 2)) {
            wide++;
        }
    }
    if ((wide == 0) || (len + wide > cap)) {
        return len; /* 呼び手（`an_wide_room()`）が場所を取ってあるので、ここへは来ない。 */
    }
    out = len + wide;
    if (out < cap) {
        s[out] = 0;
    }
    for (i = len; i-- > 0;) {
        const wchar_t c = s[i];
        if ((c != AN_WIDE_PAD) && (an_wcwidth((unsigned long)c) == 2)) {
            s[--out] = AN_WIDE_PAD;
        }
        s[--out] = c;
    }
    return len + wide;
}

/*! 行の記録を 1 つ足す（`z-textblock.c` の `new_line()` と同じ伸ばし方）。 */
static void an_tb_new_line(size_t **starts, size_t **lengths, size_t *alloc, size_t *count, size_t start,
    size_t len)
{
    if (*count == *alloc) {
        *alloc += 24;
        *starts = mem_realloc(*starts, *alloc * sizeof **starts);
        *lengths = mem_realloc(*lengths, *alloc * sizeof **lengths);
    }
    (*starts)[*count] = start;
    (*lengths)[*count] = len;
    (*count)++;
}

int an_textblock_lines(const wchar_t *text, size_t n, size_t **line_starts, size_t **line_lengths, size_t width,
    size_t *out_lines)
{
    size_t alloc = 0;
    size_t count = 0;
    size_t start = 0;

    if (!an_lang_enabled() || !text || !line_starts || !line_lengths || !out_lines || (width == 0)) {
        return 0;
    }
    if (n == 0) {
        *out_lines = 0;
        return 1;
    }
    while (start <= n) {
        size_t i = start;
        size_t cut = (size_t)-1;   /* 折る位置（行の長さ） */
        size_t next = (size_t)-1;  /* 次の行の頭 */
        size_t last_space = (size_t)-1;
        size_t last_wide = (size_t)-1; /* 全角の頭（ここの直前で折れる） */

        while ((i < n) && (text[i] != L'\n') && (i - start < width)) {
            if (text[i] == L' ') {
                last_space = i;
            } else if ((text[i] != AN_WIDE_PAD) && (an_wcwidth((unsigned long)text[i]) == 2) && (i > start)) {
                last_wide = i;
            }
            i++;
        }
        if ((i >= n) || (text[i] == L'\n')) {
            cut = i - start;
            next = i + 1;
        } else {
            /* 幅に達した。i は行に入らなかった最初の字。 */
            const int next_is_pad = (text[i] == AN_WIDE_PAD);
            size_t wide_break = last_wide;
            if (!next_is_pad && (text[i] != L' ') && (an_wcwidth((unsigned long)text[i]) == 2)) {
                wide_break = i; /* 次の字が全角なら、ちょうどここで折れる。 */
            }
            /* 禁則: 行頭に来てはいけない字の前では折らない（1 字ぶん前へ戻す）。 */
            while ((wide_break != (size_t)-1) && (wide_break > start) && an_kinsoku_head((unsigned long)text[wide_break])) {
                size_t k = wide_break;
                size_t prev = (size_t)-1;
                while (k > start) {
                    k--;
                    if ((text[k] != AN_WIDE_PAD) && (an_wcwidth((unsigned long)text[k]) == 2)) {
                        prev = k;
                        break;
                    }
                }
                wide_break = (prev != (size_t)-1 && prev > start) ? prev : (size_t)-1;
            }
            if ((last_space != (size_t)-1) && (last_space > start)
                && ((wide_break == (size_t)-1) || (last_space + 1 >= wide_break))) {
                cut = last_space - start;
                next = last_space + 1;
            } else if ((wide_break != (size_t)-1) && (wide_break > start)) {
                cut = wide_break - start;
                next = wide_break;
            } else {
                /* 折れる所が無い（英語の長い 1 語）。原作と同じく幅で切る。埋め字を割らない。 */
                size_t w = width;
                if ((start + w < n) && (text[start + w] == AN_WIDE_PAD) && (w > 1)) {
                    w--;
                }
                cut = w;
                next = start + w;
            }
        }
        an_tb_new_line(line_starts, line_lengths, &alloc, &count, start, cut);
        if (next > n) {
            break;
        }
        start = next;
        if (start == n) {
            /* 末尾が改行で終わった: 原作と同じく空の最終行を足し、後で詰める。 */
            an_tb_new_line(line_starts, line_lengths, &alloc, &count, start, 0);
            break;
        }
    }
    if ((count > 0) && ((*line_lengths)[count - 1] == 0)) {
        count--;
    }
    *out_lines = count;
    return 1;
}
