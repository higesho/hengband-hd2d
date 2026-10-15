/*
 * RogueClone2 の英語の表示（修-56 の第 2 回）。r2_shim.c だけが #include する（ビルドの一覧に足さずに済ませるため）。
 *
 * 原作ソースは -DJAPAN で組まれていて、持ち物の説明などを日本語の語順で継ぎ足す（修飾語＋名詞、「〜を手に入れた」の
 * ように動詞が後ろ）。英語のときは、継ぎ足す断片（r2_mesg_markers）を私用領域の 1 字（U+E000 + mesg の番号）に
 * しておき、画面へ書く手前の r2_localize_text() が英語の語順に組み直す。
 *
 *   - 持ち物の説明の 1 かたまり（数・修飾語・名詞・括弧・状態）を、原作の英語版（JAPAN を定義しない版）の get_desc と
 *     同じ形に組む（"a blue potion"・"2 scrolls titled 'blech foo'"・"a +1,+2 mace (weapon in hand)"）。
 *   - 動詞が後ろに付く文（"…を手に入れた"）は、英語の動詞を前に出す（"You now have …"）。
 *   - 色・材質・音節・既定の果物はゲーム開始時に写されて保存されるので mesg を差し替えない。起動時に控えた日本語の字
 *     （r2_mesg_ja）を辞書の鍵にして英語にする。
 *   - znum() の全角の数字と括弧は半角にし、「。」は「.」にする。
 *
 * 日本語のときは何も変えない（保存された英語の断片が残っていたときだけ、私用領域の字を日本語に戻す）。
 */
#pragma once

enum {
    R2T_ASCII = 1,
    R2T_MK,
    R2T_NUM,
    R2T_PAREN,
    R2T_BRACK,
    R2T_TITLE,
    R2T_DICT,
    R2T_OTHER,
};

typedef struct r2_tok {
    unsigned char type;
    short index; /* R2T_MK: mesg の番号。R2T_DICT: 辞書の番号 */
    char text[96];
} r2_tok;

#define R2_TOK_MAX 420

static const char *r2_marker_text(int index)
{
    size_t i;
    for (i = 0; i < sizeof(r2_marker_en) / sizeof(r2_marker_en[0]); ++i) {
        if (r2_marker_en[i].index == index) return r2_marker_en[i].text;
    }
    return "";
}

static int r2_is_syllable(int mesg_index) { return mesg_index >= 454 && mesg_index <= 493; }

/* 辞書（色・材質・音節・既定の果物）。鍵は起動時に控えた日本語の字。最長一致。 */
static int r2_dict_match(const char *p, int syllables, int *entry)
{
    size_t i, best = 0;
    for (i = 0; i < sizeof(r2_dict_en) / sizeof(r2_dict_en[0]); ++i) {
        const int index = r2_dict_en[i].index;
        const char *ja;
        size_t len;
        if (r2_is_syllable(index) != (syllables ? 1 : 0)) continue;
        ja = r2_mesg_ja[index];
        len = strlen(ja);
        if (len < 2 || len <= best) continue;
        if (strncmp(p, ja, len) == 0) {
            best = len;
            *entry = (int)i;
        }
    }
    return (int)best;
}

static int r2_utf8_len(const unsigned char *p)
{
    if (*p < 0x80) return 1;
    if ((*p & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) return 2;
    if ((*p & 0xf0) == 0xe0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) return 3;
    if ((*p & 0xf8) == 0xf0 && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80 && (p[3] & 0xc0) == 0x80) return 4;
    return 1;
}

static unsigned r2_utf8_cp(const unsigned char *p, int len)
{
    if (len == 1) return p[0];
    if (len == 2) return ((unsigned)(p[0] & 0x1f) << 6) | (p[1] & 0x3f);
    if (len == 3) return ((unsigned)(p[0] & 0x0f) << 12) | ((unsigned)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
    return ((unsigned)(p[0] & 0x07) << 18) | ((unsigned)(p[1] & 0x3f) << 12) | ((unsigned)(p[2] & 0x3f) << 6) | (p[3] & 0x3f);
}

/* 全角の数（＋−０〜９）。読めたら字節数を返し、半角の文字列を out へ。 */
static int r2_fullwidth_number(const unsigned char *p, char *out, size_t cap)
{
    size_t used = 0;
    const unsigned char *q = p;
    int digits = 0;
    if (q[0] == 0xef && q[1] == 0xbc && q[2] == 0x8b) { /* ＋ */
        if (used + 1 < cap) out[used++] = '+';
        q += 3;
    } else if (q[0] == 0xe2 && q[1] == 0x88 && q[2] == 0x92) { /* − */
        if (used + 1 < cap) out[used++] = '-';
        q += 3;
    }
    while (q[0] == 0xef && q[1] == 0xbc && q[2] >= 0x90 && q[2] <= 0x99) {
        if (used + 1 < cap) out[used++] = (char)('0' + (q[2] - 0x90));
        q += 3;
        ++digits;
    }
    if (digits == 0) return 0;
    out[used] = 0;
    return (int)(q - p);
}

static void r2_tok_add(r2_tok *t, int *n, int type, int index, const char *text, size_t len)
{
    if (*n > 0 && (type == R2T_ASCII || type == R2T_OTHER) && t[*n - 1].type == type) {
        r2_tok *last = &t[*n - 1];
        const size_t have = strlen(last->text);
        if (have + len < sizeof(last->text)) {
            memcpy(last->text + have, text, len);
            last->text[have + len] = 0;
            return;
        }
    }
    if (*n >= R2_TOK_MAX) return;
    t[*n].type = (unsigned char)type;
    t[*n].index = (short)index;
    if (len >= sizeof(t[*n].text)) len = sizeof(t[*n].text) - 1;
    memcpy(t[*n].text, text, len);
    t[*n].text[len] = 0;
    ++*n;
}

/* 「ぶん に とす 」→ 'blech foo barf'。音節が全部辞書に当たったときだけ。 */
static int r2_parse_title(const unsigned char *p, char *out, size_t cap, int *consumed)
{
    const unsigned char *q = p + 3; /* 「 */
    size_t used = 0;
    int words = 0;
    if (cap < 8) return 0;
    out[used++] = '\'';
    while (!(q[0] == 0xe3 && q[1] == 0x80 && q[2] == 0x8d)) { /* 」 */
        int entry = -1;
        int len;
        const char *en;
        size_t el;
        if (*q == 0) return 0;
        len = r2_dict_match((const char *)q, 1, &entry);
        if (len == 0 || entry < 0) return 0;
        en = r2_dict_en[entry].text;
        el = strlen(en);
        if (used + el + 3 >= cap) return 0;
        if (words > 0) out[used++] = ' ';
        memcpy(out + used, en, el);
        used += el;
        ++words;
        q += len;
    }
    if (words == 0) return 0;
    out[used++] = '\'';
    out[used] = 0;
    *consumed = (int)(q + 3 - p);
    return 1;
}

/* 括弧の中身（znum の数と「，」）を半角にして読む。open は（［、close は）］。 */
static int r2_parse_bracket(const unsigned char *p, int open_c, int close_c, char *out, size_t cap, int *consumed)
{
    const unsigned char *q = p + 3;
    size_t used = 0;
    while (!(q[0] == 0xef && q[1] == 0xbc && q[2] == (unsigned char)close_c)) {
        char num[24];
        int len;
        if (*q == 0) return 0;
        if ((len = r2_fullwidth_number(q, num, sizeof(num))) > 0) {
            const size_t nl = strlen(num);
            if (used + nl + 1 >= cap) return 0;
            memcpy(out + used, num, nl);
            used += nl;
            q += len;
        } else if (q[0] == 0xef && q[1] == 0xbc && q[2] == 0x8c) { /* ， */
            if (used + 2 >= cap) return 0;
            out[used++] = ',';
            q += 3;
        } else {
            len = r2_utf8_len(q);
            if (used + (size_t)len + 1 >= cap) return 0;
            memcpy(out + used, q, (size_t)len);
            used += (size_t)len;
            q += len;
        }
    }
    (void)open_c;
    out[used] = 0;
    *consumed = (int)(q + 3 - p);
    return 1;
}

static int r2_tokenize(const char *in, r2_tok *t, int cap)
{
    const unsigned char *p = (const unsigned char *)in;
    int n = 0;
    (void)cap;
    while (*p != 0) {
        char buf[96];
        int len, entry = -1, consumed = 0;
        if (*p < 0x80) {
            const unsigned char *s = p;
            while (*p != 0 && *p < 0x80) ++p;
            r2_tok_add(t, &n, R2T_ASCII, 0, (const char *)s, (size_t)(p - s));
            continue;
        }
        if (p[0] == 0xee && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
            const unsigned cp = r2_utf8_cp(p, 3);
            if (cp >= 0xe000u && cp < 0xe200u) {
                r2_tok_add(t, &n, R2T_MK, (int)(cp - 0xe000u), "", 0);
                p += 3;
                continue;
            }
        }
        if ((len = r2_fullwidth_number(p, buf, sizeof(buf))) > 0) {
            r2_tok_add(t, &n, R2T_NUM, 0, buf, strlen(buf));
            p += len;
            continue;
        }
        if (p[0] == 0xef && p[1] == 0xbc && p[2] == 0x88 && r2_parse_bracket(p, 0x88, 0x89, buf, sizeof(buf), &consumed)) { /* （ ） */
            r2_tok_add(t, &n, R2T_PAREN, 0, buf, strlen(buf));
            p += consumed;
            continue;
        }
        if (p[0] == 0xef && p[1] == 0xbc && p[2] == 0xbb && r2_parse_bracket(p, 0xbb, 0xbd, buf, sizeof(buf), &consumed)) { /* ［ ］ */
            r2_tok_add(t, &n, R2T_BRACK, 0, buf, strlen(buf));
            p += consumed;
            continue;
        }
        if (p[0] == 0xe3 && p[1] == 0x80 && p[2] == 0x8c && r2_parse_title(p, buf, sizeof(buf), &consumed)) { /* 「 」 */
            r2_tok_add(t, &n, R2T_TITLE, 0, buf, strlen(buf));
            p += consumed;
            continue;
        }
        if ((len = r2_dict_match((const char *)p, 0, &entry)) > 0 && entry >= 0) {
            /* 杖・指輪の材質と宝石には、ゲーム開始時に「の」（mesg[39]・[40]）が付く。英語では要らない。 */
            if (r2_dict_en[entry].index >= 410 && r2_dict_en[entry].index <= 453 && p[len] == 0xe3 && p[len + 1] == 0x81 && p[len + 2] == 0xae) {
                len += 3;
            }
            r2_tok_add(t, &n, R2T_DICT, r2_dict_en[entry].index, r2_dict_en[entry].text, strlen(r2_dict_en[entry].text));
            p += len;
            continue;
        }
        len = r2_utf8_len(p);
        r2_tok_add(t, &n, R2T_OTHER, 0, (const char *)p, (size_t)len);
        p += len;
    }
    return n;
}

static size_t r2_out_put(char *out, size_t cap, size_t used, const char *text)
{
    const size_t n = strlen(text);
    if (used + n + 1 > cap) return used;
    memcpy(out + used, text, n);
    out[used + n] = 0;
    return used + n;
}

/* 日本語の記号を半角に（読み残しの字は原文のまま）。 */
static size_t r2_put_other(char *out, size_t cap, size_t used, const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p != 0) {
        const int len = r2_utf8_len(p);
        const unsigned cp = r2_utf8_cp(p, len);
        const char *ascii = 0;
        switch (cp) {
        case 0x3002: ascii = "."; break;  /* 。 */
        case 0x3001: ascii = ","; break;  /* 、 */
        case 0xff01: ascii = "!"; break;
        case 0xff1f: ascii = "?"; break;
        case 0xff1a: ascii = ":"; break;
        case 0x3000: ascii = " "; break;
        case 0xff08: ascii = "("; break;
        case 0xff09: ascii = ")"; break;
        case 0xff3b: ascii = "["; break;
        case 0xff3d: ascii = "]"; break;
        case 0x300c: case 0x300d: ascii = "'"; break;
        default: break;
        }
        if (ascii != 0) {
            used = r2_out_put(out, cap, used, ascii);
        } else if (used + (size_t)len + 1 <= cap) {
            memcpy(out + used, p, (size_t)len);
            used += (size_t)len;
            out[used] = 0;
        }
        p += len;
    }
    return used;
}

static size_t r2_render_plain(const r2_tok *t, int a, int b, char *out, size_t cap, size_t used)
{
    int i;
    for (i = a; i < b; ++i) {
        switch (t[i].type) {
        case R2T_ASCII: used = r2_out_put(out, cap, used, t[i].text); break;
        case R2T_MK: used = r2_out_put(out, cap, used, r2_marker_text(t[i].index)); break;
        case R2T_NUM: used = r2_out_put(out, cap, used, t[i].text); break;
        case R2T_PAREN: used = r2_out_put(out, cap, used, "("); used = r2_out_put(out, cap, used, t[i].text); used = r2_out_put(out, cap, used, ")"); break;
        case R2T_BRACK: used = r2_out_put(out, cap, used, "["); used = r2_out_put(out, cap, used, t[i].text); used = r2_out_put(out, cap, used, "]"); break;
        case R2T_TITLE: used = r2_out_put(out, cap, used, t[i].text); break;
        case R2T_DICT: used = r2_out_put(out, cap, used, t[i].text); break;
        default: used = r2_put_other(out, cap, used, t[i].text); break;
        }
    }
    return used;
}

static int r2_is_verb_marker(int index)
{
    switch (index) {
    case 69: case 70: case 92: case 94: case 100: case 107: case 114: case 166: case 205: return 1;
    default: return 0;
    }
}

static int r2_is_name_marker(int index)
{
    return (index >= 2 && index <= 9) || index == 80 || (index >= 374 && index <= 388);
}

static int r2_is_real_marker(int index)
{
    return (index >= 348 && index <= 373) || (index >= 389 && index <= 409);
}

static void r2_append(char *dst, size_t cap, const char *src)
{
    const size_t have = strlen(dst);
    size_t n = strlen(src);
    if (have + 1 >= cap) return;
    if (have + n + 1 > cap) n = cap - have - 1;
    memcpy(dst + have, src, n);
    dst[have + n] = 0;
}

static void r2_trim_spaces(char *s)
{
    size_t n = strlen(s);
    size_t lead = 0;
    while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
    while (s[lead] == ' ') ++lead;
    if (lead > 0) memmove(s, s + lead, n - lead + 1);
}

/*
 * 持ち物の説明の 1 かたまり（t[a..b)）を、原作の英語版の get_desc と同じ形に組む。
 * 読めない並びなら 0 を返す（呼び出し側は原文のまま平らに書く）。*is_gold は金塊のとき 1。
 */
static int r2_group_english(const r2_tok *t, int a, int b, char *out, size_t cap, int *is_gold, int no_article)
{
    int qty = 1, has_qty = 0, qk = 0;
    int name = -1, real = -1, titled = 0, called = 0, status = 0, amulet = 0, fruit_name = 0;
    const char *adj = 0;
    char title[96], user[160], parens[2][48], brack[48], body[300], noun[96], tmp[200];
    int np = 0, i;
    const int is_weapon_fn = 0;
    (void)is_weapon_fn;
    title[0] = user[0] = brack[0] = body[0] = noun[0] = 0;
    parens[0][0] = parens[1][0] = 0;
    *is_gold = 0;
    i = a;
    if (i < b && t[i].type == R2T_NUM) {
        if (i + 1 < b && t[i + 1].type == R2T_MK && t[i + 1].index >= 28 && t[i + 1].index <= 32) {
            qty = atoi(t[i].text);
            qk = t[i + 1].index;
            has_qty = 1;
            i += 2;
        } else {
            return 0;
        }
    }
    for (; i < b; ++i) {
        switch (t[i].type) {
        case R2T_MK:
            if (t[i].index == 27) amulet = 1;
            else if (t[i].index == 33) titled = 1;
            else if (t[i].index == 34) called = 1;
            else if (t[i].index >= 35 && t[i].index <= 38) status = t[i].index;
            else if (r2_is_name_marker(t[i].index)) name = t[i].index;
            else if (r2_is_real_marker(t[i].index)) real = t[i].index;
            else return 0;
            break;
        case R2T_PAREN:
            if (np < 2) { snprintf(parens[np], sizeof(parens[np]), "%s", t[i].text); ++np; }
            break;
        case R2T_BRACK: snprintf(brack, sizeof(brack), "%s", t[i].text); break;
        case R2T_TITLE: snprintf(title, sizeof(title), "%s", t[i].text); break;
        case R2T_DICT:
            if (t[i].index == 333) fruit_name = 1;
            else adj = t[i].text;
            break;
        case R2T_OTHER:
            if (strcmp(t[i].text, "\xe3\x81\xae") == 0) break; /* 材質・宝石に付く「の」 */
            r2_append(user, sizeof(user), t[i].text);
            break;
        case R2T_ASCII:
            r2_append(user, sizeof(user), t[i].text);
            break;
        default:
            return 0;
        }
    }
    r2_trim_spaces(user);
    if (user[0] != 0 && name < 0 && !amulet && !fruit_name && qk != 28) {
        /* 遊ぶ人の付けた果物の名前（辞書に無い字）。 */
        extern char *fruit;
        if (fruit != 0 && strcmp(user, fruit) == 0) fruit_name = 1;
    }
    out[0] = 0;
    if (qk == 28) { /* 金塊 */
        snprintf(out, cap, "%d %s", qty, r2_marker_text(28));
        *is_gold = 1;
        return 1;
    }
    if (amulet) {
        snprintf(out, cap, "%s", r2_marker_text(27));
    } else if (fruit_name || name < 0) {
        const char *f = fruit_name && t[a].type == R2T_DICT && t[a].index == 333 ? r2_dict_en[0].text : user;
        int k;
        if (fruit_name) {
            f = user;
            for (k = a; k < b; ++k) {
                if (t[k].type == R2T_DICT && t[k].index == 333) { f = t[k].text; break; }
            }
        }
        if (name >= 0 || (f[0] == 0)) return 0;
        if (has_qty && qty > 1) snprintf(body, sizeof(body), "%d %ss", qty, f);
        else snprintf(body, sizeof(body), "a %s", f);
        snprintf(out, cap, "%s", body);
    } else if (name == 2) { /* 食糧 */
        /* 原作の日本語の版は 1 袋でも「１袋の」と数を付ける。英語は 1 つなら "some food"。 */
        if (has_qty && qty > 1) snprintf(out, cap, "%d rations of %s", qty, r2_marker_text(2));
        else snprintf(out, cap, "some %s", r2_marker_text(2));
    } else {
        const int weapon = name >= 374 && name <= 381;
        const int armor = name >= 382 && name <= 388;
        const int plural = has_qty && qty > 1;
        char core[240];
        snprintf(noun, sizeof(noun), "%s%s", r2_marker_text(name), plural && !armor ? "s" : "");
        core[0] = 0;
        if (armor) {
            if (np > 0) { r2_append(core, sizeof(core), parens[0]); r2_append(core, sizeof(core), " "); }
            r2_append(core, sizeof(core), noun);
            if (brack[0] != 0) { snprintf(tmp, sizeof(tmp), " [%s]", brack); r2_append(core, sizeof(core), tmp); }
        } else if (weapon) {
            if (np > 0) { r2_append(core, sizeof(core), parens[0]); r2_append(core, sizeof(core), " "); }
            r2_append(core, sizeof(core), noun);
        } else {
            /* 巻物・水薬・杖・指輪 */
            if (name == 8 && np > 0) { r2_append(core, sizeof(core), parens[0]); r2_append(core, sizeof(core), " "); }
            if (real >= 0) {
                snprintf(tmp, sizeof(tmp), "%s of %s", noun, r2_marker_text(real));
                r2_append(core, sizeof(core), tmp);
            } else if (called) {
                snprintf(tmp, sizeof(tmp), "%s called %s", noun, user);
                r2_append(core, sizeof(core), tmp);
            } else if (titled || (name == 3 && title[0] != 0)) {
                snprintf(tmp, sizeof(tmp), "%s titled %s", noun, title[0] != 0 ? title : user);
                r2_append(core, sizeof(core), tmp);
            } else {
                if (adj != 0) { r2_append(core, sizeof(core), adj); r2_append(core, sizeof(core), " "); }
                r2_append(core, sizeof(core), noun);
            }
            if (name == 5 && brack[0] != 0) { snprintf(tmp, sizeof(tmp), " [%s]", brack); r2_append(core, sizeof(core), tmp); }
        }
        if (armor) {
            snprintf(out, cap, "%s", core);
        } else if (has_qty) {
            snprintf(out, cap, "%d %s", qty, core);
        } else if (no_article) {
            /* 発見した物の一覧（空白だけが前に付く行）は冠詞を付けない（"Scroll of magic mapping"） */
            snprintf(out, cap, "%s", core);
        } else {
            const char c = core[0];
            const int vowel = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
            snprintf(out, cap, "%s %s", vowel ? "an" : "a", core);
        }
    }
    if (status != 0) {
        r2_append(out, cap, " ");
        r2_append(out, cap, r2_marker_text(status));
    }
    return 1;
}

static void r2_upper_first(char *s)
{
    for (; *s != 0; ++s) {
        if (*s >= 'a' && *s <= 'z') { *s = (char)(*s - 'a' + 'A'); return; }
        if (*s != ' ' && *s != '(' && *s != '[') return;
    }
}

/* 「x) 」「  」のような、持ち物の説明の前に付く文字。 */
static int r2_is_list_prefix(const char *s)
{
    size_t i = 0;
    while (s[i] == ' ') ++i;
    if (s[i] == 0) return 1;
    if (((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z')) && (s[i + 1] == ')' || s[i + 1] == '}')) {
        i += 2;
        while (s[i] == ' ') ++i;
        return s[i] == 0;
    }
    return 0;
}

static int r2_group_start_ok(const r2_tok *t, int from, int n)
{
    int i;
    switch (t[from].type) {
    case R2T_NUM: case R2T_PAREN: case R2T_TITLE: case R2T_DICT: return 1;
    case R2T_MK:
        return !r2_is_verb_marker(t[from].index) && (r2_is_name_marker(t[from].index) || r2_is_real_marker(t[from].index)
            || t[from].index == 27 || (t[from].index >= 28 && t[from].index <= 38));
    case R2T_OTHER: case R2T_ASCII:
        /* 遊ぶ人が付けた呼び名・果物の名前で始まる説明（"foo" + と呼ぶ + 水薬）。「と呼ぶ」「という」が続くときだけ。 */
        for (i = from + 1; i < n; ++i) {
            if (t[i].type == R2T_MK && (t[i].index == 33 || t[i].index == 34)) return 1;
            if (t[i].type == R2T_MK && r2_is_verb_marker(t[i].index)) return 0;
        }
        return 0;
    default: return 0;
    }
}

/* 得点表の 1 行（地下 N 階にて…）を英語の語順にする。当てはまらなければ 0。 */
static int r2_score_row_english(const r2_tok *t, int n, char *out, size_t cap)
{
    int k = -1, i, amulet = 0, m = -1;
    char name[96], cause[160], level[24];
    size_t used = 0;
    for (i = 0; i < n; ++i) {
        if (t[i].type == R2T_MK && t[i].index == 190) { k = i; break; }
    }
    if (k < 0 || k + 2 >= n || t[k + 1].type != R2T_NUM || t[k + 2].type != R2T_MK || t[k + 2].index != 191) return 0;
    if (k > 0 && t[k - 1].type == R2T_MK && t[k - 1].index == 189) amulet = 1;
    snprintf(level, sizeof(level), "%s", t[k + 1].text);
    name[0] = cause[0] = 0;
    for (i = k + 3; i < n; ++i) {
        if (t[i].type == R2T_MK && t[i].index == 197) { m = i; break; }
    }
    out[0] = 0;
    used = r2_render_plain(t, 0, amulet ? k - 1 : k, out, cap, used);
    if (m >= 0) {
        char who[96];
        size_t wu = 0;
        who[0] = 0;
        wu = r2_render_plain(t, k + 3, m, who, sizeof(who), wu);
        snprintf(cause, sizeof(cause), "%s%s %s", r2_marker_text(197),
            (who[0] == 'a' || who[0] == 'e' || who[0] == 'i' || who[0] == 'o' || who[0] == 'u') ? "an" : "a", who);
        used = r2_out_put(out, cap, used, cause);
        used = r2_out_put(out, cap, used, " on level ");
        used = r2_out_put(out, cap, used, level);
        if (amulet) used = r2_out_put(out, cap, used, " with Amulet");
        used = r2_render_plain(t, m + 1, n, out, cap, used);
    } else {
        /* 原因の字（died of … など）は直接英語。最初の「。」の前まで。 */
        char rest[200];
        char *dot;
        size_t ru = 0;
        rest[0] = 0;
        ru = r2_render_plain(t, k + 3, n, rest, sizeof(rest), ru);
        dot = strchr(rest, '.');
        if (dot != 0) {
            const char saved = *dot;
            *dot = 0;
            used = r2_out_put(out, cap, used, rest);
            used = r2_out_put(out, cap, used, " on level ");
            used = r2_out_put(out, cap, used, level);
            if (amulet) used = r2_out_put(out, cap, used, " with Amulet");
            *dot = saved;
            used = r2_out_put(out, cap, used, dot);
        } else {
            used = r2_out_put(out, cap, used, rest);
            used = r2_out_put(out, cap, used, " on level ");
            used = r2_out_put(out, cap, used, level);
            if (amulet) used = r2_out_put(out, cap, used, " with Amulet");
        }
    }
    return 1;
}

static int r2_text_has_high(const char *s)
{
    for (; *s != 0; ++s) {
        if ((unsigned char)*s >= 0x80) return 1;
    }
    return 0;
}

/* 日本語のとき：保存された英語の断片（私用領域の字）だけを日本語に戻す。 */
static void r2_expand_markers_japanese(const char *in, char *out, size_t cap)
{
    const unsigned char *p = (const unsigned char *)in;
    size_t used = 0;
    while (*p != 0 && used + 1 < cap) {
        if (p[0] == 0xee && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) {
            const unsigned cp = r2_utf8_cp(p, 3);
            if (cp >= 0xe000u && cp < 0xe200u) {
                used = r2_out_put(out, cap, used, r2_mesg_ja[cp - 0xe000u]);
                p += 3;
                continue;
            }
        }
        out[used++] = (char)*p++;
        out[used] = 0;
    }
    out[used] = 0;
}

/*
 * 画面へ書く手前の変換。英語のとき、継ぎ足された日本語の語順の字を英語に組み直す。
 * 戻り値は変換後の字を out に作ったとき 1、そのまま使ってよいとき 0（out は触らない）。
 */
int r2_localize_text(const char *in, char *out, size_t cap)
{
    r2_tok t[R2_TOK_MAX];
    int n, g0 = 0, ge, vi = -1, i;
    size_t used = 0, e;
    if (in == 0 || out == 0 || cap < 8) return 0;
    if (!r2_text_has_high(in)) return 0;
    if (!r2_lang_applied_en) {
        const unsigned char *p = (const unsigned char *)in;
        int has_marker = 0;
        for (; *p != 0; ++p) {
            if (p[0] == 0xee && (p[1] & 0xc0) == 0x80 && (p[2] & 0xc0) == 0x80) { has_marker = 1; break; }
        }
        if (!has_marker) return 0;
        r2_expand_markers_japanese(in, out, cap);
        return 1;
    }
    /* 原作が mesg を通さずに直書きする日本語（完全に一致したときだけ。前の空白は残す）。 */
    {
        const char *body = in;
        size_t lead = 0;
        while (*body == ' ') { ++body; ++lead; }
        for (e = 0; e < sizeof(r2_exact_en) / sizeof(r2_exact_en[0]); ++e) {
            if (strcmp(body, r2_exact_en[e].japanese) == 0) {
                if (lead + strlen(r2_exact_en[e].english) + 1 > cap) return 0;
                memset(out, ' ', lead);
                snprintf(out + lead, cap - lead, "%s", r2_exact_en[e].english);
                return 1;
            }
        }
    }
    n = r2_tokenize(in, t, R2_TOK_MAX);
    out[0] = 0;
    if (r2_score_row_english(t, n, out, cap)) return 1;
    out[0] = 0;
    if (n > 0 && t[0].type == R2T_ASCII && r2_is_list_prefix(t[0].text)) g0 = 1;
    if (g0 < n && r2_group_start_ok(t, g0, n)) {
        char desc[300];
        int is_gold = 0;
        ge = n;
        for (i = g0; i < n; ++i) {
            if (t[i].type == R2T_MK && r2_is_verb_marker(t[i].index)) { vi = i; ge = i; break; }
        }
        if (r2_group_english(t, g0, ge, desc, sizeof(desc), &is_gold, (g0 == 1 && t[0].text[0] == ' ' && t[0].text[strspn(t[0].text, " ")] == 0) || (g0 == 0 && t[0].type == R2T_ASCII && t[0].text[0] == ' '))) {
            if (g0 == 1) {
                used = r2_out_put(out, cap, used, t[0].text);
            } else if (t[g0].type == R2T_ASCII) {
                const char *sp = t[g0].text;
                while (*sp == ' ' && used + 2 < cap) { out[used++] = ' '; out[used] = 0; ++sp; } /* 先頭の空白は残す */
            }
            if (vi >= 0) {
                const int idx = t[vi].index;
                if ((idx == 69 || idx == 114) && is_gold) used = r2_out_put(out, cap, used, "You found ");
                else used = r2_out_put(out, cap, used, r2_marker_text(idx));
                used = r2_out_put(out, cap, used, desc);
                if (vi + 1 < n && t[vi + 1].type == R2T_ASCII && t[vi + 1].text[0] == '(') used = r2_out_put(out, cap, used, " ");
                used = r2_render_plain(t, vi + 1, n, out, cap, used);
            } else {
                char shown[300];
                snprintf(shown, sizeof(shown), "%s", desc);
                r2_upper_first(shown);
                used = r2_out_put(out, cap, used, shown);
                used = r2_render_plain(t, ge, n, out, cap, used);
            }
            return 1;
        }
    }
    r2_render_plain(t, 0, n, out, cap, 0);
    return 1;
}
