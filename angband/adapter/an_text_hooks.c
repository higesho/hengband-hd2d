/*!
 * @file an_text_hooks.c
 * @brief `text_mbcs_hook` ほか 4 つの UTF-8 変換フックを差す（設計 §6.1）。
 *
 * `z-util.h:43-46` は 4 つのフックが NULL なら CRT のロケール依存の
 * `mbstowcs()` / `wctomb()` / `iswprint()` に落ちる（`z-util.c:831-867`）。
 * こちらは C ロケールのままなので、UTF-8 の複数バイト文字が 1 バイト 1 字として
 * 誤変換される（`Mîm` が `MÃ®m` になる。S1 の見直しで確認）。
 *
 * Windows 側の中身は `main-win.c:2359-2443`（`Term_mbstowcs_win` ほか）の写しで、
 * `MultiByteToWideChar` / `WideCharToMultiByte` を CP_UTF8 で呼ぶだけで済む。
 * Linux 側（`_WIN32` が無いとき）は同じ 4 フックを、ロケールに依存しない
 * 自前の UTF-8 変換で実装する（`wchar_t` は Linux では 4 バイトの符号位置その
 * ものなので、サロゲート対の扱いが要らない分 Windows 側より単純になる）。
 * 原作を編集せず、この TU から `text_*_hook` へ関数ポインタとして差す
 * （設計 §4 の脚注「`game-input.h` のフックは関数ポインタなので原作を変えずに
 * 差し替えられる」と同じ考え方）。
 */

#include "angband.h"

#include "init.h" /* ANGBAND_DIR_*（日本語の名前のフォルダのパスを直す。`an_fix_acp_paths()`） */
#include "an_lang_c.h" /* フック #8 */
#include "an_wide.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <locale.h>
#include <stdint.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32

/*!
 * @brief 変換の旗。英語では原作どおり壊れた UTF-8 を失敗にする（`MB_ERR_INVALID_CHARS`）。
 * @details 日本語では、原作が字を 1 バイトずつ継ぐ所や
 * バイト数で切る所で多バイト字が割れうる。失敗を返すと `textblock` の `assert` で落ちるので、
 * 日本語のときだけ割れた所を U+FFFD に置き換えて通す（落とさない）。
 */
static DWORD an_mb_flags(void)
{
    return an_lang_enabled() ? 0 : MB_ERR_INVALID_CHARS;
}

/*!
 * @brief 文に埋まった保存先のパス（ANSI＝Windows の既定のコードページ。日本語環境では CP932）を UTF-8 に直す。
 * @details 原作の `get_file_text()`（`ui-input.c`）は `prt(format("Saving as %s.", path), 0, 0)` でパスを
 * 画面に出す。このパスは `--data=` で渡したフォルダから作るので、Windows では ANSI のバイト列である。
 * 日本語の名前のフォルダだと、残りの文（UTF-8）と混ざった並びが UTF-8 として読めず、字が
 * U+FFFD（またはゴミ）に化けていた（2026-10-07）。
 *
 * **字ごとに ANSI か UTF-8 かを当てるのではなく**、`ANGBAND_DIR_*`（ANSI で持っているフォルダ）の
 * 綴りそのものが文に含まれているときだけ、その部分を UTF-8 に置き換える。字ごとに当てると、バイト数で
 * 割れた UTF-8 の字（日本語の訳が途中で切れたとき）を別の漢字と読み違える。フォルダが ASCII だけなら
 * 何もしない（既存の経路と同じ）。
 */
#define AN_ACP_DIR_MAX 16
static char *an_acp_dirs[AN_ACP_DIR_MAX];      /*!< ANSI の綴り（非 ASCII を含むフォルダだけ） */
static char *an_acp_dirs_utf8[AN_ACP_DIR_MAX]; /*!< 同じ綴りの UTF-8 */
static int an_acp_dir_count = 0;
static uintptr_t an_acp_dir_signature = (uintptr_t)-1;

static char *an_acp_to_utf8(const char *s)
{
    int wide_len;
    int utf8_len;
    wchar_t *wide;
    char *out;

    wide_len = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (wide_len <= 0) {
        return NULL;
    }
    wide = (wchar_t *)malloc((size_t)wide_len * sizeof(wchar_t));
    if (!wide) {
        return NULL;
    }
    MultiByteToWideChar(CP_ACP, 0, s, -1, wide, wide_len);
    utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    out = (utf8_len > 0) ? (char *)malloc((size_t)utf8_len) : NULL;
    if (out) {
        WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, utf8_len, NULL, NULL);
    }
    free(wide);
    return out;
}

static int an_has_high_byte(const char *s)
{
    for (; *s; ++s) {
        if ((unsigned char)*s >= 0x80) {
            return 1;
        }
    }
    return 0;
}

/*! `ANGBAND_DIR_*` が変わっていたら（起動時・保存先の差し替え `AN_REDIRECT`）集め直す。 */
static void an_acp_dirs_refresh(void)
{
    char *const dirs[] = { ANGBAND_DIR_GAMEDATA, ANGBAND_DIR_CUSTOMIZE, ANGBAND_DIR_HELP, ANGBAND_DIR_SCREENS,
        ANGBAND_DIR_FONTS, ANGBAND_DIR_TILES, ANGBAND_DIR_SOUNDS, ANGBAND_DIR_ICONS, ANGBAND_DIR_USER,
        ANGBAND_DIR_SAVE, ANGBAND_DIR_PANIC, ANGBAND_DIR_SCORES, ANGBAND_DIR_ARCHIVE };
    uintptr_t signature = 0;
    size_t i;

    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        signature = (signature * 31u) + (uintptr_t)dirs[i];
    }
    if (signature == an_acp_dir_signature) {
        return;
    }
    an_acp_dir_signature = signature;
    for (i = 0; i < (size_t)an_acp_dir_count; i++) {
        free(an_acp_dirs[i]);
        free(an_acp_dirs_utf8[i]);
    }
    an_acp_dir_count = 0;
    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        char *copy;
        char *utf8;

        if (!dirs[i] || !an_has_high_byte(dirs[i]) || (an_acp_dir_count >= AN_ACP_DIR_MAX)) {
            continue;
        }
        copy = _strdup(dirs[i]);
        utf8 = an_acp_to_utf8(dirs[i]);
        if (!copy || !utf8) {
            free(copy);
            free(utf8);
            continue;
        }
        an_acp_dirs[an_acp_dir_count] = copy;
        an_acp_dirs_utf8[an_acp_dir_count] = utf8;
        an_acp_dir_count++;
    }
}

/*!
 * @brief `src` の中の ANSI のフォルダの綴りを UTF-8 に置き換えて `dst` へ書く。
 * @return 置き換えたら 1（`dst` に書いた）。フォルダの綴りを含まない・入りきらないなら 0（`dst` は使わない）
 */
static int an_fix_acp_paths(const char *src, char *dst, size_t cap)
{
    size_t in = 0;
    size_t out = 0;
    int changed = 0;

    an_acp_dirs_refresh();
    if ((an_acp_dir_count == 0) || !an_has_high_byte(src)) {
        return 0;
    }
    while (src[in]) {
        int best = -1;
        size_t best_len = 0;
        int i;

        for (i = 0; i < an_acp_dir_count; i++) {
            const size_t len = strlen(an_acp_dirs[i]);
            if ((len > best_len) && (strncmp(src + in, an_acp_dirs[i], len) == 0)) {
                best = i;
                best_len = len;
            }
        }
        if (best >= 0) {
            const size_t len = strlen(an_acp_dirs_utf8[best]);
            if (out + len + 1 > cap) {
                return 0;
            }
            memcpy(dst + out, an_acp_dirs_utf8[best], len);
            out += len;
            in += best_len;
            changed = 1;
        } else {
            if (out + 2 > cap) {
                return 0;
            }
            dst[out++] = src[in++];
        }
    }
    dst[out] = 0;
    return changed;
}

static size_t an_text_mbstowcs(wchar_t *dest, const char *src, int n)
{
    int res;
    int required;
    wchar_t *tmp;
    char fixed[4096];

    /* 文に埋まった ANSI のパスを UTF-8 に直す（日本語の名前のフォルダ。`an_fix_acp_paths()`）。 */
    if (src && an_fix_acp_paths(src, fixed, sizeof(fixed))) {
        src = fixed;
    }

    if (dest) {
        res = MultiByteToWideChar(CP_UTF8, an_mb_flags(), src, -1, dest, n);
        if (res) {
            return (size_t)(res - 1);
        }
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            required = MultiByteToWideChar(CP_UTF8, an_mb_flags(), src, -1, NULL, 0);
            if (required <= 0) {
                return (size_t)-1;
            }
            tmp = malloc((size_t)required * sizeof(wchar_t));
            if (!tmp) {
                return (size_t)-1;
            }
            res = MultiByteToWideChar(CP_UTF8, an_mb_flags(), src, -1, tmp, required);
            if (res <= 0) {
                free(tmp);
                return (size_t)-1;
            }
            memcpy(dest, tmp, (size_t)n * sizeof(wchar_t));
            free(tmp);
            return (size_t)n;
        }
        return (size_t)-1;
    }
    return (size_t)(MultiByteToWideChar(CP_UTF8, an_mb_flags(), src, -1, NULL, 0) - 1);
}

static int an_text_wcsz(void)
{
    /* UTF-8 の 1 符号位置は最大 4 バイト。 */
    return 4;
}

static int an_text_wctomb(char *s, wchar_t wchar)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, &wchar, 1, s, an_text_wcsz(), NULL, NULL);
    return (n > 0) ? n : -1;
}

static int an_text_iswprint(wint_t wc)
{
    /*
     * サロゲート対の上位側もここへは 1 要素ずつ来る。`wint_t` を UTF-32 として
     * 判定すると必ず「印字不可」側に落ちるが、それは main-win.c の
     * `Term_iswprint_win()` の注記どおり——ui-term はサロゲート対を
     * 1 セルとして扱えないので、この判定でよい。
     *
     * 日本語（・フック #8）: 全角の字と埋め字
     * （`AN_WIDE_PAD`。私用領域）は印字できる字として通す——`text_out_to_screen()`
     * （`ui-output.c`）が印字できない字を空白に置き換えるため。どちらも英語では出ない。
     */
    if ((an_wcwidth((unsigned long)wc) != 1) && an_lang_enabled()) {
        return 1;
    }
    return iswprint((wint_t)wc);
}

#else /* !_WIN32 */

/*!
 * @brief UTF-8 の 1 符号位置を読み取る。
 * @details `src` の 1 バイト目が 0 なら文字列の終わりとして 0 を返す。壊れた
 * バイト列は U+FFFD（置き換え文字）として 1 バイトだけ読み進める——判定不能な
 * 入力で止まらないためで、Windows 側の `MB_ERR_INVALID_CHARS` とは違うが、
 * ここへ来る文字列はゲーム内部で作られた UTF-8 なので実際には起きない。
 */
static size_t an_utf8_decode_one(const char *src, uint32_t *cp)
{
    const unsigned char *s = (const unsigned char *)src;
    unsigned char c0 = s[0];
    int extra;
    uint32_t v;
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
        v = c0 & 0x1F;
    } else if ((c0 & 0xF0) == 0xE0) {
        extra = 2;
        v = c0 & 0x0F;
    } else if ((c0 & 0xF8) == 0xF0) {
        extra = 3;
        v = c0 & 0x07;
    } else {
        *cp = 0xFFFD;
        return 1;
    }
    for (i = 1; i <= extra; i++) {
        unsigned char ci = s[i];
        if ((ci & 0xC0) != 0x80) {
            *cp = 0xFFFD;
            return 1;
        }
        v = (v << 6) | (ci & 0x3F);
    }
    *cp = v;
    return (size_t)(extra + 1);
}

/*!
 * @brief `mbstowcs()` 相当。Linux の `wchar_t` は UTF-32 の符号位置そのものなので、
 * デコードした値をそのまま 1 要素として書く（サロゲート対の合成が要らない）。
 */
static size_t an_text_mbstowcs(wchar_t *dest, const char *src, int n)
{
    const char *p = src;
    size_t count = 0;

    if (!dest) {
        for (;;) {
            uint32_t cp;
            size_t adv = an_utf8_decode_one(p, &cp);
            if (adv == 0) {
                break;
            }
            p += adv;
            count++;
        }
        return count;
    }
    while ((int)count < n) {
        uint32_t cp;
        size_t adv = an_utf8_decode_one(p, &cp);
        if (adv == 0) {
            dest[count] = 0;
            return count;
        }
        p += adv;
        dest[count] = (wchar_t)cp;
        count++;
    }
    return count;
}

static int an_text_wcsz(void)
{
    /* UTF-8 の 1 符号位置は最大 4 バイト。 */
    return 4;
}

static int an_text_wctomb(char *s, wchar_t wchar)
{
    uint32_t cp = (uint32_t)wchar;

    if (cp < 0x80) {
        s[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        s[0] = (char)(0xC0 | (cp >> 6));
        s[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        s[0] = (char)(0xE0 | (cp >> 12));
        s[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        s[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    s[0] = (char)(0xF0 | (cp >> 18));
    s[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    s[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    s[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int an_text_iswprint(wint_t wc)
{
    /* 日本語（設計 §3.3・フック #8）: 上の Windows 版と同じ。 */
    if ((an_wcwidth((unsigned long)wc) != 1) && an_lang_enabled()) {
        return 1;
    }
    return iswprint(wc);
}

#endif /* _WIN32 */

void an_install_text_hooks(void)
{
#ifndef _WIN32
    /*
     * `iswprint()` はロケールで判定範囲が変わる。C ロケールのままだと ASCII 以外を
     * 全部「印字不可」にしてしまうので、UTF-8 の判定ができるロケールへ切り替える。
     * `platform/linux/core_main.cpp` の `main()` も同じ "C.UTF-8" を使っている。
     */
    setlocale(LC_CTYPE, "C.UTF-8");
#endif
    text_mbcs_hook = an_text_mbstowcs;
    text_wctomb_hook = an_text_wctomb;
    text_wcsz_hook = an_text_wcsz;
    text_iswprint_hook = an_text_iswprint;
}
